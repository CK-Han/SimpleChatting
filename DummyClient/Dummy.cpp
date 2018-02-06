#include "Dummy.h"
#pragma comment (lib, "ws2_32.lib")
#include <iostream>

Dummy::Dummy()
	: clientSocket(INVALID_SOCKET)
	, isLogin(false)
{
}

Dummy::~Dummy()
{
}

////////////////////////////////////////////////////////////////////////////

bool Dummy::Connect(const char* serverIP)
{
	clientSocket = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN ServerAddr;
	::ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(MY_SERVER_PORT);
	ServerAddr.sin_addr.s_addr = inet_addr(serverIP);

	int Result = WSAConnect(clientSocket, (sockaddr *)&ServerAddr, sizeof(ServerAddr), nullptr, nullptr, nullptr, nullptr);
	if (0 != Result)
	{
		if (clientSocket)
			::closesocket(clientSocket);
		return false;
	}
	
	return true;
}

void Dummy::Close()
{
	isLogin = false;
	::shutdown(clientSocket, SD_BOTH);
}
