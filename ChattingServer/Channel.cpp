#include "Channel.h"
#include "Client.h"
#include <algorithm>


Channel::Channel(const std::string& name)
	: channelName(name)
{
}

Channel::~Channel()
{
}


void Channel::Enter(const Client* client)
{
	auto iter = std::find_if(clientsInChannel.cbegin(), clientsInChannel.cend(), 
		[&client](const Client* user)
	{
		return user->UserName == client->UserName;
	});

	if (iter == clientsInChannel.cend())
	{
		clientsInChannel.push_back(client);
	}
}

void Channel::Exit(const Client* client)
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
	, isCreated(false)
{
}


CustomChannel::~CustomChannel()
{
}


void CustomChannel::Enter(const Client* client)
{
	if (GetClientsInChannel().size() == 0)
	{
		SetChannelMaster(client->UserName);
	}

	Channel::Enter(client);
}


void CustomChannel::Exit(const Client* client)
{
	Channel::Exit(client);

	if (client->UserName == GetChannelMaster()
		&& GetUserCount() > 0)
	{	//����Ʈ���� ���� ���� ���� ����(front)���� ������ �ѱ��.
		SetChannelMaster(GetClientsInChannel().front()->UserName);
	}
}


void CustomChannel::InitializeChannel(const std::string& chName)
{
	GetClientsInChannel().clear();
	isCreated = true;
	SetChannelName(chName);
}

void CustomChannel::CloseChannel()
{
	GetClientsInChannel().clear();
	isCreated = false;
	SetChannelMaster("");
	SetChannelName("");
}