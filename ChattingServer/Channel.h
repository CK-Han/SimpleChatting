#pragma once
#include <string>
#include <list>

struct Client;

class Channel
{
public:
	Channel(const std::string& name);
	virtual ~Channel();

	virtual void	Enter(Client* client);
	virtual void	Exit(Client* client);
	
	int								GetUserCount() const { return clientsInChannel.size(); }
	std::string						GetChannelName() const { return channelName; }
	const std::list<Client*>&		GetClientsInChannel() const { return clientsInChannel; }
	std::string						GetChannelMaster() const { return channelMaster; }

	void							SetChannelMaster(const std::string& master) { channelMaster = master; }

private:
	const std::string				channelName;
	std::string						channelMaster;
	std::list<Client*>				clientsInChannel;
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