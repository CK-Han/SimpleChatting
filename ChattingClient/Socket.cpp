#include "Socket.h"
#include "Framework.h"
#pragma comment (lib, "ws2_32.lib")


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


bool Socket::Initialize(HWND mainWindow, const char* serverIP)
{
	if (serverIP == nullptr || mainWindow == NULL
		|| isInitialized == true)
	{
		//error!
		return false;
	}

	hWnd = mainWindow;
	clientSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	if (clientSocket == INVALID_SOCKET)
		return false;

	SOCKADDR_IN serverAddr;
	::ZeroMemory(&serverAddr, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(MY_SERVER_PORT);
	serverAddr.sin_addr.s_addr = inet_addr(serverIP);

	int Result = WSAConnect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr), nullptr, nullptr, nullptr, nullptr);
	if (SOCKET_ERROR == Result)
	{
		::MessageBox(mainWindow, "WSAConnect Error!!", "Error!!", MB_OK);
		return false;
	}

	WSAAsyncSelect(clientSocket, mainWindow, WM_SOCKET, FD_CLOSE | FD_READ);

	isInitialized = true;
	return isInitialized;
}

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
	unsigned char* ptr = reinterpret_cast<unsigned char*>(recvWsaBuf.buf);

	if (savedPacketSize + remainedIoByte > sizeof(packetBuf))
	{
		int empty = sizeof(packetBuf) - savedPacketSize;
		std::memcpy(packetBuf + savedPacketSize, ptr, empty);

		remainedIoByte -= empty;
		ptr += empty;
		savedPacketSize += empty;
	}
	else
	{
		std::memcpy(packetBuf + savedPacketSize, ptr, remainedIoByte);
		savedPacketSize = iobyte;
		remainedIoByte = 0;
	}

	do
	{
		inPacketSize = GetPacketSize(packetBuf);

		if (inPacketSize <= savedPacketSize)
		{	//조립가능
			ProcessPacket(packetBuf);
			std::memmove(packetBuf, packetBuf + inPacketSize
				, savedPacketSize - inPacketSize);

			savedPacketSize -= inPacketSize;
			inPacketSize = 0;
		}
	} while (inPacketSize == 0);

	if (remainedIoByte > 0)
	{
		std::memcpy(packetBuf + savedPacketSize, ptr, remainedIoByte);
		savedPacketSize += remainedIoByte;
	}
}


void Socket::SendPacket(unsigned char* packet)
{
	DWORD ioBytes = 0;
	sendWsaBuf.len = GetPacketSize(packet);
	int ret = WSASend(clientSocket, &sendWsaBuf, 1, &ioBytes, 0, NULL, NULL);
	if (SOCKET_ERROR == ret)
	{
		::MessageBox(hWnd, "WSASend Error!", "Error!!", MB_OK);
	}
}

void Socket::ProcessPacket(unsigned char* packet)
{
	Framework::GetInstance()->ProcessPacket(packet);
}