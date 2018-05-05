#include "Socket.h"
#include "Framework.h"
#pragma comment (lib, "ws2_32.lib")


/**
	@brief		멤버변수 초기화, WSABuf 설정
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
	@brief		소켓생성 - Connect - AsyncSelect 등록 진행
	@return		모두 성공시에 true 반환
				윈도우 핸들 초기화 이후에 실패시에는 메시지박스 출력
	@warning	ip가 날 포인터이며, nullptr 검사만 진행하므로 주의한다. (invalid할 시 connect 실패일 것이다.)
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
	@brief		데이터 Recv하여 패킷 조립, 처리함수 호출
	@detailis	AsyncSelect Read 이벤트 발생시 호출되어 blocking recv 진행
				이전에 io가 덜 되어 조립하지 못한 경우에 대한 처리
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

void Socket::ProcessPacket(const void* packet, int size)
{
	Framework::GetInstance()->ProcessPacket(packet, size);
}