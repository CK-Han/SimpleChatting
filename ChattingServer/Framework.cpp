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
	, clients(MAX_CLIENT_COUNT, Client())
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	Initialize();
}


Framework::~Framework()
{
	for (auto& th : workerThreads)
		th->join();
	acceptThread->join();

	::WSACleanup();
}


void Framework::Initialize()
{
	//MAX_PUBLIC_CHANNEL_COUNT
	publicChannels.emplace_back("FreeChannel");
	publicChannels.emplace_back("ForTeenagers");
	publicChannels.emplace_back("For20s");
	publicChannels.emplace_back("For3040s");
	publicChannels.emplace_back("AboutGame");
	publicChannels.emplace_back("AboutStudy");
	publicChannels.emplace_back("AboutHobby");
	publicChannels.emplace_back("AboutExcercise");

	publicChannels.emplace_back("Sample1");
	publicChannels.emplace_back("Sample2");
	publicChannels.emplace_back("Sample3");
	publicChannels.emplace_back("Sample4");
	publicChannels.emplace_back("Sample5");
	publicChannels.emplace_back("Sample6");
	publicChannels.emplace_back("Sample7");
	publicChannels.emplace_back("Sample8");
	
	for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
		workerThreads.emplace_back(new std::thread(WorkerThreadStart));

	acceptThread = std::unique_ptr<std::thread>(new std::thread(AcceptThreadStart));
}
	 

////////////////////////////////////////////////////////////////////////////
//Send to Client
void Framework::SendPacket(int serial, unsigned char* packet) const
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;
	
	auto& client = GetClient(serial);
	if (client.IsLogin == false) return;

	Overlap_Exp* overlapExp = new Overlap_Exp;
	::ZeroMemory(overlapExp, sizeof(Overlap_Exp));

	overlapExp->Operation = OPERATION_SEND;
	overlapExp->WsaBuf.buf = reinterpret_cast<CHAR *>(overlapExp->Iocp_Buffer);
	overlapExp->WsaBuf.len = GetPacketSize(packet);
	memcpy(overlapExp->Iocp_Buffer, packet, overlapExp->WsaBuf.len);

	int ret = ::WSASend(client.ClientSocket, &overlapExp->WsaBuf, 1, NULL, 0,
		&overlapExp->Original_Overlap, NULL);

	if (0 != ret) 
	{
		int error_no = ::WSAGetLastError();
		if (WSA_IO_PENDING != error_no 
			&& WSAECONNRESET != error_no)
			std::cout << "SendPacket::WSASend Error : " <<  error_no << std::endl;
	}
}

void Framework::SendSystemMessage(int serial, const std::string& msg) const
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return;
	if (GetClient(serial).IsLogin == false) return;

	packet_system to_packet;
	::ZeroMemory(&to_packet, sizeof(to_packet));
	to_packet.Size = sizeof(to_packet);
	to_packet.Type = PACKET_SYSTEM;
	std::memcpy(&to_packet.SystemMessage, msg.c_str(), msg.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(&to_packet));
}

////////////////////////////////////////////////////////////////////////////
//Received from Client
void Framework::ProcessPacket(int serial, unsigned char* packet)
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


void Framework::ProcessUserClose(int serial)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return;
	auto& client = GetClient(serial);
	if (client.IsLogin == false) return;

	if (client.ChannelName.empty() == false)
	{	//ä���� �����Ѵ� -> ä�ο� �����ϴ� �����鿡�� �� ������ ������ �˸���.
		Channel* channel = FindChannelFromName(client.ChannelName);
		if(channel)
			HandleUserLeave(serial, false, channel);
	}
	::closesocket(client.ClientSocket);

	std::unique_lock<std::mutex> ulLogin(loginLock);
	client.IsLogin = false;
	client.UserName.clear();
	client.ChannelName.clear();
}

void Framework::ProcessLogin(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	auto& client = GetClient(serial);
	if (client.IsLogin == false) return;

	packet_login* from_packet = reinterpret_cast<packet_login*>(packet);
	bool isDuplicated = false;

	std::unique_lock<std::mutex> ulName(clientNameLock);
	if (SERIAL_ERROR != FindClientSerialFromName(from_packet->User))
		isDuplicated = true;

	if(isDuplicated == false) //���� �� �� ����
		client.UserName = from_packet->User;
	ulName.unlock();

	packet_login to_packet;
	::ZeroMemory(&to_packet, sizeof(to_packet));
	to_packet.Size = sizeof(packet_login);
	to_packet.Type = PACKET_LOGIN;
	to_packet.Created = !(isDuplicated);
	std::memcpy(&to_packet.User, from_packet->User, MAX_USERNAME_LENGTH);

	SendPacket(serial, reinterpret_cast<unsigned char *>(&to_packet));

	//���̵� �����Ǿ� �α����ߴٸ�, ����ä������ ���� �� �����ؾ��Ѵ�.
	if (to_packet.Created)
	{
		ConnectToRandomPublicChannel(serial);
	}
}

void Framework::ProcessChannelList(int serial)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return;
	if (GetClient(serial).IsLogin == false) return;

	packet_channel_list channelList_packet;
	::ZeroMemory(&channelList_packet, sizeof(channelList_packet));
	//����ä���� �Һ����� ���� �ʿ�ġ ������ Ŀ����ä���� �������� ���� Ȯ�ν� ���� �ʿ��� ���̴�.
	channelList_packet.Size = sizeof(packet_channel_list);
	channelList_packet.Type = PACKET_CHANNEL_LIST;
	channelList_packet.PublicChannelCount = publicChannels.size();
	std::string publicChannelNames;
	for (auto& ch : publicChannels)
	{
		publicChannelNames += ch.GetChannelName() + NAME_DELIMITER;
	}
	std::memcpy(&channelList_packet.PublicChannelNames, publicChannelNames.c_str(), publicChannelNames.size());
	
	std::unique_lock<std::mutex> ulCustom(customChannelsLock);
	channelList_packet.CustomChannelCount = customChannels.size();
	ulCustom.unlock();
	
	SendPacket(serial, reinterpret_cast<unsigned char *>(&channelList_packet));
}

void Framework::ProcessChatting(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	auto& client = GetClient(serial);
	if (client.IsLogin == false) return;

	packet_chatting* from_packet = reinterpret_cast<packet_chatting*>(packet);
	
	if (from_packet->IsWhisper == false)
	{
		BroadcastToChannel(client.ChannelName, packet);
	}
	else
	{
		int listnerSerial = FindClientSerialFromName(from_packet->Listner);
		if (SERIAL_ERROR != listnerSerial)
		{
			SendPacket(serial, packet);
			SendPacket(listnerSerial, packet);
		}
		else
			SendSystemMessage(serial, "***System*** �ش� ������ �������� �ƴմϴ�.");
	}
}

void Framework::ProcessKick(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	if (GetClient(serial).IsLogin == false) return;

	packet_kick_user* from_packet = reinterpret_cast<packet_kick_user*>(packet);
	
	int targetSerial = FindClientSerialFromName(from_packet->Target);
	if (targetSerial == SERIAL_ERROR)
	{
		SendSystemMessage(serial, "***System*** �ش� ������ �������� �ƴմϴ�.");
		return;
	}
	auto& target = GetClient(targetSerial);

	Channel* channel = FindChannelFromName(from_packet->Channel);
	if (channel == nullptr)
	{
		SendSystemMessage(serial, "***System*** ��û�� ������ �߻��Ͽ����ϴ�.");

		std::cout << "ProcessKick() - cannot find channel\n";
		return;
	}
	
	if (channel->GetChannelMaster() != from_packet->Kicker
		|| target.ChannelName != from_packet->Channel)
	{
		SendSystemMessage(serial, "***System*** ������ �ƴϰų�, ���� ä���� �ƴմϴ�.");
		return;
	}

	if (target.Serial == serial)
	{
		SendSystemMessage(serial, "***System*** ������ ������ �� �����ϴ�.");
		return;
	}

	//�������� �� ���������� �̵���Ŵ
	HandleUserLeave(target.Serial, true, channel);
	ConnectToRandomPublicChannel(target.Serial);
}

void Framework::ProcessChannelChange(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial
		|| packet == nullptr) return;

	auto& client = GetClient(serial);
	if (client.IsLogin == false) return;

	packet_channel_enter* from_packet = reinterpret_cast<packet_channel_enter*>(packet);
	

	bool isChannelChanged = false;
	Channel* channel = FindChannelFromName(from_packet->ChannelName);
	Channel* prevChannel = FindChannelFromName(client.ChannelName);

	if (channel)
	{
		isChannelChanged = ConnectToChannel(serial, from_packet->ChannelName);
		if (isChannelChanged == false)
			SendSystemMessage(serial, "***System*** ä���� �ִ� �����ο��� �ʰ��Ͽ����ϴ�.");
	}
	else //�� Ŀ����ä�� ����
	{
		AddNewCustomChannel(from_packet->ChannelName);
		ConnectToChannel(serial, from_packet->ChannelName);
		isChannelChanged = true;
	}

	if (isChannelChanged && prevChannel)
	{
		HandleUserLeave(serial, false, prevChannel);
	}
}


////////////////////////////////////////////////////////////////////////////
//Private Functions


//return [-1, MAX_PUBLIC_CHANNEL_COUNT), '-1' means public channels are full
int Framework::GetRandomPublicChannelIndex() const
{
	const unsigned int publicChannelCount = publicChannels.size();
	std::uniform_int_distribution<int> uid(0, publicChannelCount - 1);
	static std::default_random_engine dre;

	std::vector<bool> channelIsFull(publicChannelCount, false);
	int randSlot = -1;
	size_t fullChannelCount = 0;

	while (true)
	{
		randSlot = uid(dre);
		if (publicChannels[randSlot].GetUserCount() < MAX_CHANNEL_USERS)
			return randSlot;
		else if(channelIsFull[randSlot] == false)
		{
			channelIsFull[randSlot] = true;
			++fullChannelCount;

			if (fullChannelCount == publicChannelCount)
				return -1;
		}
	}
}

void Framework::BroadcastToChannel(const std::string& channelName, unsigned char* packet)
{
	if (packet == nullptr) return;
	Channel* channel = FindChannelFromName(channelName);

	if (channel)
	{
		std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());

		for (auto* client : channel->GetClientsInChannel())
			SendPacket(client->Serial, packet);
	}
}

void Framework::HandleUserLeave(int leaver, bool isKicked, Channel* channel)
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
	channel->Exit(&client);

	if (client.UserName == channel->GetChannelMaster())
	{	//������ ������ ���, �� Ŀ����ä���� ��� (dynamic_cast�� �ʿ����!)
		std::unique_lock<std::mutex> ulCustom(customChannelsLock);

		auto toDeleteChannelIter = std::find_if(customChannels.cbegin(), customChannels.cend(),
			[&channelName](const CustomChannel& ch)
		{
			return ch.GetChannelName() == channelName;
		});

		if (toDeleteChannelIter != customChannels.cend())
		{
			if (channel->GetUserCount() == 0)
			{
				customChannels.erase(toDeleteChannelIter);
				return; //ä���� �������Ƿ�, �ļ�ó��(���� �����鿡�� ���� ����)�� �ʿ����� �ʾ� return
			}
			else
			{	//����Ʈ���� ���� ���� ���� �������� ������ �ѱ��.
				std::string newMaster = channel->GetClientsInChannel().front()->UserName;
				channel->SetChannelMaster(newMaster);
				isMasterChanged = true;
			}
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
		SendSystemMessage(leaver, "***System*** ���忡 ���� ������Ͽ����ϴ�. ����ä�η� �̵��մϴ�.");

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

void Framework::ConnectToRandomPublicChannel(int serial)
{
	bool isConnected = false;
	while (isConnected == false)
	{
		int randSlot = GetRandomPublicChannelIndex();
		if (randSlot == -1)
		{	//��� ����ä���� ���� �� ����, ��Ÿũ����Ʈ1 ��Ʋ���� Void ä�ΰ� ����
			SendSystemMessage(serial, "***System*** ��� ����ä�ο� �ο��� ���� á���ϴ�.");
			std::cout << "ä�ο���\n";
			return;
		}
		else
			isConnected = ConnectToChannel(serial, publicChannels[randSlot].GetChannelName());
	}
}

bool Framework::ConnectToChannel(int serial, const std::string& channelName)
{
	if (serial < 0 || MAX_CLIENT_COUNT <= serial) return false;

	auto& client = GetClient(serial);
	if (client.IsLogin == false) return false;

	Channel* channel = FindChannelFromName(channelName);
	if (channel == nullptr)
	{
		std::cout << "ConnectToChannel() - cannot find channel\n";
		return false;
	}

	std::unique_lock<std::mutex> ulChannel(channel->GetChannelLock());

	if (channel->GetUserCount() < MAX_CHANNEL_USERS)
	{
		channel->Enter(&client);
		client.ChannelName = channelName;
	}
	else
		return false;

	packet_newface_enter newface_packet;
	::ZeroMemory(&newface_packet, sizeof(newface_packet));
	newface_packet.Size = sizeof(packet_newface_enter);
	newface_packet.Type = PACKET_NEWFACE_ENTER;
	std::memcpy(&newface_packet.User, client.UserName.c_str(), client.UserName.size());

	//1. �����ϰ��� �ϴ� ä���� �����鿡�� �� ������ �˸��鼭 �̸��� �����Ѵ�.
	std::string userNames;
	unsigned int userCount = 0;
	for (auto* clientInChannel : channel->GetClientsInChannel())
	{
		SendPacket(clientInChannel->Serial, reinterpret_cast<unsigned char*>(&newface_packet));
		userNames += clientInChannel->UserName + NAME_DELIMITER;
		++userCount;
	}
	ulChannel.unlock();

	//2. �� ������ �̵��ϰ��� �ϴ� ä�� ���� �� ä�ο� �����ϰ��ִ� ���� ������ ��´�.
	packet_channel_enter enter_packet;
	::ZeroMemory(&enter_packet, sizeof(enter_packet));
	enter_packet.Size = sizeof(enter_packet);
	enter_packet.Type = PACKET_CHANNEL_ENTER;
	std::memcpy(&enter_packet.ChannelName, client.ChannelName.c_str(), client.ChannelName.size());
	std::memcpy(&enter_packet.ChannelMaster, channel->GetChannelMaster().c_str(), channel->GetChannelMaster().size());

	SendPacket(serial, reinterpret_cast<unsigned char *>(&enter_packet));

	packet_channel_users users_packet;
	::ZeroMemory(&users_packet, sizeof(users_packet));
	users_packet.Size = sizeof(users_packet.Size) + sizeof(users_packet.Type) + sizeof(users_packet.UserCountInPacket)
		+ sizeof(users_packet.ChannelName) + static_cast<unsigned short>(userNames.size());
	users_packet.Type = PACKET_CHANNEL_USERS;
	std::memcpy(&users_packet.ChannelName, client.ChannelName.c_str(), client.ChannelName.size());
	users_packet.UserCountInPacket = userCount;
	std::memcpy(&users_packet.UserNames, userNames.c_str(), userNames.size());

	SendPacket(serial, reinterpret_cast<unsigned char *>(&users_packet));

	return true;
}

int Framework::FindClientSerialFromName(const std::string& clientName)
{
	for (int i = 0; i < MAX_CLIENT_COUNT; ++i)
	{
		if (clients[i].IsLogin == false) continue;
		if (clients[i].UserName == clientName)
			return i;
	}

	return SERIAL_ERROR;
}

Channel* Framework::FindChannelFromName(const std::string& channelName)
{
	Channel* channel = nullptr;
	//�������� ������� �ʴ´�.
	for (auto& ch : publicChannels)
	{
		if (channelName == ch.GetChannelName())
		{
			channel = &ch;
			return channel;
		}
	}

	std::unique_lock<std::mutex> ulCustom(customChannelsLock);
	for (auto& ch : customChannels)
	{
		if (channelName == ch.GetChannelName())
		{
			channel = &ch;
			return channel;
		}
	}

	return channel;
}

int Framework::GetSeirialForNewClient()
{
	std::unique_lock<std::mutex> ulLogin(loginLock);
	for (int i = 0; i < MAX_CLIENT_COUNT; ++i)
	{
		if (clients[i].IsLogin == true) continue;
		
		clients[i].IsLogin = true;
		return i;
	}

	return SERIAL_ERROR;
}


void Framework::AddNewCustomChannel(const std::string& channelName)
{
	std::unique_lock<std::mutex> ulCustom(customChannelsLock);

	auto iter = std::find_if(customChannels.cbegin(), customChannels.cend(),
		[&channelName](const CustomChannel& cc) 
	{
		return cc.GetChannelName() == channelName;
	});

	if (iter == customChannels.cend())
		customChannels.emplace_back(channelName);
}



namespace
{
	void AcceptThreadStart()
	{
		Framework* framework = Framework::GetInstance();
		SOCKADDR_IN listenAddr;

		SOCKET acceptSocket = ::WSASocket(AF_INET, SOCK_STREAM,
			IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

		std::memset(&listenAddr, 0, sizeof(listenAddr));
		listenAddr.sin_family = AF_INET;
		listenAddr.sin_addr.s_addr = htonl(ADDR_ANY);
		listenAddr.sin_port = htons(MY_SERVER_PORT);

		::bind(acceptSocket, reinterpret_cast<sockaddr *>(&listenAddr), sizeof(listenAddr));
		::listen(acceptSocket, 10);

		while (false == framework->IsShutDown())
		{
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

			int newSerial = framework->GetSeirialForNewClient();
			if (newSerial == Framework::SERIAL_ERROR)
			{
				::closesocket(newClientSocket);
				std::cout << "AcceptThreadStart() - WSAAccept " << ": too much people\n";
				continue;
			}

			//�� Ŭ���̾�Ʈ ����, �ڷᱸ���� ����
			Client& client = framework->GetClient(newSerial);

			client.Serial = newSerial;
			client.ClientSocket = newClientSocket;
			client.RecvOverlap.Operation = OPERATION_RECV;
			client.RecvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(client.RecvOverlap.Iocp_Buffer);
			client.RecvOverlap.WsaBuf.len = sizeof(client.RecvOverlap.Iocp_Buffer);

			CreateIoCompletionPort(reinterpret_cast<HANDLE>(newClientSocket),
				framework->GetIocpHandle(), newSerial, 0);

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
		Framework* framework = Framework::GetInstance();
		while (false == framework->IsShutDown())
		{
			DWORD iosize;
			DWORD serial;
			Overlap_Exp* overlapExp;

			BOOL result = GetQueuedCompletionStatus(framework->GetIocpHandle(),
				&iosize, &serial, reinterpret_cast<LPOVERLAPPED*>(&overlapExp), INFINITE);

			if (result == false 
				&& overlapExp->Original_Overlap.Pointer != nullptr)
			{
				std::cout << "WorkerThreadStart() - GetQueuedCompletionStatus error\n";
				continue;
			}
			if (0 == iosize)
			{
				framework->ProcessUserClose(serial);
				continue;
			}

			if (OPERATION_RECV == overlapExp->Operation)
			{
				Client& client = framework->GetClient(serial);
				
				unsigned char* buf_ptr = client.RecvOverlap.Iocp_Buffer;
				int remained = iosize;
				while (0 < remained)
				{
					if (0 == client.PacketSize)
						client.PacketSize = GetPacketSize(buf_ptr);
					int required = client.PacketSize - client.PreviousSize;

					if (remained >= required)
					{	//��Ŷ ���� �Ϸ�
						std::memcpy(client.PacketBuff + client.PreviousSize, buf_ptr, required);
						framework->ProcessPacket(serial, client.PacketBuff);
						buf_ptr += required;
						remained -= required;
						client.PacketSize = 0;
						client.PreviousSize = 0;
					}
					else
					{
						std::memcpy(client.PacketBuff + client.PreviousSize, buf_ptr, required);
						buf_ptr += remained;
						client.PreviousSize += remained;
						remained = 0;
					}
				}

				DWORD flags = 0;
				::WSARecv(client.ClientSocket,
					&client.RecvOverlap.WsaBuf, 1, NULL, &flags,
					&client.RecvOverlap.Original_Overlap, NULL);
			}
			else if (OPERATION_SEND == overlapExp->Operation)
			{	//���ۿϷ�, �޸� ����
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





