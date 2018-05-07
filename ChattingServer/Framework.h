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


class Framework
{
public:
	using SERIAL_TYPE = decltype(Client::Serial);
	using Packet_Procedure = void(Framework::*)(SERIAL_TYPE, StreamReader&);

	static const unsigned int	GQCS_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	ACCEPT_TIMEOUT_SECONDS = 3;
	static const unsigned int	LOGIN_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	MAKECUSTOM_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	GETOVERLAP_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	NUM_WORKER_THREADS = 8;
	static const unsigned int	MAX_CLIENT_COUNT = 10000;
	static const unsigned int	MAX_CUSTOM_COUNT = 10000;
	static const unsigned int	MAX_OVERLAPEXP_COUNT = 600000; //Overlap_Exp ¾à 4kb, ÃÑ ¾à 2.4GB
	static const unsigned int	PUBLIC_BUSY_COUNT = 2;
	static const unsigned int	MAX_CHANNEL_USERS = 200;
	static const SERIAL_TYPE	SERIAL_ERROR = (std::numeric_limits<SERIAL_TYPE>::max)();

private:
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
	SERIAL_TYPE		GetSerialForNewClient();

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
	SERIAL_TYPE				GetSerialForNewCustomChannel();
	SERIAL_TYPE				GetSerialForUseOverlapExp();

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

	concurrency::concurrent_queue<SERIAL_TYPE>		validClientSerials;
	concurrency::concurrent_queue<SERIAL_TYPE>		validCustomChannelSerials;
	concurrency::concurrent_queue<SERIAL_TYPE>		validOverlapExpSerials;

	std::vector<std::shared_ptr<PublicChannel>>		publicChannels;
	std::vector<std::shared_ptr<CustomChannel>>		customChannels;
	
	std::map<Packet_Base::ValueType /*type*/, Packet_Procedure>		packetProcedures;
};

