#include "Socket.h"
#include "Framework.h"
#pragma comment (lib, "ws2_32.lib")


/**
	@brief		������� �ʱ�ȭ, WSABuf ����
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
	@brief		���ϻ��� - Connect - AsyncSelect ��� ����
	@return		��� �����ÿ� true ��ȯ
				������ �ڵ� �ʱ�ȭ ���Ŀ� ���нÿ��� �޽����ڽ� ���
	@warning	ip�� �� �������̸�, nullptr �˻縸 �����ϹǷ� �����Ѵ�. (invalid�� �� connect ������ ���̴�.)
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
	@brief		������ Recv�Ͽ� ��Ŷ ����, ó���Լ� ȣ��
	@detailis	AsyncSelect Read �̺�Ʈ �߻��� ȣ��Ǿ� blocking recv ����
				������ io�� �� �Ǿ� �������� ���� ��쿡 ���� ó��
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

		//��������
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