#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>
#include "../Common/Protocol.h"

class DummyHandler;

class Dummy
{
	friend class DummyHandler;

public:
	Dummy();
	~Dummy();

	bool Connect(const char* serverIP);
	void Close();

	SOCKET GetSocket() { return clientSocket; };

private:
	SOCKET					clientSocket;
	bool					isLogin;

	std::string				userName;
	std::string				userChannel;
	std::string				whisperUser;
};

