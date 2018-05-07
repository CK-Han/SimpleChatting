#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <mutex>
#include "../Common/Protocol.h"

class DummyHandler;


/**
	@class Dummy
	@brief		스트레스 테스트에 사용되는 더미 유저
	@details	서버와 통신을 위한 최소한의 정보와, 동기화를 위한 mutex를 갖는다.
*/
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

