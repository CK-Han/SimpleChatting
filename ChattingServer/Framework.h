#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <concurrent_queue.h>
#include <unordered_map>
#include <string>
#include "../Common/Protocol.h"
#include "Client.h"
#include "Channel.h"

/**
	@class Framework
	@brief		채팅 서버 메인 프레임워크, 싱글톤 클래스
	@details	iocp로 동작한다. overlap 구조체를 확장하여 사용하며(Overlap_Exp), key는 클라이언트 시리얼이다.
				
				사용하는 락은 다음과 같다.
			1. clientNameLock - 유저의 이름을 유일하게 관리하기 위한 mutex이다. usedClientNames에 사용된다.
			2. customChannelLock - 커스텀채널을 유일하게 관리(이름)하기 위한 mutex이다. usedCustomChannelNames에 사용된다.
								공개채널은 불변으로 mutex를 두지 않는다.
			3. Client::clientLock - 클라이언트의 정보수정에서 동기화가 필요한 경우에 사용되는 mutex이다.
			4. Channel::channelLock - 채널의 Enter와 Exit시 자료구조를 동기화하기 위해 사용된다.
			
				동작하는 스레드는 다음과 같다.
			1. 메인 스레드 - main()에서 콘솔을 통해 서버 프로그래머의 채널 상태 확인 및 종료요청을 처리한다.
			2. 수신 스레드 - accept를 처리한다. GetSerialForNewClient()에서 clientNameLock을 통해 동기화된다.
			3. 워커 스레드 - NUM_WORKER_THREADS 만큼 생성되어 통신 작업을 진행한다.

				프레임워크 진행의 흐름은
			Initialize 단계에서 미리 클라이언트, 공개 및 커스텀채널, 전송용 Overlap을 생성하고
			이를 thread_safe하게 사용하기 위해 이들의 사용가능한 인덱스를 concurrent_queue에 담아 사용한다.
			패킷 처리함수는 프로시저 map에 등록하여 이를 호출하도록 한다.
	
			concurrent_queue의 동작실패는 MAKECUSTOM_TIMEOUT_MILLISECONDS과 같은 타임아웃으로 확인한다.
			콘솔에서 서버 종료요청에 응답하기 위해 위와 같이 GQCS_TIMEOUT_MILLISECONDS과 같은 상수를 사용한다.
			
				프로그램은 대표적으로 다음과 같은 사용자 요청을 처리한다.
			접속, 로그인, 채팅 + 귓속말, 서버의 채널 리스트, 채널 변경, 강퇴, 종료

	@warning	Send의 상황에서, MAX_OVERLAPEXP_COUNT를 초과하는 요청이 발생할 수 있음
					ex. 동시에 다수 접속종료 -> 채널 내 인원이 200인 경우, 이러한 채널당 패킷이 200 + 199 ... + 1 = 약 2만개 발생
				고로 상수는 최대 동접을 고려하여 적절히 정해져야 할 것이다.
	
	@todo		메모리 풀 개발 고려, 허나 warning에 적은 예외는 언제나 발생할 수 있으며 가변적인 메모리 풀을 적용하더라도
				그로 인한 오버헤드를 생각해야 하며 너무 많은 메모리를 차지하게 될 수도 있다.
				따라서 메모리 풀 개발과 함께 적절히 부하를 관리할 수 있는 동시 접속자의 수를 고려해야 한다.
*/
class Framework
{
public:
	using SERIAL_TYPE = decltype(Client::Serial);
	static const SERIAL_TYPE	SERIAL_ERROR = (std::numeric_limits<SERIAL_TYPE>::max)();
	static const unsigned int	GQCS_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	ACCEPT_TIMEOUT_SECONDS = 3;

	enum class SERIAL_GET {
		CLIENT = 0
		, CUSTOMCHANNEL
		, OVERLAPEXP
	};

private:
	using Packet_Procedure = void(Framework::*)(SERIAL_TYPE, StreamReader&);
	using Serial_ConcurrentQueue = concurrency::concurrent_queue<SERIAL_TYPE>;

	static const unsigned int	NEWBIESERIAL_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	MAKECUSTOM_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	GETOVERLAP_TIMEOUT_MILLISECONDS = 3000;

	static const unsigned int	NUM_WORKER_THREADS = 8;
	static const unsigned int	MAX_CLIENT_COUNT = 10000;
	static const unsigned int	MAX_CUSTOM_COUNT = 10000;
	static const unsigned int	MAX_OVERLAPEXP_COUNT = 600000; //Overlap_Exp 약 4kb, 총 약 2.4GB
	static const unsigned int	PUBLIC_BUSY_COUNT = 2;
	static const unsigned int	MAX_CHANNEL_USERS = 200;

	enum class CHANNEL_CONNECT {
		SUCCESS = 0
		, FAIL_ARGUMENT
		, FAIL_INVALID_CHANNEL
		, FAIL_FULL
	};


public:
	static Framework& GetInstance()
	{
		static Framework framework;
		return framework;
	}

	bool			IsShutDown() const { return isShutDown; }
	bool			IsValidClientSerial(SERIAL_TYPE serial) const { return (serial < MAX_CLIENT_COUNT) ? true : false; }

	HANDLE			GetIocpHandle() const { return hIocp; }
	Client&			GetClient(SERIAL_TYPE serial) { return *clients[serial]; }
	SERIAL_TYPE		GetSerialForNewOne(SERIAL_GET type);

	void			ReturnUsedOverlapExp(SERIAL_TYPE serial);

	void			SendPacket(SERIAL_TYPE serial, const void* packet);
	void			SendSystemMessage(SERIAL_TYPE serial, const std::string& msg);
	
	void			ProcessUserClose(SERIAL_TYPE serial);	//disconnect
	void			ProcessPacket(SERIAL_TYPE serial, unsigned char* packet, int size);
	
	//Debug
	std::vector<std::string>	DebugCustomChannels(bool doLock);
	size_t						DebugUserCount(bool doLock);

private:
	Framework();
	~Framework();

	bool Initialize();
	void ShutDown();

	void Process_Login(SERIAL_TYPE serial, StreamReader&);
	void Process_ChannelList(SERIAL_TYPE serial, StreamReader&);
	void Process_Chatting(SERIAL_TYPE serial, StreamReader&);
	void Process_Kick(SERIAL_TYPE serial, StreamReader&);
	void Process_ChannelChange(SERIAL_TYPE serial, StreamReader&);

	SERIAL_TYPE				GetRandomPublicChannelSerial() const;

	void					BroadcastToChannel(const std::shared_ptr<Channel>& channel, const void* packet);

	void					HandleUserLeave(SERIAL_TYPE leaver, bool isKicked, const std::shared_ptr<Channel>& channel);
	void					ConnectToRandomPublicChannel(SERIAL_TYPE serial);
	CHANNEL_CONNECT			ConnectToChannel(SERIAL_TYPE serial, const std::string& channelName);
	
	SERIAL_TYPE						FindClientSerialFromName(const std::string& clientName);
	std::shared_ptr<Channel>		FindChannelFromName(const std::string& channelName);

	void							AddNewCustomChannel(const std::string& channelName);
	
private:
	HANDLE					hIocp;
	bool					isShutDown;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					acceptThread;

	std::mutex										clientNameLock;
	std::mutex										customChannelsLock;
	
	std::vector<std::unique_ptr<Client>>			clients;
	std::vector<std::unique_ptr<Overlap_Exp>>		sendOverlapExps;

	std::unordered_map<std::string, SERIAL_TYPE>	usedClientNames;
	std::unordered_map<std::string, SERIAL_TYPE>	usedCustomChannelNames;

	Serial_ConcurrentQueue							validClientSerials;
	Serial_ConcurrentQueue							validCustomChannelSerials;
	Serial_ConcurrentQueue							validOverlapExpSerials;

	std::vector<std::shared_ptr<PublicChannel>>		publicChannels;
	std::vector<std::shared_ptr<CustomChannel>>		customChannels;
	
	std::map<Packet_Base::ValueType /*type*/, Packet_Procedure>		packetProcedures;
};

