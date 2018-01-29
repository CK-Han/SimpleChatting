#include "Socket.h"
#include <winsock2.h>
#include "Framework.h"
#pragma comment (lib, "ws2_32.lib")

Socket::Socket()
	: isInitialized(false)
{
}

Socket::~Socket()
{
}


bool Socket::Initialize(HWND mainWindow, const char* serverIP)
{
	if (serverIP == nullptr 
		|| isInitialized == true)
	{
		//error!
		return false;
	}

	hWnd = mainWindow;
	clientSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	
	SOCKADDR_IN ServerAddr;
	::ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(MY_SERVER_PORT);
	ServerAddr.sin_addr.s_addr = inet_addr(serverIP);

	int Result = WSAConnect(clientSocket, (sockaddr *)&ServerAddr, sizeof(ServerAddr), nullptr, nullptr, nullptr, nullptr);
	if (0 != Result)
	{
		::MessageBox(mainWindow, "WSAConnect Error!!", "Error!!", MB_OK);
		if (clientSocket) 
			closesocket(clientSocket);

		return false;
	}

	WSAAsyncSelect(clientSocket, mainWindow, WM_SOCKET, FD_CLOSE | FD_READ);

	sendWsaBuf.buf = reinterpret_cast<CHAR*>(sendBuf);
	sendWsaBuf.len = MAX_BUFF_SIZE;
	recvWsaBuf.buf = reinterpret_cast<CHAR*>(recvBuf);
	recvWsaBuf.len = MAX_BUFF_SIZE;

	isInitialized = true;
	return true;	
}

void Socket::ReadPacket(SOCKET sock)
{
	DWORD iobyte, ioflag = 0;
	int ret = WSARecv(sock, &recvWsaBuf, 1, &iobyte, &ioflag, nullptr, nullptr);
	if (0 != ret)
	{
		::MessageBox(hWnd, "WSARecv Error!", "Error!!", MB_OK);
		return;
	}

	unsigned char* ptr = reinterpret_cast<unsigned char*>(recvBuf);

	while (0 != iobyte) 
	{
		if (0 == inPacketSize) inPacketSize = GetPacketSize(ptr);
		if (iobyte + savedPacketSize >= inPacketSize) 
		{	
			memcpy(packetBuf + savedPacketSize, ptr, inPacketSize - savedPacketSize);
			ProcessPacket(packetBuf);

			ptr += inPacketSize - savedPacketSize;
			iobyte -= inPacketSize - savedPacketSize;
			inPacketSize = 0;
			savedPacketSize = 0;
		}
		else 
		{
			memcpy(packetBuf + savedPacketSize, ptr, iobyte);
			savedPacketSize += iobyte;
			iobyte = 0;
		}
	}
}


void Socket::SendPacket(unsigned char* packet)
{
	DWORD iobyte;

	sendWsaBuf.len = GetPacketSize(packet);
	int ret = WSASend(clientSocket, &sendWsaBuf, 1, &iobyte, 0, NULL, NULL);
	if (0 != ret)
	{
		::MessageBox(hWnd, "WSASend Error!", "Error!!", MB_OK);
	}
}

void Socket::ProcessPacket(unsigned char* packet)
{
	Framework::GetInstance()->ProcessPacket(packet);
}