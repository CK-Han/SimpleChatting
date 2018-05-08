#pragma once
#include <string>
#include <list>
#include <mutex>


struct Client;

/**
	@class Channel
	@brief		기본적인 채널 구조를 정의하는 추상 클래스
	@details	내부에서 mutex를 사용하지 않으며, 동기화는 channel mutex를 얻어 외부에서 진행되어야 한다.
				순수가상함수인 Enter와 Exit는 구현되어있다. (std::algorithm, std::list)
*/
class Channel
{
public:
	Channel(const std::string& name);
	virtual ~Channel();
	
	virtual void	Enter(const Client* client) = 0;
	virtual void	Exit(const Client* client) = 0;
	
	size_t								GetUserCount() const { return clientsInChannel.size(); }
	std::string							GetChannelName() const { return channelName; }
	std::string							GetChannelMaster() const { return channelMaster; }
	std::mutex&							GetChannelLock() { return channelLock; }
	const std::list<const Client*>&		GetClientsInChannel() const { return clientsInChannel; }
	
protected:
	std::string						channelName;
	std::string						channelMaster;
	std::list<const Client*>		clientsInChannel;
	std::mutex						channelLock;
};

/**
	@class PublicChannel
	@brief		공개채널을 정의, ChannelName은 불변, Master는 존재하지 않는다.

	@warning	공개채널은 Master가 존재하지 않으며, 채널이름이 변경되지 않아야 한다.
*/
class PublicChannel
	: public Channel
{
public:
	PublicChannel(const std::string& name);
	virtual ~PublicChannel();

	virtual void	Enter(const Client* client);
	virtual void	Exit(const Client* client);
};


/**
	@class CustomChannel
	@brief		사용자 요청에 의해 관리되는 채널
	@details	Master는 첫 채널 개설시, Master가 나가 다른 사람에게 위임하는 경우에 변경되며
				ChanneName은 첫 채널 개설시에 정해진다.

	@warning	CustomChannel은 Initilize되고 Enter가 진행되어야 한다. 즉, 생성 후 연결
*/
class CustomChannel
	: public Channel
{
public:
	CustomChannel(const std::string& name);
	virtual ~CustomChannel();

	virtual void	Enter(const Client* client);
	virtual void	Exit(const Client* client);

	void			InitializeChannel(const std::string& chName);
};