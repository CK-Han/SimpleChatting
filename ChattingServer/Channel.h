#pragma once
#include <string>
#include <list>
#include <mutex>


//test 
#include <iostream>

struct Client;

class Channel
{
public:
	Channel(const std::string& name);
	Channel(const Channel& other);
	virtual ~Channel();
	
	

	virtual void	Enter(Client* client);
	virtual void	Exit(Client* client);
	
	unsigned int					GetUserCount() const { return clientsInChannel.size(); }
	std::string						GetChannelName() const { return channelName; }
	const std::list<Client*>&		GetClientsInChannel() const { return clientsInChannel; }
	std::string						GetChannelMaster() const { return channelMaster; }
	std::mutex&						GetChannelLock() { return channelLock; }

	void							SetChannelMaster(const std::string& master) { channelMaster = master; }

private:
	std::string						channelName;
	std::string						channelMaster;
	std::list<Client*>				clientsInChannel;
	std::mutex						channelLock;
};


class PublicChannel
	: public Channel
{
public:
	PublicChannel(const std::string& name);
	virtual ~PublicChannel();
};


class CustomChannel
	: public Channel
{
public:
	CustomChannel(const std::string& name);
	virtual ~CustomChannel();

	virtual void	Enter(Client* client);
};