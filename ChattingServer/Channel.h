#pragma once
#include <string>
#include <list>
#include <mutex>


struct Client;

class Channel
{
public:
	Channel(const std::string& name);
	virtual ~Channel();
	
	virtual void	Enter(const Client* client) = 0;
	virtual void	Exit(const Client* client) = 0;
	

	unsigned int						GetUserCount() const { return clientsInChannel.size(); }
	std::string							GetChannelName() const { return channelName; }
	std::string							GetChannelMaster() const { return channelMaster; }
	std::mutex&							GetChannelLock() { return channelLock; }
	const std::list<const Client*>&		GetClientsInChannel() const { return clientsInChannel; }
	
protected:	
	void								SetChannelMaster(const std::string& master) { channelMaster = master; }
	void								SetChannelName(const std::string& name) { channelName = name; }

protected:
	std::string						channelName;
	std::string						channelMaster;
	std::list<const Client*>		clientsInChannel;
	std::mutex						channelLock;
};


class PublicChannel
	: public Channel
{
public:
	PublicChannel(const std::string& name);
	virtual ~PublicChannel();

	virtual void	Enter(const Client* client);
	virtual void	Exit(const Client* client);
};


class CustomChannel
	: public Channel
{
public:
	CustomChannel(const std::string& name);
	virtual ~CustomChannel();

	virtual void	Enter(const Client* client);
	virtual void	Exit(const Client* client);

	bool			IsCreated() const { return isCreated; }

	void			InitializeChannel(const std::string& chName);
	void			CloseChannel();

private:
	bool			isCreated;
};