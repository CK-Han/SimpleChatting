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
	@brief		사용자를 채널 유저 리스트에 등록
	@details	std::find_if로 구현되어있다.
	
	@warning	동기화는 외부에서 channel mutex를 얻어 진행되어야 한다.
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
	@brief		사용자를 채널 유저 리스트에서 제거
	@details	std::find_if로 구현되어있다.

	@warning	동기화는 외부에서 channel mutex를 얻어 진행되어야 한다.
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
	@brief		Channel::Enter로 동작한다.
*/
void PublicChannel::Enter(const Client* client)
{
	Channel::Enter(client);
}

/**
	@brief		Channel::Exit로 동작한다.
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
	@brief		채널 리스트에 등록 전, 처음 들어오는 client를 방장으로 한다.
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
	@brief		유저를 채널 리스트에서 제거한다.
	@details	나간 유저가 Master라면, 다음 Master를 선정한다. (list의 front)
				나간 유저가 채널 마지막 유저였다면, 채널을 폐기한다.
*/
void CustomChannel::Exit(const Client* client)
{
	Channel::Exit(client);

	if (client->UserName == GetChannelMaster()
		&& GetUserCount() >= 1)
	{	//리스트에서 가장 오래 지낸 유저(front)에게 방장을 넘긴다.
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
	@brief		채널이 처음 생성되었을 때, 채널의 이름 및 유저 리스트를 초기화한다.
*/
void CustomChannel::InitializeChannel(const std::string& chName)
{
	clientsInChannel.clear();
	channelName = chName;
}
