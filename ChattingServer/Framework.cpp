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

/**
	@brief		iocp �ʱ�ȭ, ���� �ʱ�ȭ, Initialize ȣ���� �����Ѵ�.
*/
Framework::Framework()
	: hIocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0))
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	isShutDown = !Initialize();
}


/**
	@brief		thread join, ���� ������ �����Ѵ�.
*/
Framework::~Framework()
{
	ShutDown();
}

/**
	@brief		���� �ڷ�� ��� ����, ���� �Է�, ���ν��� ���, ������ ������ �����Ѵ�.

	@return		bad alloc�� ���� ó������ �����ϸ�, �������� �ʰ� false�� ��ȯ�Ѵ�.
				��� ���������� �ʱ�ȭ �� true�� ��ȯ�Ѵ�.
*/
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

/**
	@brief		�����ӿ�ũ �Ҹ�� ȣ��, ������ ���� �� ���� ������ �����Ѵ�.
*/
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

/**
	@brief		��� Send ��û�� ���� ���� �ܰ� - WsaSend
	@details	������ ���� Overlap Ȯ�� ����ü�� concurrent queue�κ��� �ø����� �޾Ƽ� ����Ѵ�.
				
	@param serial �������� �ϴ� Ŭ���̾�Ʈ �ø���
	@param packet �������� �ϴ� ��Ŷ, Serialize �Ǿ��ִ�.

	@warning	concurrent queue�� try_pop�� �����Ǿ� timeout �� �� �ִ�.

	@todo		queue�� �� ���� �ʵ��� �������� �߰��ϴ� ���, Ȥ�� �޸� Ǯ�� ����
				Ȥ�� ������ ������ �׿� �´� ť�� ũ�� ������ ���� ����
*/
void Framework::SendPacket(Framework::SERIAL_TYPE serial, const void* packet)
{
	if (IsValidClientSerial(serial) == false
		|| packet == nullptr) return;
	
	auto& client =  clients[serial];
	if (client->IsConnect == false) return;

	SERIAL_TYPE overlapSerial = GetSerialForNewOne(SERIAL_GET::OVERLAPEXP);
	if (SERIAL_ERROR == overlapSerial)
	{
		std::cout << "SendPacket() - GetSerialForUseOverlapExp timeout\n";
		return;
	}
	
	::ZeroMemory(&(*sendOverlapExps[overlapSerial]), sizeof(Overlap_Exp));
	sendOverlapExps[overlapSerial]->Serial = overlapSerial;
	sendOverlapExps[overlapSerial]->Operation = Overlap_Exp::OPERATION_SEND;
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

/**
	@brief		���� ������, server message�� ������ �Լ� - Packet_System
*/
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

/**
	@brief		������ ��Ŷ���κ��� ���ν����� ȣ��
	@param size ��Ŷ ũ��, GetPacketSize() �� �� ������ ��Ŷ �����ܰ� Ȯ�������Ƿ� ���� �Ѱܹ޵��� ��

	@throw StreamReadUnderflow - ���ν��� �� ��Ŷ Deserialize���� �߻��� �� ����
*/
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

/**
	@brief		Ŭ���̾�Ʈ �� �����û�� ���� ó��
	@details	���� ��� ������ ���� broadcast, ä�� �ı⸦ �����ϰ�, closesocket ����, �ڷᱸ�� ����
				clientNameLock �� Ŭ���̾�Ʈ �ø��� ť ���� ����ȭ�Ǹ�
				��� ó���� �Ϸ�Ǹ� ����ߴ� Ŭ���̾�Ʈ �ø����� �ݳ��Ѵ�.
*/
void Framework::ProcessUserClose(Framework::SERIAL_TYPE serial)
{
	if (IsValidClientSerial(serial) == false) return;
	
	auto& client = GetClient(serial);
	if (client.IsConnect == false) return;
	//Ŭ���̾�Ʈ �� �ʿ� X -> clientNameLock�� validClientSerials ť�� ���� ����ȭ�ȴ�.
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

/**
	@brief		connect�� Ŭ���̾�Ʈ�� ù ��û - ���̵� ���� ó��
	@details	clientNameLock���� ����ȭ�Ͽ�, �ߺ����� �ʴ� ���̵��� �����ϰ� ���� �����Ѵ�.
				login���� ���� client�� �̸��� ��(empty) �̸��̴�.
				���������� �����Ǿ��ٸ� ����ä�η� �̵���Ų��.

	@throw StreamReadUnderflow - Packet_Login::Deserialize()���� �߻��� �� ����
*/
void Framework::Process_Login(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false
		|| clients[serial]->IsConnect == false) 
		return;
	//Ŭ���̾�Ʈ �� �ʿ� X -> ��Ŷ ó�� ���� ������, Ŭ���̾�Ʈ�� login ó�� ���� ��û�� ������ ���� �� ������ ������ ��������.
	
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

/**
	@brief		����ä�� ����Ʈ �� Ŀ����ä�� ���� ��û�� ���� ó��
	@details	����ä���� �Һ��̹Ƿ� ����ȭ���� �̸��� �����ϰ�
				Ŀ����ä���� ����������, �������� Ȯ���ϹǷ� lock ���� �����Ѵ�.
				Ŀ����ä���� Ȱ��ȭ���δ�, Initialize �Ǿ�����, �� �̸��� �����ϴ����� Ȯ���Ѵ�.

	@warning	Ŀ���� ä���� ������ ��Ȯ�� ��ġ�� �ƴϴ�.
	@author		cgHan
*/
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
		if (customChannel->GetChannelName().empty() == false) 
			channelListPacket.customChannelCount++;
	}	
	
	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter stream(buf, sizeof(buf));
	channelListPacket.Serialize(stream);

	SendPacket(serial, stream.GetBuffer());
}

/**
	@brief		ä�� �� ��üä�� Ȥ�� �ӼӸ� ��û�� ���� ó��
	@details	��üä���� ���, Ŭ���̾�Ʈ�� ä���� ģ ä���� ���濡 ���� ����ȭ�Ǿ�� �ϹǷ�
				Ŭ���̾�Ʈ ���� �����Ͽ� �ش� ä�ο� ���޵ǵ��� �ۼ��Ǿ���.
				
				�ӼӸ��� ��� Ŭ���̾�Ʈ ���� �ʿ����� �����Ƿ� listener�� ��ȿ�� �˻縦 ������ ��
				�ѿ��� �ٽ� �����ֵ��� �Ѵ�.

	@warning	��üä���� ��� clientLock -> channelLock �� �ΰ��� ����� �����Ƿ� ������ �Ű澲���� �Ѵ�.
				�ٸ� ó���Լ����� �� ������ �������� �Ѵ�.

	@throw StreamReadUnderflow - Packet_Chatting::Deserialize()���� �߻��� �� ����
*/
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

/**
	@brief		������ ������ ���� ��û�������� ���� ó��
	@details	��û�� kicker�� �ƴ� ������� target�� ��� �� ���� ��û�� ��ȿ���� Ȯ���Ѵ�.
				
				�̸� ���� ������ ���� ��Ȳ �� ������ ������ �����Ѵ�.
					A�� B�� �����ϰ��� �ϴ� ó���� B�� �ٸ� ä�η� �̵��Ϸ��� ó���� ���ÿ� �߻� ��
					��û�� ��ȿ�� ���� Ȯ���� �� ���� ó���ϱ� �� B�� ä���� �ٲٸ�, B�� �ٲ� ä�ο��� ������Ѵ�.

				��û�� ��ȿ�ϴٸ�, target�� ä�ηκ��� �������� ����ä�η� �̵���Ų��.

	@warning	kicker�� ��� ������ ������ ��ż� �ȵȴ�. ����� �߻��� �����̴�.

	@throw StreamReadUnderflow - Packet_Kick_User::Deserialize()���� �߻��� �� ����
*/
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

/**
	@brief		ä�� �̵� ��û�� ���� ó��
	@details	�̵��ϰ��� �ϴ� ä�� �̸��� �����ϸ� �̵��̰�, ������ Ŀ����ä�� �����̴�.
				�����Ѵٸ�, ���� ä�ο��� ������ ó���� �� ä�ο� ���� ó���� �����ϰ�
				���ٸ�, Ŀ����ä���� �����ϰ� ���� ä�ο��� ������ ó���� ������ ä�ο� ����.
				���� ä���� void��� ������ ó���� �������� �ʴ´�.

	@warning	clientLock�� ConnectToChannel()���� channelLock �� �� ���� ����� �߻��Ѵ�.

	@throw StreamReadUnderflow - Packet_Channel_Enter::Deserialize()���� �߻��� �� ����
*/
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


/**
	@brief		����ä�� �� ������ �� �ִ� ������ ä���� �ε����� ��´�.
	@details	std::default_random_engine���� ���� �߻�, Knuth Shuffle�� ����Ͽ� �ۼ��Ǿ���.
				�� PUBLIC_BUSY_COUNT ���� �ߺ����� ����(ä�� �ε���) Ȯ�� ��, �װ��� ���� á�ٸ� ȥ������ �����Ѵ�.

	@return		[0, MAX_PUBLIC_CHANNEL_COUNT)�� SERIAL_ERROR�� ��ȯ�Ѵ�.
				SERIAL_ERROR�� ����ä���� ȥ���� ������ �Ǵ�, voidä�η� �����ϵ��� �Ѵ�.

	@see		https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
*/
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

/**
	@brief		�ش� ä�ο� ��� �����鿡�� ��Ŷ�� �����Ѵ�.

	@warning	�ش� ä�ο� ���� ����� ȣ�� ���� ����Ǿ�� �Ѵ�.
*/
void Framework::BroadcastToChannel(const std::shared_ptr<Channel>& channel, const void* packet)
{
	if (channel == nullptr || packet == nullptr) return;

	for (const auto* client : channel->GetClientsInChannel())
		SendPacket(client->Serial, packet);
}

/**
	@brief		�ش� ������ ä�ο��� �����Ű��, ä�ο� ���� �����鿡�� �˸���.
	@details	channelLock�� ����Ǹ�, ä�� ���忡 ���� Ŀ����ä���� Close�Ǵ� ��� customChannelLock�� �����Ѵ�.
				leaver�� ������ �˸���, ������ �ٲ� ��� �̿� ���� �˸� ���� �����Ѵ�.
				������� ���, leaver ���ο��Ե� �˸���.

	@param isKicked ���� ���� ä���� �������� �����̴�.

	@warning	channelLock �� customChannelLock �� �� ���� ��ݿ� �����Ѵ�.
*/
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
		{	//ä���� �ı�Ǿ���.
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

/**
	@brief		������ ����ä�η� ������ �̵���Ų��.
	@details	ä���� ȥ���� ���, Ŭ���̾�Ʈ�� ä���� clear�ϰ� Void ä�η� ���

	@warning	clientLock�� ����ǰ� ȣ��Ǿ�� �Ѵ�. 
				���ܷ�, ó�� �α��� �ܰ迡���� ȣ���� ������ �������Ƿ� �������� �ʴ´�.
*/
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

/**
	@brief		������ �ش� ä�η� �����Ѵ�.
	@details	channelLock ���� �� ������ �����ϸ�, ���� ����� ������ ä�� ������ ��������Ʈ��
				ä�ο� �ִ� ������ ���ο� ���� ������ �ް� �ȴ�.

	@return		CHANNEL_CONNECT ������ Ÿ���� ��ȯ�Ѵ�.
				���д� �μ��� ��ȿ���� �ʰų�, ã������ ä���� ���ų�, �����Ϸ��� ä���� ������ ����̰�
				������ ���������� ä�� �̵��ϰ� �׿� ���� ó���� �Ϸ�Ǵ� ����̴�.

	@warning	clientLock�� ����Ǿ�� �Ѵ�.
				clientLock�� channelLock �� ���� �����Ƿ� �����Ѵ�.
*/
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

/**
	@brief		�̸����κ��� Ŭ���̾�Ʈ�� �ø����� ��´�.
	@details	clientNameLock�� ���� ����ȭ�Ѵ�.

	@return		SERIAL_ERROR �� �ߺ��� �̸��� ����̸�
				�� �ܿ� ���� �̸��� ��ġ�ϴ� Ŭ���̾�Ʈ�� �ø����̴�.
*/
Framework::SERIAL_TYPE Framework::FindClientSerialFromName(const std::string& clientName)
{
	std::unique_lock<std::mutex> ulName(clientNameLock);

	auto iterName = usedClientNames.find(clientName);
	if (iterName != usedClientNames.cend())
		return iterName->second;
	else
		return SERIAL_ERROR;
}

/**
	@brief		ä�� �̸����κ��� �ش�Ǵ� ä���� shared_ptr�� ��´�.
	@details	�������� �Һ����� ��ݾ��� �̸��� ��ġ�ϴ��� �˻縦 �����ϸ�
				Ŀ����ä���� ��� ���� �̸��� ���Ѵ�.
				
	@return		nullptr ��ȯ -> ��ġ�ϴ� �̸��� ä���� ���� ���
				��ȿ�� �� ��ȯ -> �ش� ä���� '�������� ��' �������̴�.
*/
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

/**
	@brief		concurrent queue�κ��� type�� �´�, ��밡���� �ø��� ���� ��´�.

	@param	type enum class�̸� Ŭ���̾�Ʈ, Ŀ����ä��, OverlapȮ�� ����ü �� �ϳ��̴�.

	@return		������ TIMEOUT ���� try_pop ���� �� ��ȿ�� Serial ���� ��´�.
				TIMEOUT�Ǹ�, SERIAL_ERROR ��ȯ
*/
Framework::SERIAL_TYPE Framework::GetSerialForNewOne(SERIAL_GET type)
{
	SERIAL_TYPE retSerial = SERIAL_ERROR;
	unsigned int timeout_milliseconds = 0;
	Serial_ConcurrentQueue* serialQueue = nullptr;

	switch (type)
	{
	case SERIAL_GET::CLIENT:
		timeout_milliseconds = NEWBIESERIAL_TIMEOUT_MILLISECONDS;
		serialQueue = &validClientSerials;
		break;
	case SERIAL_GET::CUSTOMCHANNEL:
		timeout_milliseconds = MAKECUSTOM_TIMEOUT_MILLISECONDS;
		serialQueue = &validCustomChannelSerials;
		break;
	case SERIAL_GET::OVERLAPEXP:
		timeout_milliseconds = GETOVERLAP_TIMEOUT_MILLISECONDS;
		serialQueue = &validOverlapExpSerials;
		break;
	default:
		std::cout << "GetSerialForNewOne() - invalid type\n";
		return retSerial;	//SERIAL_ERROR
	}

	using namespace std::chrono;

	auto beginTime = high_resolution_clock::now();
	while (false == serialQueue->try_pop(retSerial))
	{
		auto elapsedTime = duration_cast<milliseconds>(high_resolution_clock::now() - beginTime);
		if (timeout_milliseconds <= elapsedTime.count())
			return SERIAL_ERROR;
	}

	return retSerial;
}

/**
	@brief		����� OverlapExp�� ��ȯ�Ѵ�. WSASend�� �Ϸ�� �������� WorkerThread�� ���� ȣ��ȴ�.
*/
void Framework::ReturnUsedOverlapExp(SERIAL_TYPE serial)
{
	if (MAX_OVERLAPEXP_COUNT <= serial) return;
	validOverlapExpSerials.push(serial);
}

/**
	@brief		���ο� Ŀ����ä���� �����Ѵ�.
	@details	concurrent queue�κ��� �� Ŀ����ä�� �ε����� ��´�.
				���������� ����ٸ�, ��ü Ŀ����ä���� ��װ� ������� Ŀ����ä�� �̸��� ����Ѵ�.
				
				AddNewCustomChannel() ȣ�� ��ü�� ����ȭ ���� �����Ƿ� �� �̻��� ������
				���� �̸��� Ŀ����ä���� �����Ϸ� �� �� �ִ�. �㳪 ���� ����� ��� ������ ������ ó���Ǹ�
				�ʰ� ó���Ǵ� ������ �ø����� ��ȯ�ϰ� �� ä�η� �̵��ϰԵȴ�.(Process_ChannelChange() ����)

	@see		Framework::Process_ChannelChange()
	@warning	��Ŀ��Ʈ ť�� try_pop�� ��ü�� �� �ִ�.
				�㳪, MAX_CLIENT_COUNT�� MAX_CUSTOM_COUNT�� �����Ƿ� ����� �׷��� �ʴ�.

	@author		cgHan
*/
void Framework::AddNewCustomChannel(const std::string& channelName)
{
	auto customSerial = GetSerialForNewOne(SERIAL_GET::CUSTOMCHANNEL);
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


/**
	@brief		Ŀ����ä���� �̸��� ��´�.

	@param doLock	����� �����Ͽ� ��Ȯ�� ������ �������� ���� �����̴�.

	@return ���� �ʱ�ȭ�� ��� Ŀ����ä���� �̸��� ����ִ�.
*/
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

/**
	@brief	�� �������� ������ ���� ��´�.

	@param doLock	����� �����Ͽ� ��Ȯ�� ������ �������� ���� �����̴�.
*/
size_t Framework::DebugUserCount(bool doLock)
{
	std::unique_lock<std::mutex> ulUser(clientNameLock, std::defer_lock);
	if (doLock) ulUser.lock();

	return usedClientNames.size();
}


namespace
{
	/**
		@brief		acceptThread �����Լ�, Ŭ���̾�Ʈ connect�� ó���Ѵ�.
		@details	acceptThread�� ���Ͻ������̸� select �𵨷� �����Ѵ�.
					���� �����û ó���� ���� Ÿ�Ӿƿ��� �ִ�. (Framework::ACCEPT_TIMEOUT_SECONDS)
					�����ӿ�ũ Ŭ���̾�Ʈ �ø��� concurrent queue���� �ø����� �޾� ����Ѵ�.
					���� Ŭ���̾�Ʈ �ʱ�ȭ, iocp ���, WSARecv ����� �����ϸ� �������Ѵ�.

		@see		Framework::GetSerialForNewClient()

		@warning	���� ������ ó���� �ø��� ��Ŀ��Ʈ ť�� Ÿ�Ӿƿ� (NEWBIESERIAL_TIMEOUT_MILLISECONDS), 
					�� ť�� ȥ�⵵�� ���� ó���ǰ� �־� MAX_CLIENT_COUNT�� �ؼ����� ������ �����Ѵ�.
	*/
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

			Framework::SERIAL_TYPE newSerial = framework.GetSerialForNewOne(Framework::SERIAL_GET::CLIENT);
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
			client.RecvOverlap.Operation = Overlap_Exp::OPERATION_RECV;
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

	/**
		@brief		�� �۾��������� �����Լ��̴�.
		@details	iocp�� �����Ѵ�. overlap ����ü�� Ȯ���Ͽ� ����ϸ�(Overlap_Exp), key�� Ŭ���̾�Ʈ �ø����̴�.
					Overlap_Exp�� OPERATION ���������� �̺�Ʈ�� �����Ͽ� ó���Ѵ�.
					recv�� ��Ŷ�� �����ϰ�, �ش� ���ν����� ȣ���� ��, �ٽ� recv�� ����Ѵ�.
					send�� ����ߴ� Overlap_Exp�� ��ȯ�ϴ� �۾��� �����Ѵ�.

					���� �����û�� ó���ϱ� ���� Ÿ�Ӿƿ��� �ξ���(GQCS_TIMEOUT_MILLISECONDS)

		@todo		��Ŷ �����ڵ�� �� ���� ������ ����� ã�Ƴ������� �����ϰ� �׽�Ʈ�ؾ� �Ѵ�.
		@author		cgHan
	*/
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

			if (Overlap_Exp::OPERATION_RECV == overlapExp->Operation)
			{
				Client& client = framework.GetClient(serial);
				unsigned char* ioBufCursor = overlapExp->Iocp_Buffer;
				int remained = 0;

				if ((client.SavedPacketSize + iosize) > Packet_Base::MAX_BUF_SIZE)
				{
					int empty = Packet_Base::MAX_BUF_SIZE - client.SavedPacketSize;
					std::memcpy(client.PacketBuff + client.SavedPacketSize, ioBufCursor, empty);

					remained = iosize - empty;
					ioBufCursor += empty;
					client.SavedPacketSize += empty;
				}
				else
				{
					std::memcpy(client.PacketBuff + client.SavedPacketSize, ioBufCursor, iosize);
					client.SavedPacketSize += iosize;
				}

				do
				{
					client.PacketSize = GetPacketSize(client.PacketBuff);

					if (client.PacketSize <= client.SavedPacketSize)
					{	//��������
						framework.ProcessPacket(serial, client.PacketBuff, client.PacketSize);
						std::memmove(client.PacketBuff, client.PacketBuff + client.PacketSize
							, client.SavedPacketSize - client.PacketSize);

						client.SavedPacketSize -= client.PacketSize;
						client.PacketSize = 0;

						if (remained > 0
							&& (client.SavedPacketSize + remained) <= sizeof(client.PacketBuff))
						{
							std::memcpy(client.PacketBuff + client.SavedPacketSize, ioBufCursor, remained);
							client.SavedPacketSize += remained;
							remained = 0;
						}
					}
				} while (client.PacketSize == 0);

				DWORD flags = 0;
				::WSARecv(client.ClientSocket,
					&client.RecvOverlap.WsaBuf, 1, NULL, &flags,
					&client.RecvOverlap.Original_Overlap, NULL);
			}
			else if (Overlap_Exp::OPERATION_SEND == overlapExp->Operation)
			{	//���ۿϷ�, overlap_exp ��ȯ
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





