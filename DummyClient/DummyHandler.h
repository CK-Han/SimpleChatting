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


/**
	@struct Overlap_Exp
	@brief		overlapped 구조체 확장, overlap 이벤트와 버퍼를 갖는다.
	@details	Serial의 최대값(numeric_limits)을 오류 값으로 사용한다.
*/
struct Overlap_Exp
{
	enum Overlap_Operation
	{
		OPERATION_RECV = 1
		, OPERATION_SEND
		, OPERATION_RANDPACKET
	};

	::WSAOVERLAPPED Original_Overlap;
	int Operation;
	size_t Serial;
	WSABUF WsaBuf;
	unsigned char Iocp_Buffer[Packet_Base::MAX_BUF_SIZE];

	Overlap_Exp()
		: Operation(0)
		, Serial((std::numeric_limits<size_t>::max)())
	{
		::ZeroMemory(&Original_Overlap, sizeof(Original_Overlap));
		::ZeroMemory(Iocp_Buffer, sizeof(Iocp_Buffer));

		WsaBuf.buf = reinterpret_cast<CHAR*>(Iocp_Buffer);
		WsaBuf.len = sizeof(Iocp_Buffer);
	}
};


/**
	@struct	Overlap_Info
	@brief		Overlap_Exp과 패킷 조립을 위한 정보를 담은 구조체
	@details	PacketSize는 다음에 조립할 패킷 크기이며
				SavedPacketSize 패킷버퍼에 총 저장된 크기이다.
				즉, PacketSize <= SavedPacketSize 이면 패킷 조립이 가능하다.
*/
struct Overlap_Info
{
	Overlap_Exp				RecvOverlapExp;

	unsigned int			PacketSize;
	unsigned int			SavedPacketSize;
	unsigned char			PacketBuff[Packet_Base::MAX_BUF_SIZE];

	Overlap_Info()
		: PacketSize(0)
		, SavedPacketSize(0)
	{
		::ZeroMemory(PacketBuff, sizeof(PacketBuff));
	}
};


/**
	@struct Event_Info
	@brief		더미의 패킷발송을 위한 타이머 큐의 원소
	@details	Wakeup_Time 만큼 대기 후 priority_queue의 pop()이 진행되는 방식이며
				Wakeup_Time의 단위는 millisecond이다.
*/
struct Event_Info
{
	int			 Event_Type;
	size_t		 Serial;
	unsigned int Wakeup_Time;	//ms
};

/**
	@class Event_Compare
	@brief		priority_queue의 비교 클래스 정의
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
	@brief		더미를 관리하며 서버와 통신하는 메인 프레임워크이다. 싱글턴
	@details	iocp로 동작한다. dummy에 수정을 가하는 경우 더미 각자의 dummyLock으로 동기화한다. 
				타이머를 통해 주기적으로 임의의 요청을 서버에 전달하며 timerLock으로 동기화한다.

				스레드는 iocp 통신하는 워커 스레드와, 타이머 스레드, 그리고 메인 스레드 총 3종류이다.
				더미 개수는 유동적으로 관리할 수 있다. 콘솔로 입력하여 추가 혹은 가장 앞선 더미부터 close한다.

				프로그램의 흐름은, 처음 START_DUMMY_COUNT 만큼 더미 생성하여 연결하고, 랜덤패킷 전송을 계속 진행한다.
				전송용 Overlap 확장구조체를 미리 생성하고, concurrent queue에서 인덱스를 얻어 사용하고, 반납한다.
				Send가 비정상적으로 많아지는 경우는 없지만, 넉넉하게 할당한다.(MAX_OVERLAPEXP_COUNT)
				임의 확률을 결정하는 계수는 SendRandomPacket()과 GetRandom...() 함수들 내에 정의되어있다.

				recv 처리는 로그인, 채널변경, 강퇴만이 처리된다.
*/
class DummyHandler
{
public:
	using SERIAL_TYPE = size_t;
	using Timer_Queue = std::priority_queue < Event_Info, std::vector<Event_Info>, Event_Compare>;
	using Packet_Procedure = void(DummyHandler::*)(SERIAL_TYPE, StreamReader&);

	static const unsigned int NUM_WORKER_THREADS = 8;
	static const unsigned int START_DUMMY_COUNT = 2000;
	static const unsigned int MAX_DUMMY_COUNT = 10000;
	static const unsigned int MAX_OVERLAPEXP_COUNT = 40000;
	static const unsigned int PACKET_DELAY_TIME = 2000;
	static const SERIAL_TYPE  SERIAL_ERROR = (std::numeric_limits<SERIAL_TYPE>::max)();

	static const unsigned int GETOVERLAP_TIMEOUT_MILLISECONDS = 2000;
	static const unsigned int GQCS_TIMEOUT_MILLISECONDS = 2000;
	
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

