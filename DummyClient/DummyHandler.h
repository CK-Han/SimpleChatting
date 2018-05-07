#pragma once

#include "Dummy.h"
#include "../Common/protocol.h"
#include "../Common/stream.h"

#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <queue>
#include <map>
#include <random>
#include <concurrent_queue.h>


enum Overlap_Operation
{
	OPERATION_RECV = 1
	, OPERATION_SEND
	, OPERATION_RANDPACKET
};

/**
	@struct Overlap_Exp
	@brief		overlapped 구조체 확장, overlap 이벤트와 버퍼를 갖는다.
*/
struct Overlap_Exp
{
	::WSAOVERLAPPED Original_Overlap;
	int Operation;
	size_t Serial;
	WSABUF WsaBuf;
	unsigned char Iocp_Buffer[Packet_Base::MAX_BUF_SIZE];
};


/**
	@struct	Overlap_Info
	@brief		Overlap_Exp를 통해 패킷을 조립
*/
struct Overlap_Info
{
	Overlap_Exp				RecvOverlapExp;

	unsigned int			PacketSize;
	unsigned int			PreviousCursor;
	unsigned char			PacketBuff[Packet_Base::MAX_BUF_SIZE];
};


/**
	@struct Event_Info
	@brief		더미의 패킷발송을 위한 타이머 큐의 원소
*/
struct Event_Info
{
	int Event_Type;
	int Serial;
	unsigned int Wakeup_Time;
};

/**
	@class Event_Compare
	@brief		priority_queue의 비교용 callable class
*/
class Event_Compare
{
public:
	bool operator()(const Event_Info& lhs, const Event_Info& rhs) const
	{
		return lhs.Wakeup_Time > rhs.Wakeup_Time;
	}
};


/**
	@class DummyHandler
	@brief		더미를 관리하는 클래스, 싱글턴
	@details	iocp로 동작, 타이머를 통해 주기적으로 임의의 요청 서버에 전달, 수신시 필요한 처리만 진행
*/
class DummyHandler
{
public:
	using SERIAL_TYPE = size_t;
	using Timer_Queue = std::priority_queue < Event_Info, std::vector<Event_Info>, Event_Compare>;
	using Packet_Procedure = void(DummyHandler::*)(SERIAL_TYPE, StreamReader&);

	static const unsigned int NUM_WORKER_THREADS = 8;
	static const unsigned int START_DUMMY_COUNT = 1000;
	static const unsigned int MAX_DUMMY_COUNT = 10000;
	static const unsigned int MAX_OVERLAPEXP_COUNT = 40000;
	static const unsigned int PACKET_DELAY_TIME = 2000;
	static const SERIAL_TYPE  SERIAL_ERROR = (std::numeric_limits<SERIAL_TYPE>::max)();
	static const unsigned int GETOVERLAP_TIMEOUT_MILLISECONDS = 2000;
	
private:
	static thread_local std::default_random_engine RANDOM_ENGINE;
	
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

	void SendPacket(SERIAL_TYPE serial, const void* packet);
	void ProcessPacket(SERIAL_TYPE serial, const void* packet, int size);

	void SendRandomPacket(SERIAL_TYPE serial);


	HANDLE			GetIocpHandle() const { return hIocp; }
	std::mutex&		GetTimerLock() { return timerLock; }
	bool			IsShutdown() const { return isShutdown; }
	bool			IsValidSerial(SERIAL_TYPE serial) const { return (lastToCloseSerial <= serial && serial < lastSerial) ? true : false; }
	
	std::vector<std::pair<Dummy, Overlap_Info>>&	GetDummies() { return dummies; }
	Timer_Queue&									GetTimerQueue() { return timerQueue; }
	Overlap_Exp&									GetSendOverlapExp(SERIAL_TYPE serial) { return *sendOverlapExps[serial]; }

	SERIAL_TYPE		GetSerialForUseOverlapExp();
	void			ReturnUsedOverlapExp(SERIAL_TYPE serial);

private:
	DummyHandler();
	~DummyHandler();

	void			Process_Login(SERIAL_TYPE serial, StreamReader&);
	void			Process_ChannelEnter(SERIAL_TYPE serial, StreamReader&);
	void			Process_UserLeave(SERIAL_TYPE serial, StreamReader&);

	void			AddRandomPacketEvent(SERIAL_TYPE serial);
	
	void			RequestWhisper(SERIAL_TYPE serial);
	void			RequestChannelList(SERIAL_TYPE serial);
	void			RequestChannelChange(SERIAL_TYPE serial);
	void			RequestKick(SERIAL_TYPE serial);
	void			RequestChatting(SERIAL_TYPE serial);

	std::string		GetRandomUser() const;
	std::string		GetRandomChannel() const;

private:
	HANDLE				hIocp;
	bool				isInitialized;
	bool				isShutdown;
	SERIAL_TYPE			lastSerial;
	SERIAL_TYPE			lastToCloseSerial;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					timerThread;
	std::mutex										timerLock;

	std::vector<std::pair<Dummy, Overlap_Info>>		dummies;
	std::vector<std::string>						publicChannels;
	
	Timer_Queue										timerQueue;

	std::map<Packet_Base::ValueType /*type*/, Packet_Procedure>		packetProcedures;

	std::vector<std::unique_ptr<Overlap_Exp>>		sendOverlapExps;
	concurrency::concurrent_queue<SERIAL_TYPE>		validOverlapExpSerials;
};

