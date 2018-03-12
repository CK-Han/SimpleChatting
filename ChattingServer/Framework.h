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
	static const unsigned int	NUM_WORKER_THREADS = 8;
	static const unsigned int	MAX_CLIENT_COUNT = 10000;
	static const SERIAL_TYPE	SERIAL_ERROR = -1;

	static Framework& GetInstance()
	{
		static Framework framework;
		return framework;
	}

	bool						IsShutDown() const { return isShutdown; }

	HANDLE						GetIocpHandle() const { return hIocp; }
	const Client&				GetClient(SERIAL_TYPE index) const { return clients[index]; }
	Client&						GetClient(SERIAL_TYPE index) { return clients[index]; }
	
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


//Debug
	int									DebugUserCount();
	std::vector<std::string>			DebugCustomChannels();
//Debug


private:
	Framework();
	~Framework();

	void Initialize();
	void ShutDown();

	SERIAL_TYPE				GetRandomPublicChannelIndex() const;
	void					BroadcastToChannel(const std::string& channelName, unsigned char* packet);

	void					HandleUserLeave(SERIAL_TYPE leaver, bool isKicked, Channel* channel);
	void					ConnectToRandomPublicChannel(SERIAL_TYPE serial);
	bool					ConnectToChannel(SERIAL_TYPE serial, const std::string& channelName);
	
	SERIAL_TYPE				FindClientSerialFromName(const std::string& clientName);
	Channel*				FindChannelFromName(const std::string& channelName);

	void					AddNewCustomChannel(const std::string& channelName);

private:
	HANDLE					hIocp;
	bool					isShutdown;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					acceptThread;

	std::mutex										clientNameLock;
	std::mutex										customChannelsLock;
	
	std::vector<Client>								clients;

	std::unordered_map<std::string, SERIAL_TYPE>	usedClientNames;
	concurrency::concurrent_queue<SERIAL_TYPE>		validClientSerials;

	std::vector<PublicChannel>						publicChannels;
	std::vector<CustomChannel>						customChannels;
};

