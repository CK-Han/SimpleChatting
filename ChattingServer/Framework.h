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

	static const unsigned int	GQCS_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	ACCEPT_TIMEOUT_SECONDS = 3;
	static const unsigned int	LOGIN_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	MAKECUSTOM_TIMEOUT_MILLISECONDS = 3000;
	static const unsigned int	NUM_WORKER_THREADS = 8;
	static const unsigned int	MAX_CLIENT_COUNT = 10000;
	static const unsigned int	MAX_CUSTOM_COUNT = 10000;
	static const unsigned int	PUBLIC_BUSY_COUNT = 3;
	static const SERIAL_TYPE	SERIAL_ERROR = -1;
	

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

	bool						IsShutDown() const { return isShutDown; }
	bool						IsValidClientSerial(SERIAL_TYPE serial) const { return (0 <= serial && serial < MAX_CLIENT_COUNT) ? true : false; }

	HANDLE						GetIocpHandle() const { return hIocp; }
	Client&						GetClient(SERIAL_TYPE serial) { return *clients[serial]; }
	SERIAL_TYPE					GetSerialForNewClient();

	void		SendPacket(SERIAL_TYPE serial, unsigned char* packet) const;
	void		SendSystemMessage(SERIAL_TYPE serial, const std::string& msg) const;
	
	void		ProcessPacket(SERIAL_TYPE serial, unsigned char* packet);
	void		ProcessUserClose(SERIAL_TYPE serial);
	void		ProcessLogin(SERIAL_TYPE serial, unsigned char* packet);
	void		ProcessChannelList(SERIAL_TYPE serial);
	void		ProcessChatting(SERIAL_TYPE serial, unsigned char* packet);
	void		ProcessKick(SERIAL_TYPE serial, unsigned char* packet);
	void		ProcessChannelChange(SERIAL_TYPE serial, unsigned char* packet);


private:
	Framework();
	~Framework();

	void Initialize();
	void ShutDown();

	SERIAL_TYPE				GetRandomPublicChannelSerial() const;
	SERIAL_TYPE				GetSerialForNewCustomChannel();
	void					BroadcastToChannel(std::shared_ptr<Channel>& channel, unsigned char* packet) const;

	void					HandleUserLeave(SERIAL_TYPE leaver, bool isKicked, std::shared_ptr<Channel>& channel);
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
	
	std::unordered_map<std::string, SERIAL_TYPE>	usedClientNames;
	std::unordered_map<std::string, SERIAL_TYPE>	usedCustomChannelNames;

	concurrency::concurrent_queue<SERIAL_TYPE>		validClientSerials;
	concurrency::concurrent_queue<SERIAL_TYPE>		validCustomChannelSerials;

	std::vector<std::shared_ptr<PublicChannel>>		publicChannels;
	std::vector<std::shared_ptr<CustomChannel>>		customChannels;
};

