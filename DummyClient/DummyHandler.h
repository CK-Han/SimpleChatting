#pragma once

#include "Dummy.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <random>


enum Overlap_Operation
{
	OPERATION_RECV = 1
	, OPERATION_SEND
	, OPERATION_RANDPACKET
};

struct Overlap_Exp
{
	::WSAOVERLAPPED Original_Overlap;
	int Operation;
	WSABUF WsaBuf;
	unsigned char Iocp_Buffer[MAX_BUFF_SIZE];
};

struct Overlap_Info
{
	Overlap_Exp				recvOverlapExp;

	int						PacketSize;
	int						PreviousSize;
	unsigned char			PacketBuff[MAX_BUFF_SIZE];
};

struct Event_Info
{
	int Event_Type;
	int Serial;
	unsigned int Wakeup_Time;
};


class Event_Compare
{
public:
	bool operator()(const Event_Info& lhs, const Event_Info& rhs) const
	{
		return lhs.Wakeup_Time > rhs.Wakeup_Time;
	}
};

class DummyHandler
{
public:
	static constexpr unsigned int NUM_WORKER_THREADS	 = 8;
	static constexpr unsigned int START_DUMMY_COUNT		 = 1000;
	static constexpr unsigned int MAX_DUMMY_COUNT		 = 10000;
	static constexpr unsigned int PACKET_DELAY_TIME		 = 2000;

private:
	static std::default_random_engine RANDOM_ENGINE;

public:
	static DummyHandler* GetInstance()
	{
		static DummyHandler hanlder;
		return &hanlder;
	}

	bool Start(const std::string& ip);
	void Close();

	bool AddDummy(int beginSerial, int count, const std::string& ip);
	bool CloseDummy(int count);

	void SendPacket(int serial, unsigned char* packet);
	void ProcessPacket(int serial, unsigned char* packet);

	void RequestRandomPacket(int serial);


	HANDLE						GetIocpHandle() const { return hIocp; }
	std::mutex&					GetTimerLock() { return timerLock; }
	int							GetValidSerial() const { return lastSerial; }
	bool						IsShutdown() const { return isShutdown; }
	

	std::vector<std::pair<Dummy, Overlap_Info>>& 
		GetDummies() { return dummies; }
	std::priority_queue < Event_Info, std::vector<Event_Info>, Event_Compare>&
		GetTimerQueue() { return timerQueue; }


private:
	DummyHandler();
	~DummyHandler();

	void			ProcessLogin(int serial, unsigned char* packet);
	void			ProcessChannelEnter(int serial, unsigned char* packet);

	void			AddRandomPacketEvent(int serial);
	
	void			RequestWhisper(int serial);
	void			RequestChannelList(int serial);
	void			RequestChannelChange(int serial);
	void			RequestKick(int serial);
	void			RequestChatting(int serial);

	std::string		GetRandomUser() const;
	std::string		GetRandomChannel() const;

private:
	HANDLE				hIocp;
	bool				isInitialized;
	bool				isShutdown;
	int					lastSerial;
	int					lastCloseSerial;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					timerThread;
	std::mutex										timerLock;

	std::vector<std::pair<Dummy, Overlap_Info>>		dummies;

	std::priority_queue < Event_Info, std::vector<Event_Info>, Event_Compare> timerQueue;
};

