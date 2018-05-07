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
	@brief		���� ����, ���� connect
	@details	overlapped socket���� �����Ѵ�.
	@return		���� ���� �� connect ������ true

	@warning	ip�� �� �������̹Ƿ� ��뿡 ���� (invalid�� �� connect ������ ���̴�.)
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
