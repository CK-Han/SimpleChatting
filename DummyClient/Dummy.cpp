#include "Dummy.h"
#pragma comment (lib, "ws2_32.lib")


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
	clientSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN serverAddr;
	::ZeroMemory(&serverAddr, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(MY_SERVER_PORT);
	serverAddr.sin_addr.s_addr = inet_addr(serverIP);

	int result = WSAConnect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr), nullptr, nullptr, nullptr, nullptr);
	if (SOCKET_ERROR == result)
		return false;
	
	return true;
}

void Dummy::Close()
{
	isLogin = false;
	::shutdown(clientSocket, SD_BOTH);
}
