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
private:
	static const unsigned int	NUM_WORKER_THREADS = 8;


public:
	static Framework* GetInstance()
	{
		static Framework framework;
		return &framework;
	}

	HANDLE						GetIocpHandle() const { return hIocp; }
	bool						IsShutDown() const { return isShutdown; }
	int							GetNextValidSerial() { return validSerial++; }
	std::map<int, Client>&		GetClients() { return clients; }

	
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
	
	Client*			FindClientFromName(const std::string& clientName);
	Channel*		FindChannelFromName(const std::string& channelName);


private:
	HANDLE				hIocp;
	bool				isShutdown;
	int					validSerial;

	std::vector<std::unique_ptr<std::thread>>		workerThreads;
	std::unique_ptr<std::thread>					acceptThread;

	std::mutex										mLock;
	std::map<int /*serial*/, Client>				clients;

	std::vector<PublicChannel>						publicChannels;
	std::list<CustomChannel>						customChannels;
};

