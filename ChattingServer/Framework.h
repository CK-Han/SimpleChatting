#pragma once
#include <WinSock2.h>
#include <Windows.h>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include "../Common/Protocol.h"
#include "Client.h"
#include "Channel.h"



class Framework
{
public:
	static const unsigned int	NUM_WORKER_THREADS = 8;
	static const unsigned int	MAX_CLIENT_COUNT = 10000;
	static const int			SERIAL_ERROR = -1;

	static Framework* GetInstance()
	{
		static Framework framework;
		return &framework;
	}

	HANDLE						GetIocpHandle() const { return hIocp; }
	bool						IsShutDown() const { return isShutdown; }
	const Client&				GetClient(int index) const { return clients[index]; }
	Client&						GetClient(int index) { return clients[index]; }

	int							GetSeirialForNewClient();

	void		SendPacket(int serial, unsigned char* packet) const;
	void		SendSystemMessage(int serial, const std::string& msg) const;
	
	void		ProcessPacket(int serial, unsigned char* packet);
	void		ProcessUserClose(int serial);
	void		ProcessLogin(int serial, unsigned char* packet);
	void		ProcessChannelList(int serial);
	void		ProcessChatting(int serial, unsigned char* packet);
	void		ProcessKick(int serial, unsigned char* packet);
	void		ProcessChannelChange(int serial, unsigned char* packet);

private:
	Framework();
	~Framework();

	void Initialize();

	int				GetRandomPublicChannelIndex() const;
	void			BroadcastToChannel(const std::string& channelName, unsigned char* packet);

	void			HandleUserLeave(int leaver, bool isKicked, Channel* channel);
	void			ConnectToRandomPublicChannel(int serial);
	bool			ConnectToChannel(int serial, const std::string& channelName);
	
	int				FindClientSerialFromName(const std::string& clientName);
	Channel*		FindChannelFromName(const std::string& channelName);

	void			AddNewCustomChannel(const std::string& channelName);

private:
	HANDLE				hIocp;
	bool				isShutdown;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					acceptThread;

	std::mutex										mLock;
	std::mutex										loginLock;
	std::mutex										clientNameLock;
	std::mutex										customChannelsLock;
	std::vector<Client>								clients;

	std::vector<PublicChannel>						publicChannels;
	std::list<CustomChannel>						customChannels;
};

