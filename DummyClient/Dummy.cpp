#include "Dummy.h"


Dummy::Dummy()
	: clientSocket(INVALID_SOCKET)
	, isLogin(false)
{
}

Dummy::~Dummy()
{
}

////////////////////////////////////////////////////////////////////////////

/**
	@brief		소켓 생성, 서버 connect
	@details	overlapped socket으로 생성한다.
	@return		소켓 생성 및 connect 성공시 true

	@warning	ip가 날 포인터이므로 사용에 주의 (invalid할 시 connect 에러일 것이다.)
*/
bool Dummy::Connect(const char* serverIP)
{
	clientSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (clientSocket == INVALID_SOCKET)
		return false;

	SOCKADDR_IN serverAddr;
	::ZeroMemory(&serverAddr, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(Packet_Base::PORT_NUMBER);
	serverAddr.sin_addr.s_addr = inet_addr(serverIP);

	int result = WSAConnect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr), nullptr, nullptr, nullptr, nullptr);
	if (SOCKET_ERROR == result)
		return false;
	
	return true;
}

void Dummy::Close()
{
	isLogin = false;
	closesocket(clientSocket);
}
