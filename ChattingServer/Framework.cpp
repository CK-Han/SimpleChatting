#include "Framework.h"
#include "../Common/stream.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <random>
#include <fstream>


namespace
{
	void AcceptThreadStart();
	void WorkerThreadStart();
	
} //unnamed namespace

Framework::Framework()
	: hIocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0))
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	isShutDown = !Initialize();
}


Framework::~Framework()
{
	ShutDown();
}

bool Framework::Initialize()
{
	try
	{
		clients.reserve(MAX_CLIENT_COUNT);
		for (auto i = 0; i < MAX_CLIENT_COUNT; ++i)
			clients.emplace_back(new Client);

		sendOverlapExps.reserve(MAX_OVERLAPEXP_COUNT);
		for (auto i = 0; i < MAX_OVERLAPEXP_COUNT; ++i)
			sendOverlapExps.emplace_back(new Overlap_Exp);
	}
	catch (const std::bad_alloc&)
	{
		std::cout << "Initialize() - bad_alloc\n";
		return false;
	}

	for (auto i = 0; i < MAX_CLIENT_COUNT; ++i)
		validClientSerials.push(i);

	for (auto i = 0; i < MAX_CUSTOM_COUNT; ++i)
		validCustomChannelSerials.push(i);

	for (auto i = 0; i < MAX_OVERLAPEXP_COUNT; ++i)
		validOverlapExpSerials.push(i);

	//����ä�� ����Ʈ ���� �б�
	std::ifstream publicChannelFile("../Common/PublicChannel.txt");
	if (publicChannelFile.is_open() == false)
	{
		std::cout << "cannot open public Channel list file\n";
		return false;
	}

	std::string token;
	while (std::getline(publicChannelFile, token))
	{
		if (token.empty() == false)
			publicChannels.emplace_back(std::make_shared<PublicChannel>(token));
	}
	publicChannelFile.close();

	customChannels.reserve(MAX_CUSTOM_COUNT);
	for (auto i = 0; i < MAX_CUSTOM_COUNT; ++i)
		customChannels.emplace_back(std::make_shared<CustomChannel>(""));


	packetProcedures.insert(std::make_pair(Packet_Login::typeAdder.GetType(), &Framework::Process_Login));
	packetProcedures.insert(std::make_pair(Packet_Channel_List::typeAdder.GetType(), &Framework::Process_ChannelList));
	packetProcedures.insert(std::make_pair(Packet_Chatting::typeAdder.GetType(), &Framework::Process_Chatting));
	packetProcedures.insert(std::make_pair(Packet_Kick_User::typeAdder.GetType(), &Framework::Process_Kick));
	packetProcedures.insert(std::make_pair(Packet_Channel_Enter::typeAdder.GetType(), &Framework::Process_ChannelChange));

	try
	{
		for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
			workerThreads.emplace_back(new std::thread(WorkerThreadStart));

		acceptThread = std::unique_ptr<std::thread>(new std::thread(AcceptThreadStart));
	}
	catch (const std::bad_alloc&)
	{
		std::cout << "Initialize() - bad_alloc\n";
		return false;
	}

	return true;
}

void Framework::ShutDown()
{
	isShutDown = true;

	for (auto& th : workerThreads)
		th->join();
	acceptThread->join();

	::WSACleanup();
}


////////////////////////////////////////////////////////////////////////////
//Send to Client
void Framework::SendPacket(Framework::SERIAL_TYPE serial, const void* packet)
{
	if (IsValidClientSerial(serial) == false
		|| packet == nullptr) return;
	
	auto& client =  clients[serial];
	if (client->IsConnect == false) return;


	SERIAL_TYPE overlapSerial = GetSerialForUseOverlapExp();
	if (SERIAL_ERROR == overlapSerial)
	{
		std::cout << "SendPacket() - GetSerialForUseOverlapExp timeout\n";
		return;
	}
	
	::ZeroMemory(&(*sendOverlapExps[overlapSerial]), sizeof(Overlap_Exp));
	sendOverlapExps[overlapSerial]->Serial = overlapSerial;
	sendOverlapExps[overlapSerial]->Operation = OPERATION_SEND;
	sendOverlapExps[overlapSerial]->WsaBuf.buf = reinterpret_cast<CHAR *>(sendOverlapExps[overlapSerial]->Iocp_Buffer);
	sendOverlapExps[overlapSerial]->WsaBuf.len = GetPacketSize(packet);
	std::memcpy(sendOverlapExps[overlapSerial]->Iocp_Buffer, packet, sendOverlapExps[overlapSerial]->WsaBuf.len);

	int ret = ::WSASend(client->ClientSocket, &sendOverlapExps[overlapSerial]->WsaBuf, 1, NULL, 0,
	&sendOverlapExps[overlapSerial]->Original_Overlap, NULL);
	
	if (0 != ret) 
	{
		int error_no = ::WSAGetLastError();
		if (WSA_IO_PENDING != error_no 
			&& WSAECONNRESET != error_no
			&& WSAECONNABORTED != error_no)
			std::cout << "SendPacket::WSASend Error : " <<  error_no << std::endl;
	}
}

void Framework::SendSystemMessage(Framework::SERIAL_TYPE serial, const std::string& msg)
{
	if (IsValidClientSerial(serial) == false 
		|| clients[serial]->IsConnect == false)
		return;

	Packet_System systemPacket;
	systemPacket.systemMessage = msg;

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter stream(buf, sizeof(buf));
	systemPacket.Serialize(stream);

	SendPacket(serial, stream.GetBuffer());
}

////////////////////////////////////////////////////////////////////////////
//Received from Client
void Framework::ProcessPacket(Framework::SERIAL_TYPE serial, unsigned char* packet, int size)
{
	if (IsValidClientSerial(serial) == false
		|| packet == nullptr) return;

	auto type = GetPacketType(packet);
	StreamReader stream(packet, size);

	auto procedure = packetProcedures.find(type);
	if (procedure == packetProcedures.end())
		std::cout << "ProcessPacket() - Unknown Packet Type : " << type << std::endl;
	else
		(this->*packetProcedures[type])(serial, stream);
}


void Framework::ProcessUserClose(Framework::SERIAL_TYPE serial)
{
	if (IsValidClientSerial(serial) == false) return;
	
	auto& client = GetClient(serial);
	if (client.IsConnect == false) return;
	//Ŭ���̾�Ʈ �� �ʿ� X -> serial ��ȯ ��
	client.IsConnect = false;

	if (client.ChannelName.empty() == false)
	{	//ä���� �����Ѵ� -> ä�ο� �����ϴ� �����鿡�� �� ������ ������ �˸���.
		auto channel = FindChannelFromName(client.ChannelName);
		if(channel)
			HandleUserLeave(serial, false, channel);
	}
	::closesocket(client.ClientSocket);

	std::unique_lock<std::mutex> ulName(clientNameLock);
	usedClientNames.erase(client.UserName);
	ulName.unlock();

	client.UserName.clear();
	client.ChannelName.clear();
	validClientSerials.push(serial);
}

void Framework::Process_Login(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false
		|| clients[serial]->IsConnect == false) 
		return;
	
	//Ŭ���̾�Ʈ �� �ʿ� X -> ��Ŷ ó�� ���� ������
	//Ŭ���̾�Ʈ�� login ó�� ���� ��û�� ������ ���� �� ������ ������ ��������.
	
	//�̹� login�� ����
	if (clients[serial]->UserName.empty() == false) return;

	Packet_Login from_packet;
	from_packet.Deserialize(in);
	
	bool isDuplicated = false;

	std::unique_lock<std::mutex> ulName(clientNameLock);
	auto iterName = usedClientNames.find(from_packet.userName);
	if (iterName != usedClientNames.cend())
		isDuplicated = true;
	else
	{
		clients[serial]->UserName = from_packet.userName;
		usedClientNames.insert(std::make_pair(from_packet.userName, serial));
	}
	ulName.unlock();

	Packet_Login to_packet;
	to_packet.isCreated = !(isDuplicated);
	to_packet.userName = from_packet.userName;

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter stream(buf, sizeof(buf));
	to_packet.Serialize(stream);

	SendPacket(serial, stream.GetBuffer());

	//���̵� �����Ǿ� �α����ߴٸ�, ����ä������ ���� �� �����ؾ��Ѵ�.
	if (to_packet.isCreated)
	{
		ConnectToRandomPublicChannel(serial);
	}
}

void Framework::Process_ChannelList(Framework::SERIAL_TYPE serial, StreamReader&)
{
	if (IsValidClientSerial(serial) == false
		|| clients[serial]->IsConnect == false)
		return;

	Packet_Channel_List channelListPacket;
	size_t publicChannelCount = publicChannels.size();
	
	channelListPacket.publicChannelNames.clear();
	channelListPacket.publicChannelNames.resize(publicChannelCount);
	for (size_t i = 0; i < publicChannelCount; ++i)
	{
		channelListPacket.publicChannelNames[i] = publicChannels[i]->GetChannelName();
	}

	//Ŀ����ä���� ���� Ȯ�ν� lock�� �ϴ°��� ��Ȯ�� ����̳�
	//�̸��� Ȯ������ �ʰ� ��������� Ŀ����ä���� �ִ��� Ȯ���ϴ� ���̹Ƿ�, lock�� ���� �ʴ´�.
	channelListPacket.customChannelCount = 0;
	for (auto& customChannel : customChannels)
	{
		if (customChannel->IsCreated()) 
			channelListPacket.customChannelCount++;
	}	
	
	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter stream(buf, sizeof(buf));
	channelListPacket.Serialize(stream);

	SendPacket(serial, stream.GetBuffer());
}

void Framework::Process_Chatting(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false) return;

	auto& client = GetClient(serial);
	if (client.IsConnect == false) return;
	
	Packet_Chatting chatPacket;
	chatPacket.Deserialize(in);

	if (chatPacket.isWhisper == false)
	{
		//Ŭ���̾�Ʈ �� �ʿ� -> ä�� �̸� Ȯ��, ���
		std::unique_lock<std::mutex> ulClient(client.clientMutex);

		auto channel = FindChannelFromName(client.ChannelName);
		if (channel == nullptr) return;

		std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());
		BroadcastToChannel(channel, in.GetBuffer());
	}
	else
	{
		Framework::SERIAL_TYPE listenerSerial = FindClientSerialFromName(chatPacket.listener);
		if (SERIAL_ERROR != listenerSerial)
		{
			SendPacket(serial, in.GetBuffer());
			SendPacket(listenerSerial, in.GetBuffer());
		}
		else
			SendSystemMessage(serial, "***System*** �ش� ������ �������� �ƴմϴ�.");
	}
}

void Framework::Process_Kick(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false) return;
	if (clients[serial]->IsConnect == false) return;

	Packet_Kick_User kickPacket;
	kickPacket.Deserialize(in);

	Framework::SERIAL_TYPE targetSerial = FindClientSerialFromName(kickPacket.target);
	if (targetSerial == SERIAL_ERROR)
	{
		SendSystemMessage(serial, "***System*** �ش� ������ �������� �ƴմϴ�.");
		return;
	}

	auto& target = clients[targetSerial];
	//target �� �ʿ� -> ä�� �̸� Ȯ��, ���
	std::unique_lock<std::mutex> ulClient(target->clientMutex);

	auto channel = FindChannelFromName(kickPacket.channelName);
	if (channel == nullptr)
	{
		SendSystemMessage(serial, "***System*** ��û ó�� ����, �ٽ� �Է����ּ���.");
		return;
	}
	
	if (channel->GetChannelMaster() != kickPacket.kicker
		|| target->ChannelName != kickPacket.channelName)
	{
		SendSystemMessage(serial, "***System*** ������ �ƴϰų�, ���� ä���� �ƴմϴ�.");
		return;
	}

	if (target->Serial == serial)
	{
		SendSystemMessage(serial, "***System*** ������ ������ �� �����ϴ�.");
		return;
	}

	//�������� �� ���������� �̵���Ŵ
	HandleUserLeave(target->Serial, true, channel);
	ConnectToRandomPublicChannel(target->Serial);
}

void Framework::Process_ChannelChange(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false) return;

	auto& client = GetClient(serial);
	if (client.IsConnect == false) return;
	//Ŭ���̾�Ʈ �� �ʿ� -> ä�� Ȯ��, �̵�
	std::unique_lock<std::mutex> ulClient(client.clientMutex);

	Packet_Channel_Enter enterPacket;
	enterPacket.Deserialize(in);

	if (enterPacket.channelName == client.ChannelName
		|| enterPacket.channelName.empty()) return;

	CHANNEL_CONNECT isChannelChanged = CHANNEL_CONNECT::FAIL_ARGUMENT;
	auto channel = FindChannelFromName(enterPacket.channelName);
	auto prevChannel = FindChannelFromName(client.ChannelName);

	if (channel)
	{
		isChannelChanged = ConnectToChannel(serial, enterPacket.channelName);

		switch (isChannelChanged)
		{
		case CHANNEL_CONNECT::FAIL_ARGUMENT:
		case CHANNEL_CONNECT::FAIL_INVALID_CHANNEL:
			SendSystemMessage(serial, "***System*** ��û ó�� ����, �ٽ� �Է����ּ���.");
			break;
		case CHANNEL_CONNECT::FAIL_FULL:
			SendSystemMessage(serial, "***System*** ä���� �ִ� �����ο��� �ʰ��Ͽ����ϴ�.");
			break;
		default:
			break;
		}		
	}
	else //�� Ŀ����ä�� ����
	{
		AddNewCustomChannel(enterPacket.channelName);
		isChannelChanged = ConnectToChannel(serial, enterPacket.channelName);
	}

	if (isChannelChanged == CHANNEL_CONNECT::SUCCESS 
		&& (prevChannel != nullptr))
	{
		HandleUserLeave(serial, false, prevChannel);
	}
}


////////////////////////////////////////////////////////////////////////////
//Private Functions


//return integer SERIAL_ERROR, [0, MAX_PUBLIC_CHANNEL_COUNT)... 'SERIAL_ERROR' means public channels are busy
Framework::SERIAL_TYPE Framework::GetRandomPublicChannelSerial() const
{
	static thread_local std::default_random_engine dre;
	
	size_t publicChannelCount = publicChannels.size();	
	std::vector<unsigned int> slots(publicChannelCount);
	std::iota(slots.begin(), slots.end(), 0);

	for(auto i = 0; i < PUBLIC_BUSY_COUNT; ++i)
	{
		size_t toPickLength = publicChannelCount - i;
		std::uniform_int_distribution<size_t> uid(0, toPickLength - 1);
		size_t slot = uid(dre);

		if (publicChannels[slots[slot]]->GetUserCount() < MAX_CHANNEL_USERS)
			return slots[slot];
		else
			std::swap(slots[slot], slots[toPickLength - 1]);
	}

	return SERIAL_ERROR;
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

Framework::SERIAL_TYPE Framework::GetSerialForUseOverlapExp()
{
	using namespace std::chrono;
	SERIAL_TYPE overlapSerial = SERIAL_ERROR;

	auto beginTime = high_resolution_clock::now();
	while (false == validOverlapExpSerials.try_pop(overlapSerial))
	{
		auto elapsedTime = duration_cast<milliseconds>(high_resolution_clock::now() - beginTime);
		if (GETOVERLAP_TIMEOUT_MILLISECONDS <= elapsedTime.count())
			return SERIAL_ERROR;
	}

	return overlapSerial;
}

void Framework::BroadcastToChannel(const std::shared_ptr<Channel>& channel, const void* packet)
{
	if (channel == nullptr || packet == nullptr) return;

	for (const auto* client : channel->GetClientsInChannel())
		SendPacket(client->Serial, packet);
}

void Framework::HandleUserLeave(Framework::SERIAL_TYPE leaver, bool isKicked, const std::shared_ptr<Channel>& channel)
{
	if (IsValidClientSerial(leaver) == false) return;
	auto& client = GetClient(leaver);

	if (channel == nullptr)
	{
		std::cout << "HandleUserLeave() - channel is nullptr\n";
		return;
	}

	bool isMasterChanged = false;
	std::string channelName = channel->GetChannelName();

	std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());

	//���忡 ���� ������ ����� �� �ִ�.
	std::string beforeMasterName = channel->GetChannelMaster();
	channel->Exit(&client);	

	if (client.UserName == beforeMasterName)
	{	//�� ���ǹ��� ����Ǵ� ���� channel�� �ݵ�� CustomChannel �̴�.
		isMasterChanged = true;

		if (channel->GetUserCount() == 0)
		{	//ä�� �ı�
			std::unique_lock<std::mutex> ulCustom(customChannelsLock);
			validCustomChannelSerials.push(usedCustomChannelNames[channelName]);
			usedCustomChannelNames.erase(channelName);
			ulCustom.unlock();

			return; //�ļ�ó��(ä�ο� ���� �����鿡�� ���� ����)�� �ʿ����� �ʾ� return
		}
	}

	Packet_User_Leave leavePacket;
	leavePacket.isKicked = isKicked;
	leavePacket.userName = client.UserName;

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter leaveStream(buf, sizeof(buf));
	leavePacket.Serialize(leaveStream);

	if (isKicked && client.IsConnect)
		SendPacket(leaver, leaveStream.GetBuffer());

	BroadcastToChannel(channel, leaveStream.GetBuffer());

	if (isMasterChanged)
	{
		Packet_New_Master newMasterPacket;
		newMasterPacket.channelName = channelName;
		newMasterPacket.master = channel->GetChannelMaster();

		StreamWriter newMasterStream(buf, sizeof(buf));
		newMasterPacket.Serialize(newMasterStream);

		BroadcastToChannel(channel, newMasterStream.GetBuffer());
	}
}

void Framework::ConnectToRandomPublicChannel(Framework::SERIAL_TYPE serial)
{
	if (IsValidClientSerial(serial) == false
		|| clients[serial]->IsConnect == false)
		return;

	CHANNEL_CONNECT isConnected = CHANNEL_CONNECT::FAIL_ARGUMENT;
	while (isConnected != CHANNEL_CONNECT::SUCCESS)
	{
		Framework::SERIAL_TYPE randSlot = GetRandomPublicChannelSerial();
		if (randSlot == SERIAL_ERROR)
		{	//��� ����ä���� ȥ���� ���·�, ä�ο� �������� ���� ����
			//��Ÿũ����Ʈ1 ��Ʋ���� Void ä�ΰ� ����
			clients[serial]->ChannelName.clear();
			SendSystemMessage(serial, "***System*** ����ä���� ȥ���Ͽ� �������� ���߽��ϴ�.");
			return;
		}
		else
			isConnected = ConnectToChannel(serial, publicChannels[randSlot]->GetChannelName());
	}
}

Framework::CHANNEL_CONNECT 
	Framework::ConnectToChannel(Framework::SERIAL_TYPE serial, const std::string& channelName)
{
	if (IsValidClientSerial(serial) == false)
		return CHANNEL_CONNECT::FAIL_ARGUMENT;

	auto& client = GetClient(serial);
	if (client.IsConnect == false) return CHANNEL_CONNECT::FAIL_ARGUMENT;

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

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];

	//1. �� ������ �̵��ϰ��� �ϴ� ä�� ������ ��´�.
	Packet_Channel_Enter enterPacket;
	enterPacket.channelName = client.ChannelName;
	enterPacket.channelMaster = channel->GetChannelMaster();
	StreamWriter enterStream(buf, sizeof(buf));
	enterPacket.Serialize(enterStream);

	SendPacket(serial, enterStream.GetBuffer());

	//2. �����ϰ��� �ϴ� ä���� �����鿡�� �� ������ �˸��鼭 + �� �̸����� �����Ѵ�.
	Packet_Newface_Enter newfacePacket;
	newfacePacket.userName = client.UserName;
	
	Packet_Channel_Users usersPacket;
	usersPacket.channelName = client.ChannelName;
	auto userCountInChannel = channel->GetUserCount();
	usersPacket.userNames.clear();
	usersPacket.userNames.reserve(userCountInChannel);

	StreamWriter newfaceStream(buf, sizeof(buf));
	newfacePacket.Serialize(newfaceStream);

	for (const auto* clientInChannel : channel->GetClientsInChannel())
	{
		SendPacket(clientInChannel->Serial, newfaceStream.GetBuffer());
		usersPacket.userNames.push_back(clientInChannel->UserName);
	}
	
	//3. ���������� �� ������ ä�ο� �����ϴ� �������� ����Ʈ�� ��´�.
	StreamWriter usersStream(buf, sizeof(buf));
	usersPacket.Serialize(usersStream);
	SendPacket(serial, usersStream.GetBuffer());

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
	//�������� ������� �ʴ´�.
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

	return newSerial;
}

void Framework::ReturnUsedOverlapExp(SERIAL_TYPE serial)
{
	if (MAX_OVERLAPEXP_COUNT <= serial) return;

	validOverlapExpSerials.push(serial);
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
	{	//�ٸ� ������ ���� ä�� ����, �ø��� ��ȯ
		validCustomChannelSerials.push(customSerial);
		return;
	}

	usedCustomChannelNames.insert(std::make_pair(channelName, customSerial));
	customChannels[customSerial]->InitializeChannel(channelName);
}



std::vector<std::string>	
	Framework::DebugCustomChannels(bool doLock)
{
	std::unique_lock<std::mutex> ulCustom(customChannelsLock, std::defer_lock);
	if (doLock) ulCustom.lock();

	std::vector<std::string> customs;
	customs.reserve(usedCustomChannelNames.size());

	for (const auto& name : usedCustomChannelNames)
		customs.push_back(name.first);

	return customs;
}

size_t Framework::DebugUserCount(bool doLock)
{
	std::unique_lock<std::mutex> ulUser(clientNameLock, std::defer_lock);
	if (doLock) ulUser.lock();

	return usedClientNames.size();
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
		listenAddr.sin_port = htons(Packet_Base::PORT_NUMBER);

		::bind(acceptSocket, reinterpret_cast<sockaddr *>(&listenAddr), sizeof(listenAddr));
		::listen(acceptSocket, 100);

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

			//�� Ŭ���̾�Ʈ �ʱ�ȭ, ���
			Client& client = framework.GetClient(newSerial);

			client.IsConnect = true;
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
			unsigned long long serial;
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

				if ((client.PreviousCursor + iosize) > Packet_Base::MAX_BUF_SIZE)
				{
					int empty = Packet_Base::MAX_BUF_SIZE - client.PreviousCursor;
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
					{	//��������
						framework.ProcessPacket(serial, client.PacketBuff, client.PacketSize);
						std::memmove(client.PacketBuff, client.PacketBuff + client.PacketSize
							, client.PreviousCursor - client.PacketSize);

						client.PreviousCursor -= client.PacketSize;
						client.PacketSize = 0;

						if (remained > 0
							&& (client.PreviousCursor + remained) <= sizeof(client.PacketBuff))
						{
							std::memcpy(client.PacketBuff + client.PreviousCursor, ioBufCursor, remained);
							client.PreviousCursor += remained;
							remained = 0;
						}
					}
				} while (client.PacketSize == 0);

				

				DWORD flags = 0;
				::WSARecv(client.ClientSocket,
					&client.RecvOverlap.WsaBuf, 1, NULL, &flags,
					&client.RecvOverlap.Original_Overlap, NULL);
			}
			else if (OPERATION_SEND == overlapExp->Operation)
			{	//���ۿϷ�, overlap_exp ��ȯ
				//delete overlapExp;
				framework.ReturnUsedOverlapExp(overlapExp->Serial);
			}
			else
			{
				std::cout << "Unknown IOCP event!\n";
				return;
			}
		}
	}

} //unnamed namespace





