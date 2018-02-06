#include "Channel.h"
#include "Client.h"
#include <algorithm>


Channel::Channel(const std::string& name)
	: channelName(name)
{
}

Channel::Channel(const Channel& other)
	: channelName(other.channelName)
	, channelMaster(other.channelMaster)
	, clientsInChannel(other.clientsInChannel)
{
}

Channel::~Channel()
{
}


void Channel::Enter(Client* client)
{
	auto iter = std::find_if(clientsInChannel.cbegin(), clientsInChannel.cend(), 
		[&client](const Client* user)
	{
		return user->UserName == client->UserName;
	});

	if (iter == clientsInChannel.cend())
	{
		client->ChannelName = this->channelName;
		clientsInChannel.push_back(client);
	}
}

void Channel::Exit(Client* client)
{
	auto iter = std::find_if(clientsInChannel.cbegin(), clientsInChannel.cend(),
		[client](const Client* user)
	{
		return user->UserName == client->UserName;
	});

	if (iter != clientsInChannel.cend())
	{
		clientsInChannel.erase(iter);
	}
}

///////////////////////////////////////////////////////////////////

PublicChannel::PublicChannel(const std::string& name)
	: Channel(name)
{
}


PublicChannel::~PublicChannel()
{
}

///////////////////////////////////////////////////////////////////

CustomChannel::CustomChannel(const std::string& name)
	: Channel(name)
{
}


CustomChannel::~CustomChannel()
{
}

void CustomChannel::Enter(Client* client)
{
	if (GetClientsInChannel().size() == 0)
	{
		SetChannelMaster(client->UserName);
	}

	Channel::Enter(client);
}