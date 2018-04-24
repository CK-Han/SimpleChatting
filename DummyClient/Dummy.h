#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <mutex>
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

	SOCKET			GetSocket() const { return clientSocket; }
	std::mutex&		GetLock() { return dummyLock; }

private:
	SOCKET					clientSocket;
	bool					isLogin;
	std::mutex				dummyLock;

	std::string				userName;
	std::string				userChannel;
};

