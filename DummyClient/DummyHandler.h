#pragma once

#include "Dummy.h"
#include "../Common/protocol.h"
#include "../Common/stream.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <map>
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
	unsigned char Iocp_Buffer[Packet_Base::MAX_BUF_SIZE];
};

struct Overlap_Info
{
	Overlap_Exp				recvOverlapExp;

	unsigned int			PacketSize;
	unsigned int			PreviousCursor;
	unsigned char			PacketBuff[Packet_Base::MAX_BUF_SIZE];
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
private:
	static constexpr unsigned int NUM_WORKER_THREADS	 = 8;
	static constexpr unsigned int START_DUMMY_COUNT		 = 1000;
	static constexpr unsigned int MAX_DUMMY_COUNT		 = 10000;
	static constexpr unsigned int PACKET_DELAY_TIME		 = 2000;

	static thread_local std::default_random_engine RANDOM_ENGINE;
	using Timer_Queue = std::priority_queue < Event_Info, std::vector<Event_Info>, Event_Compare>;
	using Packet_Procedure = void(DummyHandler::*)(int /*serial*/, StreamReader&);

public:
	static DummyHandler* GetInstance()
	{
		static DummyHandler hanlder;
		return &hanlder;
	}

	bool Start(const std::string& ip);
	void Close();

	bool AddDummy(unsigned int count, const std::string& ip);
	bool CloseDummy(unsigned int count);

	void SendPacket(int serial, const void* packet) const;
	void ProcessPacket(int serial, const void* packet, int size);

	void SendRandomPacket(int serial);


	HANDLE						GetIocpHandle() const { return hIocp; }
	std::mutex&					GetTimerLock() { return timerLock; }
	int							GetValidSerial() const { return lastSerial; }
	bool						IsShutdown() const { return isShutdown; }
	bool						IsValidSerial(int serial) const;
	
	std::vector<std::pair<Dummy, Overlap_Info>>&	GetDummies() { return dummies; }
	Timer_Queue&									GetTimerQueue() { return timerQueue; }


private:
	DummyHandler();
	~DummyHandler();

	void			Process_Login(int serial, StreamReader&);
	void			Process_ChannelEnter(int serial, StreamReader&);

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
	int					lastToCloseSerial;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					timerThread;
	std::mutex										timerLock;

	std::vector<std::pair<Dummy, Overlap_Info>>		dummies;
	std::vector<std::string>						publicChannels;

	Timer_Queue										timerQueue;

	std::map<Packet_Base::ValueType /*type*/, Packet_Procedure>		packetProcedures;
};

