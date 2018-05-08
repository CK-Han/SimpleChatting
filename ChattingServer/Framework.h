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
	@brief		ä�� ���� ���� �����ӿ�ũ, �̱��� Ŭ����
	@details	iocp�� �����Ѵ�. overlap ����ü�� Ȯ���Ͽ� ����ϸ�(Overlap_Exp), key�� Ŭ���̾�Ʈ �ø����̴�.
				
				����ϴ� ���� ������ ����.
			1. clientNameLock - ������ �̸��� �����ϰ� �����ϱ� ���� mutex�̴�. usedClientNames�� ���ȴ�.
			2. customChannelLock - Ŀ����ä���� �����ϰ� ����(�̸�)�ϱ� ���� mutex�̴�. usedCustomChannelNames�� ���ȴ�.
								����ä���� �Һ����� mutex�� ���� �ʴ´�.
			3. Client::clientLock - Ŭ���̾�Ʈ�� ������������ ����ȭ�� �ʿ��� ��쿡 ���Ǵ� mutex�̴�.
			4. Channel::channelLock - ä���� Enter�� Exit�� �ڷᱸ���� ����ȭ�ϱ� ���� ���ȴ�.
			
				�����ϴ� ������� ������ ����.
			1. ���� ������ - main()���� �ܼ��� ���� ���� ���α׷����� ä�� ���� Ȯ�� �� �����û�� ó���Ѵ�.
			2. ���� ������ - accept�� ó���Ѵ�. GetSerialForNewClient()���� clientNameLock�� ���� ����ȭ�ȴ�.
			3. ��Ŀ ������ - NUM_WORKER_THREADS ��ŭ �����Ǿ� ��� �۾��� �����Ѵ�.

				�����ӿ�ũ ������ �帧��
			Initialize �ܰ迡�� �̸� Ŭ���̾�Ʈ, ���� �� Ŀ����ä��, ���ۿ� Overlap�� �����ϰ�
			�̸� thread_safe�ϰ� ����ϱ� ���� �̵��� ��밡���� �ε����� concurrent_queue�� ��� ����Ѵ�.
			��Ŷ ó���Լ��� ���ν��� map�� ����Ͽ� �̸� ȣ���ϵ��� �Ѵ�.
	
			concurrent_queue�� ���۽��д� MAKECUSTOM_TIMEOUT_MILLISECONDS�� ���� Ÿ�Ӿƿ����� Ȯ���Ѵ�.
			�ֿܼ��� ���� �����û�� �����ϱ� ���� ���� ���� GQCS_TIMEOUT_MILLISECONDS�� ���� ����� ����Ѵ�.
			
				���α׷��� ��ǥ������ ������ ���� ����� ��û�� ó���Ѵ�.
			����, �α���, ä�� + �ӼӸ�, ������ ä�� ����Ʈ, ä�� ����, ����, ����

	@warning	Send�� ��Ȳ����, MAX_OVERLAPEXP_COUNT�� �ʰ��ϴ� ��û�� �߻��� �� ����
					ex. ���ÿ� �ټ� �������� -> ä�� �� �ο��� 200�� ���, �̷��� ä�δ� ��Ŷ�� 200 + 199 ... + 1 = �� 2���� �߻�
				��� ����� �ִ� ������ ����Ͽ� ������ �������� �� ���̴�.
	
	@todo		�޸� Ǯ ���� ���, �㳪 warning�� ���� ���ܴ� ������ �߻��� �� ������ �������� �޸� Ǯ�� �����ϴ���
				�׷� ���� ������带 �����ؾ� �ϸ� �ʹ� ���� �޸𸮸� �����ϰ� �� ���� �ִ�.
				���� �޸� Ǯ ���߰� �Բ� ������ ���ϸ� ������ �� �ִ� ���� �������� ���� ����ؾ� �Ѵ�.
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
	static const unsigned int	MAX_OVERLAPEXP_COUNT = 600000; //Overlap_Exp �� 4kb, �� �� 2.4GB
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

