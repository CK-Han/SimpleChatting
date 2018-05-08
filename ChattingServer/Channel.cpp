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

/**
	@brief		����ڸ� ä�� ���� ����Ʈ�� ���
	@details	std::find_if�� �����Ǿ��ִ�.
	
	@warning	����ȭ�� �ܺο��� channel mutex�� ��� ����Ǿ�� �Ѵ�.
*/
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

/**
	@brief		����ڸ� ä�� ���� ����Ʈ���� ����
	@details	std::find_if�� �����Ǿ��ִ�.

	@warning	����ȭ�� �ܺο��� channel mutex�� ��� ����Ǿ�� �Ѵ�.
*/
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

/**
	@brief		Channel::Enter�� �����Ѵ�.
*/
void PublicChannel::Enter(const Client* client)
{
	Channel::Enter(client);
}

/**
	@brief		Channel::Exit�� �����Ѵ�.
*/
void PublicChannel::Exit(const Client* client)
{
	Channel::Exit(client);
}

///////////////////////////////////////////////////////////////////

CustomChannel::CustomChannel(const std::string& name)
	: Channel(name)
{
}


CustomChannel::~CustomChannel()
{
}

/**
	@brief		ä�� ����Ʈ�� ��� ��, ó�� ������ client�� �������� �Ѵ�.
*/
void CustomChannel::Enter(const Client* client)
{
	if (GetClientsInChannel().size() == 0)
	{
		channelMaster = client->UserName;
	}

	Channel::Enter(client);
}

/**
	@brief		������ ä�� ����Ʈ���� �����Ѵ�.
	@details	���� ������ Master���, ���� Master�� �����Ѵ�. (list�� front)
				���� ������ ä�� ������ �������ٸ�, ä���� ����Ѵ�.
*/
void CustomChannel::Exit(const Client* client)
{
	Channel::Exit(client);

	if (client->UserName == GetChannelMaster()
		&& GetUserCount() >= 1)
	{	//����Ʈ���� ���� ���� ���� ����(front)���� ������ �ѱ��.
		channelMaster = GetClientsInChannel().front()->UserName;
	}
	else if (GetUserCount() == 0)
	{
		clientsInChannel.clear();
		channelMaster.clear();
		channelName.clear();
	}
}

/**
	@brief		ä���� ó�� �����Ǿ��� ��, ä���� �̸� �� ���� ����Ʈ�� �ʱ�ȭ�Ѵ�.
*/
void CustomChannel::InitializeChannel(const std::string& chName)
{
	clientsInChannel.clear();
	channelName = chName;
}
