#include "Socket.h"
#include "Framework.h"
#pragma comment (lib, "ws2_32.lib")


/**
	@brief		멤버변수 초기화, buffer clear 및 WSABuf 설정을 진행한다.
*/
Socket::Socket()
	: hWnd(NULL)
	, clientSocket(INVALID_SOCKET)
	, isInitialized(false)
	, inPacketSize(0)
	, savedPacketSize(0)
{
	::ZeroMemory(sendBuf, sizeof(sendBuf));
	::ZeroMemory(recvBuf, sizeof(recvBuf));
	::ZeroMemory(packetBuf, sizeof(packetBuf));

	sendWsaBuf.buf = reinterpret_cast<CHAR*>(sendBuf);
	sendWsaBuf.len = sizeof(sendBuf);
	recvWsaBuf.buf = reinterpret_cast<CHAR*>(recvBuf);
	recvWsaBuf.len = sizeof(recvBuf);
}

Socket::~Socket()
{
}


/**
	@brief		소켓생성 - connect - AsyncSelect 순으로 진행한다.
	@return		모두 성공시에 true 반환
				윈도우 핸들 초기화 이후에 실패시에는 해당 실패상황 메시지박스 출력, false 반환

	@warning	ip가 raw pointer이며, nullptr 검사만 진행하므로 주의한다. (invalid할 시 connect 실패일 것이다.)
*/
bool Socket::Initialize(HWND mainWindow, const char* serverIP)
{
	if (serverIP == nullptr || mainWindow == NULL
		|| isInitialized == true)
	{
		return false;
	}

	hWnd = mainWindow;
	clientSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	if (clientSocket == INVALID_SOCKET)
	{
		::MessageBox(mainWindow, "WSASocket Error!!", "Error!!", MB_OK);
		closesocket(clientSocket);
		return false;
	}

	SOCKADDR_IN serverAddr;
	::ZeroMemory(&serverAddr, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(Packet_Base::PORT_NUMBER);
	serverAddr.sin_addr.s_addr = inet_addr(serverIP);

	int retVal = WSAConnect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr), nullptr, nullptr, nullptr, nullptr);
	if (SOCKET_ERROR == retVal)
	{
		::MessageBox(mainWindow, "WSAConnect Error!!", "Error!!", MB_OK);
		return false;
	}

	retVal = WSAAsyncSelect(clientSocket, mainWindow, WM_SOCKET, FD_CLOSE | FD_READ);
	if (SOCKET_ERROR == retVal)
	{
		::MessageBox(mainWindow, "WSAAsyncSelect Error!!", "Error!!", MB_OK);
		return false;
	}

	isInitialized = true;
	return isInitialized;
}

/**
	@brief		데이터를 Recv하여 패킷 조립, 처리함수 호출한다.
	@details	wsabuf 데이터를 packetBuffer로 복사하며 조립을 진행한다.

	@todo		패킷 조립코드는 더 좋고 간결한 방법을 찾아낼때마다 적용하고 테스트해야 한다.
	@author		cgHan
*/
void Socket::ReadPacket(SOCKET sock)
{
	DWORD iobyte = 0, ioflag = 0;
	int ret = WSARecv(sock, &recvWsaBuf, 1, &iobyte, &ioflag, nullptr, nullptr);
	if (0 != ret)
	{
		::MessageBox(hWnd, "WSARecv Error!", "Error!!", MB_OK);
		return;
	}

	DWORD remainedIoByte = iobyte;
	unsigned char* ioPtr = reinterpret_cast<unsigned char*>(recvWsaBuf.buf);

	if (savedPacketSize + remainedIoByte > sizeof(packetBuf))
	{
		int empty = sizeof(packetBuf) - savedPacketSize;
		std::memcpy(packetBuf + savedPacketSize, ioPtr, empty);

		remainedIoByte -= empty;
		ioPtr += empty;
		savedPacketSize += empty;
	}
	else
	{
		std::memcpy(packetBuf + savedPacketSize, ioPtr, remainedIoByte);
		savedPacketSize = iobyte;
		remainedIoByte = 0;
	}

	do
	{
		inPacketSize = GetPacketSize(packetBuf);

		//조립가능
		if (inPacketSize <= savedPacketSize)
		{
			ProcessPacket(packetBuf, inPacketSize);
			std::memmove(packetBuf, packetBuf + inPacketSize
				, savedPacketSize - inPacketSize);

			savedPacketSize -= inPacketSize;
			inPacketSize = 0;

			if (remainedIoByte > 0 
				&& (savedPacketSize + remainedIoByte) <= sizeof(packetBuf))
			{
				std::memcpy(packetBuf + savedPacketSize, ioPtr, remainedIoByte);
				savedPacketSize += remainedIoByte;
				remainedIoByte = 0;
			}
		}
	} while (inPacketSize == 0);
}



/**
	@brief		사용자 요청에 의해 최종적으로 Send가 호출되는 함수
	@details	blocking send이다.

	@param packet 보내고자 하는 패킷, Serialize 되어있다(되어있어야 한다).
*/
void Socket::SendPacket(const void* packet)
{
	DWORD ioBytes = 0;
	sendWsaBuf.len = GetPacketSize(packet);
	int ret = WSASend(clientSocket, &sendWsaBuf, 1, &ioBytes, 0, NULL, NULL);
	if (SOCKET_ERROR == ret)
	{
		::MessageBox(hWnd, "WSASend Error!", "Error!!", MB_OK);
	}
}

/**
	@brief		패킷 조립 완료로 Framework의 패킷 프로시저를 호출하도록 한다.
	@details	단순하게 인자검사 없이 전달한다.
*/
void Socket::ProcessPacket(const void* packet, int size)
{
	Framework::GetInstance()->ProcessPacket(packet, size);
}