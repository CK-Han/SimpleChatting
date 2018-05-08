#pragma once
#include <string>
#include <list>
#include <mutex>


struct Client;

/**
	@class Channel
	@brief		�⺻���� ä�� ������ �����ϴ� �߻� Ŭ����
	@details	���ο��� mutex�� ������� ������, ����ȭ�� channel mutex�� ��� �ܺο��� ����Ǿ�� �Ѵ�.
				���������Լ��� Enter�� Exit�� �����Ǿ��ִ�. (std::algorithm, std::list)
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
	@brief		����ä���� ����, ChannelName�� �Һ�, Master�� �������� �ʴ´�.

	@warning	����ä���� Master�� �������� ������, ä���̸��� ������� �ʾƾ� �Ѵ�.
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
	@brief		����� ��û�� ���� �����Ǵ� ä��
	@details	Master�� ù ä�� ������, Master�� ���� �ٸ� ������� �����ϴ� ��쿡 ����Ǹ�
				ChanneName�� ù ä�� �����ÿ� ��������.

	@warning	CustomChannel�� Initilize�ǰ� Enter�� ����Ǿ�� �Ѵ�. ��, ���� �� ����
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