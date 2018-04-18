#include "Framework.h"
#include <iostream>
#include <algorithm>
#include <random>
#pragma comment (lib, "ws2_32.lib")

namespace
{
	void AcceptThreadStart();
	void WorkerThreadStart();
	
} //unnamed namespace

Framework::Framework()
	: hIocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0))
	, isShutdown(false)
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	Initialize();
}


Framework::~Framework()
{
	ShutDown();
}


void Framework::Initialize()
{
	clients.reserve(MAX_CLIENT_COUNT);
	for (auto i = 0; i < MAX_CLIENT_COUNT; ++i)
		clients.emplace_back(std::make_unique<Client>());

	//protocol의 MAX_PUBLIC_CHANNEL_COUNT 고려
	publicChannels.emplace_back(std::make_shared<PublicChannel>("FreeChannel"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("ForTeenagers"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("For20s"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("For3040s"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("AboutGame"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("AboutStudy"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("AboutHobby"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("AboutExcercise"));
								
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample1"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample2"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample3"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample4"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample5"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample6"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample7"));
	publicChannels.emplace_back(std::make_shared<PublicChannel>("Sample8"));
	
	customChannels.reserve(MAX_CUSTOM_COUNT);
	for (auto i = 0; i < MAX_CUSTOM_COUNT; ++i)
		customChannels.emplace_back(std::make_shared<CustomChannel>(""));

	for (auto i = 0; i < MAX_CLIENT_COUNT; ++i)
		validClientSerials.push(i);

	for (auto i = 0; i < MAX_CUSTOM_COUNT; ++i)
		validCustomChannelSerials.push(i);

	for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
		workerThreads.emplace_back(new std::thread(WorkerThreadStart));

	acceptThread = std::unique_ptr<std::thread>(new std::thread(AcceptThreadStart));
}

void Framework::ShutDown()
{
	isShutdown = true;

	for (auto& th : workerThreads)
		th->join();
	acceptThread->join();

	::WSACleanup();
}


////////////////////////////////////////////////////////////////////////////
//Send to Client
void Framework::SendPacket(Framework::SERIAL_TYPE serial, unsigned char* packet) const
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;
	
	auto& client =  clients[serial];
	if (client->IsLogin == false) return;

	Overlap_Exp* overlapExp = nullptr;
	try
	{
		overlapExp = new Overlap_Exp;
	}
	catch(...)
	{
		std::cout << "SendPacket() - new error!\n";
		return;
	}

	::ZeroMemory(overlapExp, sizeof(Overlap_Exp));
	
	overlapExp->Operation = OPERATION_SEND;
	overlapExp->WsaBuf.buf = reinterpret_cast<CHAR *>(overlapExp->Iocp_Buffer);
	overlapExp->WsaBuf.len = GetPacketSize(packet);
	memcpy(overlapExp->Iocp_Buffer, packet, overlapExp->WsaBuf.len);

	int ret = ::WSASend(client->ClientSocket, &overlapExp->WsaBuf, 1, NULL, 0,
		&overlapExp->Original_Overlap, NULL);

	if (0 != ret) 
	{
		int error_no = ::WSAGetLastError();
		if (WSA_IO_PENDING != error_no 
			&& WSAECONNRESET != error_no)
			std::cout << "SendPacket::WSASend Error : " <<  error_no << std::endl;
	}
}

void Framework::SendSystemMessage(Framework::SERIAL_TYPE serial, const std::string& msg) const
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return;
	if (clients[serial]->IsLogin == false) return;

	packet_system to_packet;
	::ZeroMemory(&to_packet, sizeof(to_packet));
	to_packet.Size = sizeof(to_packet);
	to_packet.Type = PACKET_SYSTEM;
	std::memcpy(to_packet.SystemMessage, msg.c_str(), min(msg.size(), MAX_SYSTEMMSG_LENGTH));

	SendPacket(serial, reinterpret_cast<unsigned char*>(&to_packet));
}

////////////////////////////////////////////////////////////////////////////
//Received from Client
void Framework::ProcessPacket(Framework::SERIAL_TYPE serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	switch (GetPacketType(packet))
	{
	case PACKET_LOGIN:
		ProcessLogin(serial, packet);
		break;
	case PACKET_CHANNEL_LIST:
		ProcessChannelList(serial);
		break;
	case PACKET_CHANNEL_ENTER:
		ProcessChannelChange(serial, packet);
		break;
	case PACKET_KICK_USER:
		ProcessKick(serial, packet);
		break;
	case PACKET_CHATTING:
		ProcessChatting(serial, packet);
		break;
	
	default:
		std::cout << "ProcessPacket() - Unknown Packet Type" << std::endl;
		break;
	}
}


void Framework::ProcessUserClose(Framework::SERIAL_TYPE serial)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return;
	auto& client = GetClient(serial);
	if (client.IsLogin == false) return;

	if (client.ChannelName.empty() == false)
	{	//채널이 존재한다 -> 채널에 존재하는 유저들에게 이 유저가 떠남을 알린다.
		auto channel = FindChannelFromName(client.ChannelName);
		if(channel)
			HandleUserLeave(serial, false, channel);
	}
	::closesocket(client.ClientSocket);

	std::unique_lock<std::mutex> ulName(clientNameLock);
	usedClientNames.erase(client.UserName);
	ulName.unlock();

	client.IsLogin = false;
	client.UserName.clear();
	client.ChannelName.clear();
	validClientSerials.push(serial);
}

void Framework::ProcessLogin(Framework::SERIAL_TYPE serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	packet_login* from_packet = reinterpret_cast<packet_login*>(packet);
	bool isDuplicated = false;

	std::unique_lock<std::mutex> ulName(clientNameLock);
	auto iterName = usedClientNames.find(from_packet->User);
	if (iterName != usedClientNames.cend())
		isDuplicated = true;
	else
	{
		clients[serial]->UserName = from_packet->User;
		usedClientNames.insert(std::make_pair(from_packet->User, serial));
	}
	ulName.unlock();

	packet_login to_packet;
	::ZeroMemory(&to_packet, sizeof(to_packet));
	to_packet.Size = sizeof(to_packet);
	to_packet.Type = PACKET_LOGIN;
	to_packet.Created = !(isDuplicated);
	std::memcpy(&to_packet.User, from_packet->User, MAX_USERNAME_LENGTH);

	SendPacket(serial, reinterpret_cast<unsigned char *>(&to_packet));

	//아이디 생성되어 로그인했다면, 공개채널으로 연결 및 통지해야한다.
	if (to_packet.Created)
	{
		ConnectToRandomPublicChannel(serial);
	}
}

void Framework::ProcessChannelList(Framework::SERIAL_TYPE serial)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return;
	if (clients[serial]->IsLogin == false) return;

	packet_channel_list channelList_packet;
	::ZeroMemory(&channelList_packet, sizeof(channelList_packet));
	//공개채널은 불변으로 락이 필요치 않으나 커스텀채널은 가변으로 개수 확인시 락이 필요할 것이다.
	channelList_packet.Size = sizeof(channelList_packet);
	channelList_packet.Type = PACKET_CHANNEL_LIST;
	channelList_packet.PublicChannelCount = publicChannels.size();
	std::string publicChannelNames;
	for (auto& ch : publicChannels)
	{
		publicChannelNames += ch->GetChannelName() + NAME_DELIMITER;
	}
	std::memcpy(&channelList_packet.PublicChannelNames, publicChannelNames.c_str(), publicChannelNames.size());
	
	std::unique_lock<std::mutex> ulCustom(customChannelsLock);
	unsigned int customChannelCount = 0;	
	for (auto& customChannel : customChannels)
	{
		if (customChannel->IsCreated()) 
			customChannelCount++;
	}
	ulCustom.unlock();

	channelList_packet.CustomChannelCount = customChannelCount;
	
	
	SendPacket(serial, reinterpret_cast<unsigned char *>(&channelList_packet));
}

void Framework::ProcessChatting(Framework::SERIAL_TYPE serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	auto& client =GetClient(serial);
	if (client.IsLogin == false) return;

	packet_chatting* from_packet = reinterpret_cast<packet_chatting*>(packet);
	
	if (from_packet->IsWhisper == false)
	{
		BroadcastToChannel(client.ChannelName, packet);
	}
	else
	{
		Framework::SERIAL_TYPE listnerSerial = FindClientSerialFromName(from_packet->Listner);
		if (SERIAL_ERROR != listnerSerial)
		{
			SendPacket(serial, packet);
			SendPacket(listnerSerial, packet);
		}
		else
			SendSystemMessage(serial, "***System*** 해당 유저는 접속중이 아닙니다.");
	}
}

void Framework::ProcessKick(Framework::SERIAL_TYPE serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	if (clients[serial]->IsLogin == false) return;

	packet_kick_user* from_packet = reinterpret_cast<packet_kick_user*>(packet);
	
	Framework::SERIAL_TYPE targetSerial = FindClientSerialFromName(from_packet->Target);
	if (targetSerial == SERIAL_ERROR)
	{
		SendSystemMessage(serial, "***System*** 해당 유저는 접속중이 아닙니다.");
		return;
	}
	auto& target = clients[targetSerial];

	auto channel = FindChannelFromName(from_packet->Channel);
	if (channel == nullptr)
	{
		SendSystemMessage(serial, "***System*** 요청 처리 실패, 다시 입력해주세요.");
		return;
	}
	
	if (channel->GetChannelMaster() != from_packet->Kicker
		|| target->ChannelName != from_packet->Channel)
	{
		SendSystemMessage(serial, "***System*** 방장이 아니거나, 같은 채널이 아닙니다.");
		return;
	}

	if (target->Serial == serial)
	{
		SendSystemMessage(serial, "***System*** 본인을 강퇴할 수 없습니다.");
		return;
	}

	//내보내고 빈 공개방으로 이동시킴
	HandleUserLeave(target->Serial, true, channel);
	ConnectToRandomPublicChannel(target->Serial);
}

void Framework::ProcessChannelChange(Framework::SERIAL_TYPE serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	auto& client =GetClient(serial);
	if (client.IsLogin == false) return;

	packet_channel_enter* from_packet = reinterpret_cast<packet_channel_enter*>(packet);
	if (from_packet->ChannelName == client.ChannelName) return;

	CHANNEL_CONNECT isChannelChanged = CHANNEL_CONNECT::FAIL_ARGUMENT;
	auto channel = FindChannelFromName(from_packet->ChannelName);
	auto prevChannel = FindChannelFromName(client.ChannelName);

	if (channel)
	{
		isChannelChanged = ConnectToChannel(serial, from_packet->ChannelName);

		switch (isChannelChanged)
		{
		case CHANNEL_CONNECT::FAIL_ARGUMENT:
		case CHANNEL_CONNECT::FAIL_INVALID_CHANNEL:
			SendSystemMessage(serial, "***System*** 요청 처리 실패, 다시 입력해주세요.");
			break;
		case CHANNEL_CONNECT::FAIL_FULL:
			SendSystemMessage(serial, "***System*** 채널의 최대 수용인원을 초과하였습니다.");
			break;
		default:
			break;
		}		
	}
	else //새 커스텀채널 생성
	{
		AddNewCustomChannel(from_packet->ChannelName);
		isChannelChanged = ConnectToChannel(serial, from_packet->ChannelName);
	}

	if (isChannelChanged == CHANNEL_CONNECT::SUCCESS 
		&& (prevChannel != nullptr))
	{
		HandleUserLeave(serial, false, prevChannel);
	}
}


////////////////////////////////////////////////////////////////////////////
//Private Functions


//return integer [-1, MAX_PUBLIC_CHANNEL_COUNT), '-1' means public channels are busy
Framework::SERIAL_TYPE Framework::GetRandomPublicChannelSerial() const
{
	const unsigned int publicChannelCount = publicChannels.size();
	std::uniform_int_distribution<int> uid(0, publicChannelCount - 1);
	static std::default_random_engine dre;

	std::vector<bool> channelIsFull(publicChannelCount, false);
	Framework::SERIAL_TYPE randSlot = SERIAL_ERROR;
	size_t fullChannelCount = 0;

	while (true)
	{
		randSlot = uid(dre);
		if (publicChannels[randSlot]->GetUserCount() < MAX_CHANNEL_USERS)
			return randSlot;
		else if(channelIsFull[randSlot] == false)
		{
			channelIsFull[randSlot] = true;
			++fullChannelCount;

			if(fullChannelCount >= PUBLIC_BUSY_COUNT)	//어느정도 채널 내 유저가 차있다면 혼잡한 상태로 한다.
				return -1;
		}
	}
}

Framework::SERIAL_TYPE Framework::GetSerialForNewCustomChannel()
{
	using namespace std::chrono;
	SERIAL_TYPE customSerial = SERIAL_ERROR;

	auto beginTime = high_resolution_clock::now();
	while (false == validCustomChannelSerials.try_pop(customSerial))
	{
		auto elapsedTime = duration_cast<milliseconds>(high_resolution_clock::now() - beginTime);
		if (MAKECUSTOM_TIMEOUT_MILLISECONDS <= elapsedTime.count())
			return SERIAL_ERROR;
	}

	return customSerial;
}

void Framework::BroadcastToChannel(const std::string& channelName, unsigned char* packet)
{
	if (packet == nullptr) return;
	auto channel = FindChannelFromName(channelName);

	if (channel)
	{
		std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());

		for (auto* client : channel->GetClientsInChannel())
			SendPacket(client->Serial, packet);
	}
}

void Framework::HandleUserLeave(Framework::SERIAL_TYPE leaver, bool isKicked, std::shared_ptr<Channel>& channel)
{
	if (leaver < 0 || MAX_CLIENT_COUNT <= leaver) return;

	auto& client = GetClient(leaver);
	if (client.IsLogin == false) return;

	if (channel == nullptr)
	{
		std::cout << "HandleUserLeave() - channel is nullptr\n";
		return;
	}

	bool isMasterChanged = false;
	std::string channelName = channel->GetChannelName();

	std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());
	
	std::string beforeMasterName = channel->GetChannelMaster();
	channel->Exit(&client);	//퇴장에 의해 방장이 변경될 수 있다.

	if (client.UserName == beforeMasterName)
	{	
		isMasterChanged = true;

		if (channel->GetUserCount() == 0)
		{	//채널 파기, 본 조건문이 진행되는 경우는 channel이 반드시 CustomChannel 이다.
			std::unique_lock<std::mutex> ulCustom(customChannelsLock);
			validCustomChannelSerials.push(usedCustomChannelNames[channelName]);
			usedCustomChannelNames.erase(channelName);
			ulCustom.unlock();
			
			CustomChannel* closedChannel = static_cast<CustomChannel*>(channel.get());
			closedChannel->CloseChannel();
			return; //후속처리(채널에 남은 유저들에게 정보 전송)가 필요하지 않아 return
		}
	}
	ulChannel.unlock();

	packet_user_leave leave_packet;
	::ZeroMemory(&leave_packet, sizeof(leave_packet));
	leave_packet.Size = sizeof(leave_packet);
	leave_packet.Type = PACKET_USER_LEAVE;
	std::memcpy(&leave_packet.User, client.UserName.c_str(), client.UserName.size());
	leave_packet.IsKicked = isKicked;

	if (isKicked)
		SendSystemMessage(leaver, "***System*** 방장에 의해 강퇴당하였습니다. 공개채널로 이동합니다.");

	BroadcastToChannel(channelName, reinterpret_cast<unsigned char*>(&leave_packet));

	if (isMasterChanged)
	{
		packet_new_master new_master_packet;
		::ZeroMemory(&new_master_packet, sizeof(new_master_packet));
		new_master_packet.Size = sizeof(new_master_packet);
		new_master_packet.Type = PACKET_NEW_MASTER;
		std::memcpy(&new_master_packet.Channel, channelName.c_str(), channelName.size());
		std::memcpy(&new_master_packet.Master, channel->GetChannelMaster().c_str(), channel->GetChannelMaster().size());

		BroadcastToChannel(channelName, reinterpret_cast<unsigned char*>(&new_master_packet));
	}
}

void Framework::ConnectToRandomPublicChannel(Framework::SERIAL_TYPE serial)
{
	CHANNEL_CONNECT isConnected = CHANNEL_CONNECT::FAIL_ARGUMENT;
	while (isConnected != CHANNEL_CONNECT::SUCCESS)
	{
		Framework::SERIAL_TYPE randSlot = GetRandomPublicChannelSerial();
		if (randSlot == -1)
		{	//모든 공개채널이 가득 찬 상태로, 채널에 입장하지 못한 상태
			//스타크래프트1 배틀넷의 Void 채널과 유사
			SendSystemMessage(serial, "***System*** 모든 공개채널이 혼잡하여 연결하지 못했습니다.");
			return;
		}
		else
			isConnected = ConnectToChannel(serial, publicChannels[randSlot]->GetChannelName());
	}
}

Framework::CHANNEL_CONNECT 
	Framework::ConnectToChannel(Framework::SERIAL_TYPE serial, const std::string& channelName)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) 
		return CHANNEL_CONNECT::FAIL_ARGUMENT;

	auto& client = GetClient(serial);
	if (client.IsLogin == false) return CHANNEL_CONNECT::FAIL_ARGUMENT;

	auto channel = FindChannelFromName(channelName);
	if (channel == nullptr)
		return CHANNEL_CONNECT::FAIL_INVALID_CHANNEL;

	std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());

	if (channel->GetUserCount() < MAX_CHANNEL_USERS)
	{
		channel->Enter(&client);
		client.ChannelName = channelName;
	}
	else
		return CHANNEL_CONNECT::FAIL_FULL;

	packet_newface_enter newface_packet;
	::ZeroMemory(&newface_packet, sizeof(newface_packet));
	newface_packet.Size = sizeof(newface_packet);
	newface_packet.Type = PACKET_NEWFACE_ENTER;
	std::memcpy(&newface_packet.User, client.UserName.c_str(), client.UserName.size());

	//1. 연결하고자 하는 채널의 유저들에게 새 유저를 알리면서 이름을 종합한다.
	std::string userNames;
	unsigned int userCount = 0;
	for (auto* clientInChannel : channel->GetClientsInChannel())
	{
		SendPacket(clientInChannel->Serial, reinterpret_cast<unsigned char*>(&newface_packet));
		userNames += clientInChannel->UserName + NAME_DELIMITER;
		++userCount;
	}
	ulChannel.unlock();

	//2. 새 유저는 이동하고자 하는 채널 정보 및 채널에 존재하고있던 유저 정보를 얻는다.
	packet_channel_enter enter_packet;
	::ZeroMemory(&enter_packet, sizeof(enter_packet));
	enter_packet.Size = sizeof(enter_packet);
	enter_packet.Type = PACKET_CHANNEL_ENTER;
	std::memcpy(&enter_packet.ChannelName, client.ChannelName.c_str(), client.ChannelName.size());
	std::memcpy(&enter_packet.ChannelMaster, channel->GetChannelMaster().c_str(), channel->GetChannelMaster().size());

	SendPacket(serial, reinterpret_cast<unsigned char *>(&enter_packet));

	packet_channel_users users_packet;
	::ZeroMemory(&users_packet, sizeof(users_packet));
	users_packet.Size = sizeof(users_packet);
	users_packet.Type = PACKET_CHANNEL_USERS;
	std::memcpy(&users_packet.ChannelName, client.ChannelName.c_str(), client.ChannelName.size());
	users_packet.UserCountInPacket = userCount;
	std::memcpy(&users_packet.UserNames, userNames.c_str(), userNames.size());

	SendPacket(serial, reinterpret_cast<unsigned char *>(&users_packet));

	return CHANNEL_CONNECT::SUCCESS;
}

Framework::SERIAL_TYPE Framework::FindClientSerialFromName(const std::string& clientName)
{
	std::unique_lock<std::mutex> ulName(clientNameLock);

	auto iterName = usedClientNames.find(clientName);
	if (iterName != usedClientNames.cend())
		return iterName->second;
	else
		return SERIAL_ERROR;
}

std::shared_ptr<Channel> 
	Framework::FindChannelFromName(const std::string& channelName)
{
	//공개방은 변경되지 않는다.
	for (auto& ch : publicChannels)
	{
		if (channelName == ch->GetChannelName())
		{
			return ch;
		}
	}

	std::unique_lock<std::mutex> ulCustom(customChannelsLock);

	auto iterName = usedCustomChannelNames.find(channelName);
	if (iterName != usedCustomChannelNames.cend())
		return customChannels[iterName->second];
	else
		return{ nullptr };
}

Framework::SERIAL_TYPE Framework::GetSerialForNewClient()
{
	SERIAL_TYPE newSerial = SERIAL_ERROR;
	using namespace std::chrono;

	auto beginTime = high_resolution_clock::now();
	while (false == validClientSerials.try_pop(newSerial))
	{
		auto elapsedTime = duration_cast<milliseconds>(high_resolution_clock::now() - beginTime);
		if (LOGIN_TIMEOUT_MILLISECONDS <= elapsedTime.count())
			return SERIAL_ERROR;
	}

	clients[newSerial]->IsLogin = true;
	return newSerial;
}

void Framework::AddNewCustomChannel(const std::string& channelName)
{
	auto customSerial = GetSerialForNewCustomChannel();
	if (customSerial == SERIAL_ERROR)
	{
		std::cout << "AddNewCustomChannel() error - validCustomSerialQueue try_pop timeout\n";
		return;
	}

	std::unique_lock<std::mutex> ulCustom(customChannelsLock);

	auto iterName = usedCustomChannelNames.find(channelName);
	if (iterName != usedCustomChannelNames.cend())
	{
		validCustomChannelSerials.push(customSerial);
		std::cout << "AddNewCustomChannel() error - duplicated channelName\n";
		return;
	}

	usedCustomChannelNames.insert(std::make_pair(channelName, customSerial));
	customChannels[customSerial]->InitializeChannel(channelName);
}



Framework::SERIAL_TYPE Framework::DebugUserCount()
{
	//lock이 없어 정확하지 않음
	Framework::SERIAL_TYPE userCount = 0;
	for (Framework::SERIAL_TYPE i = 0; i < MAX_CLIENT_COUNT; ++i)
	{
		if (clients[i]->IsLogin) userCount++;
	}
	return userCount;
}

std::vector<std::string> 
	Framework::DebugCustomChannels()
{
	std::vector<std::string> customs;

	std::unique_lock<std::mutex> ulLogin(customChannelsLock);
	for (auto& ch : customChannels)
	{
		if (ch->IsCreated())
			customs.emplace_back(ch->GetChannelName());
	}

	return customs;
}



namespace
{
	void AcceptThreadStart()
	{
		Framework& framework = Framework::GetInstance();
		SOCKADDR_IN listenAddr;

		SOCKET acceptSocket = ::WSASocketW(AF_INET, SOCK_STREAM,
			IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

		std::memset(&listenAddr, 0, sizeof(listenAddr));
		listenAddr.sin_family = AF_INET;
		listenAddr.sin_addr.s_addr = htonl(ADDR_ANY);
		listenAddr.sin_port = htons(MY_SERVER_PORT);

		::bind(acceptSocket, reinterpret_cast<sockaddr *>(&listenAddr), sizeof(listenAddr));
		::listen(acceptSocket, 10);

		FD_SET acceptSet;
		timeval timeVal;
		timeVal.tv_sec = Framework::ACCEPT_TIMEOUT_SECONDS;
		timeVal.tv_usec = 0;
		
		while (false == framework.IsShutDown())
		{
			FD_ZERO(&acceptSet);
			FD_SET(acceptSocket, &acceptSet);
			int selectResult = ::select(0, &acceptSet, nullptr, nullptr, &timeVal);
			if (selectResult == 0)
			{	//timeout
				continue;
			}
			else if (selectResult == SOCKET_ERROR)
			{
				std::cout << "AcceptThreadStart() - select() error\n";
				continue;
			}

			SOCKADDR_IN clientAddr;
			int addrSize = sizeof(clientAddr);
			
			SOCKET newClientSocket = ::WSAAccept(acceptSocket,
				reinterpret_cast<sockaddr *>(&clientAddr), &addrSize,
				NULL, NULL);

			if (INVALID_SOCKET == newClientSocket)
			{
				int error_no = WSAGetLastError();
				std::cout << "AcceptThreadStart() - WSAAccept " << "error code : " << error_no << std::endl;
				continue;
			}

			Framework::SERIAL_TYPE newSerial = framework.GetSerialForNewClient();
			if (newSerial == Framework::SERIAL_ERROR)
			{
				::closesocket(newClientSocket);
				std::cout << "AcceptThreadStart() - WSAAccept " << ": too much people\n";
				continue;
			}

			//새 클라이언트 생성, 자료구조에 등록
			Client& client = framework.GetClient(newSerial);

			client.Serial = newSerial;
			client.ClientSocket = newClientSocket;
			client.RecvOverlap.Operation = OPERATION_RECV;
			client.RecvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(client.RecvOverlap.Iocp_Buffer);
			client.RecvOverlap.WsaBuf.len = sizeof(client.RecvOverlap.Iocp_Buffer);

			CreateIoCompletionPort(reinterpret_cast<HANDLE>(newClientSocket),
				framework.GetIocpHandle(), newSerial, 0);

			DWORD flags = 0;
			int ret = ::WSARecv(newClientSocket, &client.RecvOverlap.WsaBuf, 1, NULL,
				&flags, &client.RecvOverlap.Original_Overlap, NULL);
			if (0 != ret)
			{
				int error_no = WSAGetLastError();
				if (WSA_IO_PENDING != error_no)
					std::cout << "AcceptThreadStart() - WSARecv " << "error code : " << error_no << std::endl;
			}
		}
	}

	void WorkerThreadStart()
	{
		Framework& framework = Framework::GetInstance();
		while (false == framework.IsShutDown())
		{
			DWORD iosize;
			DWORD serial;
			Overlap_Exp* overlapExp;

			BOOL result = GetQueuedCompletionStatus(framework.GetIocpHandle(),
				&iosize, &serial, reinterpret_cast<LPOVERLAPPED*>(&overlapExp), Framework::GQCS_TIMEOUT_MILLISECONDS);

			if (overlapExp == nullptr 
				&& result == false)
			{	//timeout
				continue;
			}
			if (overlapExp->Original_Overlap.Pointer != nullptr)
			{
				std::cout << "WorkerThreadStart() - GetQueuedCompletionStatus error\n";
				continue;
			}
			if (0 == iosize)
			{
				framework.ProcessUserClose(serial);
				continue;
			}

			if (OPERATION_RECV == overlapExp->Operation)
			{
				Client& client = framework.GetClient(serial);
				unsigned char* ioBufCursor = overlapExp->Iocp_Buffer;
				int remained = 0;

				if ((client.PreviousCursor + iosize) > MAX_BUFF_SIZE)
				{
					int empty = MAX_BUFF_SIZE - client.PreviousCursor;
					std::memcpy(client.PacketBuff + client.PreviousCursor, ioBufCursor, empty);

					remained = iosize - empty;
					ioBufCursor += empty;
					client.PreviousCursor += empty;
				}
				else
				{
					std::memcpy(client.PacketBuff + client.PreviousCursor, ioBufCursor, iosize);
					client.PreviousCursor += iosize;
				}

				do
				{
					client.PacketSize = GetPacketSize(client.PacketBuff);

					if (client.PacketSize <= client.PreviousCursor)
					{	//조립가능
						framework.ProcessPacket(serial, client.PacketBuff);
						std::memmove(client.PacketBuff, client.PacketBuff + client.PacketSize
							, client.PreviousCursor - client.PacketSize);

						client.PreviousCursor -= client.PacketSize;
						client.PacketSize = 0;
					}
				} while (client.PacketSize == 0);

				if (remained > 0)
				{
					std::memcpy(client.PacketBuff + client.PreviousCursor, ioBufCursor, remained);
					client.PreviousCursor += remained;
				}

				DWORD flags = 0;
				::WSARecv(client.ClientSocket,
					&client.RecvOverlap.WsaBuf, 1, NULL, &flags,
					&client.RecvOverlap.Original_Overlap, NULL);
			}
			else if (OPERATION_SEND == overlapExp->Operation)
			{	//전송완료, 메모리 해제
				delete overlapExp;
			}
			else
			{
				std::cout << "Unknown IOCP event!\n";
				return;
			}
		}
	}

} //unnamed namespace





