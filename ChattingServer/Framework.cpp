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
	, validSerial(0)
{
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
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
		workerThreads.emplace_back(new std::thread(WorkerThreadStart));

	acceptThread = std::unique_ptr<std::thread>(new std::thread(AcceptThreadStart));

	publicChannels.emplace_back("FreeChannel");
	publicChannels.emplace_back("ForTeenagers");
	publicChannels.emplace_back("For20s");
	publicChannels.emplace_back("For3040s");
	publicChannels.emplace_back("AboutGame");
	publicChannels.emplace_back("AboutStudy");
	publicChannels.emplace_back("AboutHobby");
	publicChannels.emplace_back("AboutExcercise");
}
	 

////////////////////////////////////////////////////////////////////////////
//Send to Client
void Framework::SendPacket(int serial, unsigned char* packet) const
{
	auto clientIter = clients.find(serial);
	if (clientIter == clients.cend())
	{
		std::cout << "SendPacket() - Invalid serial number" << std::endl;
		return;
	}

	Overlap_Exp* overlapExp = new Overlap_Exp;
	::ZeroMemory(overlapExp, sizeof(Overlap_Exp));

	overlapExp->Operation = OPERATION_SEND;
	overlapExp->WsaBuf.buf = reinterpret_cast<CHAR *>(overlapExp->Iocp_buffer);
	overlapExp->WsaBuf.len = GetPacketSize(packet);
	memcpy(overlapExp->Iocp_buffer, packet, overlapExp->WsaBuf.len);

	int ret = WSASend(clientIter->second.ClientSocket, &overlapExp->WsaBuf, 1, NULL, 0,
		&overlapExp->Original_Overlap, NULL);
	
	if (0 != ret) 
	{
		int error_no = WSAGetLastError();
		std::cout << "SendPacket::WSASend Error : " <<  error_no << std::endl;
	}
}

void Framework::SendSystemMessage(int serial, const std::string& msg) const
{
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
	if (clients[serial].IsLogin == true)
	{	//�α��� �Ǿ��� -> ä�ο� �����ϹǷ� ä���� ������ �Ѵ�.
		Channel* channel = FindChannelFromName(clients[serial].ChannelName);
		if(channel)
		{
			HandleUserLeave(serial, false, channel);
		}
	}
	
	std::unique_lock<std::mutex> ul(mLock);
	clients.erase(serial);
}

void Framework::ProcessLogin(int serial, unsigned char* packet)
{
	packet_login* from_packet = reinterpret_cast<packet_login*>(packet);
	bool isDuplicated = false;

	std::unique_lock<std::mutex> ul(mLock);
	for (auto& client : clients)
	{
		if (client.second.UserName == from_packet->User)
		{
			isDuplicated = true;
			break;
		}
	}
	
	if(isDuplicated == false) //���� �� �� ����
	{
		clients[serial].UserName = from_packet->User;
		clients[serial].IsLogin = true;
	}
	ul.unlock();

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
	packet_channel_list channelList_packet;
	::ZeroMemory(&channelList_packet, sizeof(channelList_packet));
	//����ä���� �Һ����� ���� �ʿ�ġ ������ Ŀ����ä���� �������� ���� Ȯ�ν� ���� �ʿ��� ���̴�.
	channelList_packet.Size = sizeof(packet_channel_list);
	channelList_packet.Type = PACKET_CHANNEL_LIST;
	channelList_packet.PublicChannelCount = static_cast<char>(publicChannels.size());
	std::string publicChannelNames;
	for (auto& name : publicChannels)
	{
		publicChannelNames += name.GetChannelName() + NAME_DELIMITER;
	}
	std::memcpy(&channelList_packet.PublicChannelNames, publicChannelNames.c_str(), publicChannelNames.size());
	
	std::unique_lock<std::mutex> ul(mLock);
	channelList_packet.CustomChannelCount = customChannels.size();
	ul.unlock();
	
	SendPacket(serial, reinterpret_cast<unsigned char *>(&channelList_packet));
}

void Framework::ProcessChatting(int serial, unsigned char* packet)
{
	packet_chatting* from_packet = reinterpret_cast<packet_chatting*>(packet);
	
	if (from_packet->IsWhisper == false)
	{
		BroadcastToChannel(clients[serial].ChannelName, packet);
	}
	else
	{
		bool isWhisperLogin = false;
		
		for (auto& client : clients)
		{
			if (client.second.UserName == from_packet->Listner)
			{
				isWhisperLogin = true;
				SendPacket(serial, packet);
				SendPacket(client.second.Serial, packet);
				break;
			}
		}

		if (isWhisperLogin == false)
		{
			std::string whisperFail("***System*** �ش� ������ �������� �ƴմϴ�.");
			SendSystemMessage(serial, whisperFail);
		}
	}
}

void Framework::ProcessKick(int serial, unsigned char* packet)
{
	packet_kick_user* from_packet = reinterpret_cast<packet_kick_user*>(packet);
	
	Client* target = FindClientFromName(from_packet->Target);
	if (target == nullptr)
	{
		std::string msg("***System*** �ش� ������ �������� �ƴմϴ�.");
		SendSystemMessage(serial, msg);
		return;
	}

	Channel* channel = FindChannelFromName(from_packet->Channel);
	if (channel == nullptr)
	{
		std::string msg("***System*** ��û�� ������ �߻��Ͽ����ϴ�.");
		SendSystemMessage(serial, msg);

		std::cout << "ProcessKick() - cannot find channel\n";
		return;
	}
	
	if (channel->GetChannelMaster() != from_packet->Kicker
		|| target->ChannelName != from_packet->Channel)
	{
		std::string msg("***System*** ������ �ƴϰų�, ���� ä���� �ƴմϴ�.");
		SendSystemMessage(serial, msg);
		return;
	}

	if (target->Serial == serial)
	{
		std::string msg("***System*** ������ ������ �� �����ϴ�.");
		SendSystemMessage(serial, msg);
		return;
	}

	//�������� �� ���������� �̵���Ŵ
	HandleUserLeave(target->Serial, true, channel);
	ConnectToRandomPublicChannel(target->Serial);
}

void Framework::ProcessChannelChange(int serial, unsigned char* packet)
{
	packet_channel_enter* from_packet = reinterpret_cast<packet_channel_enter*>(packet);
	
	std::unique_lock<std::mutex> ul(mLock);
	bool isChannelChanged = false;
	Channel* channel = FindChannelFromName(from_packet->ChannelName);
	Channel* prevChannel = FindChannelFromName(clients[serial].ChannelName);
	if (channel)
	{
		ul.unlock();

		isChannelChanged = ConnectToChannel(serial, from_packet->ChannelName);
		if (isChannelChanged == false)
		{
			std::string msg("***System*** ä�� �ִ�����ο��� �ʰ��Ͽ����ϴ�.");
			SendSystemMessage(serial, msg);
		}
	}
	else //�� Ŀ����ä�� ����
	{
		CustomChannel newChannel(from_packet->ChannelName);
		customChannels.push_back(std::move(newChannel));
		ul.unlock();

		ConnectToChannel(serial, newChannel.GetChannelName());
		isChannelChanged = true;
	}

	if (isChannelChanged && prevChannel)
	{
		HandleUserLeave(serial, false, prevChannel);
	}
}


////////////////////////////////////////////////////////////////////////////
//Private Functions

int Framework::GetRandomPublicChannelIndex() const
{
	const int publicChannelCount = publicChannels.size();
	static std::uniform_int_distribution<int> uid(0, publicChannelCount - 1);
	static std::default_random_engine dre;

	int randSlot = -1;
	while (true)
	{
		randSlot = uid(dre);
		if (publicChannels[randSlot].GetUserCount() < publicChannelCount)
			return randSlot;
	}
}

void Framework::BroadcastToChannel(const std::string& channelName, unsigned char* packet)
{
	Channel* channel = FindChannelFromName(channelName);

	if (channel)
	{
		for (auto* client : channel->GetClientsInChannel())
			SendPacket(client->Serial, packet);
	}
}

void Framework::HandleUserLeave(int leaver, bool isKicked, Channel* channel)
{
	if (channel == nullptr)
	{
		std::cout << "HandleUserLeave() - channel is nullptr\n";
		return;
	}

	bool isMasterChanged = false;
	std::string channelName = channel->GetChannelName();

	std::unique_lock<std::mutex> ul(mLock);
	channel->Exit(&clients[leaver]);
	if (clients[leaver].UserName == channel->GetChannelMaster())
	{	//��������, �� Ŀ����ä���� ���/ dynamic_cast�� �ʿ����
		auto toDelete = std::find_if(customChannels.cbegin(), customChannels.cend(),
			[&channelName](const Channel& ch)
		{
			return ch.GetChannelName() == channelName;
		});

		if (toDelete != customChannels.cend())
		{
			if (channel->GetUserCount() == 0)
			{
				customChannels.erase(toDelete);
			}
			else
			{	//����Ʈ���� ���� ���� ���� �������� ������ �ѱ��.
				std::string newMaster = channel->GetClientsInChannel().front()->UserName;
				channel->SetChannelMaster(newMaster);
				isMasterChanged = true;
			}
		}
	}
	ul.unlock();

	packet_user_leave leave_packet;
	::ZeroMemory(&leave_packet, sizeof(leave_packet));
	leave_packet.Size = sizeof(leave_packet);
	leave_packet.Type = PACKET_USER_LEAVE;
	std::memcpy(&leave_packet.User, clients[leaver].UserName.c_str(), clients[leaver].UserName.size());
	leave_packet.IsKicked = isKicked;

	if (isKicked)
	{
		std::string msg("***System*** ���忡 ���� ������Ͽ����ϴ�. ����ä�η� �̵��մϴ�.");
		SendSystemMessage(leaver, msg);
	}
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
		isConnected = ConnectToChannel(serial, publicChannels[randSlot].GetChannelName());
	}
}

bool Framework::ConnectToChannel(int serial, const std::string& channelName)
{
	Client& client = clients[serial];
	Channel* channel = FindChannelFromName(channelName);
	if (channel == nullptr)
	{
		std::cout << "ConnectToChannel() - cannot find channel\n";
		return false;
	}

	std::unique_lock<std::mutex> ul(mLock);
	if (channel->GetUserCount() < MAX_CHANNEL_USERS)
	{
		channel->Enter(&client);
		client.ChannelName = channelName;
		ul.unlock();
	}
	else
		return false; //unlock ����� �ʿ� X
	
	packet_newface_enter newface_packet;
	::ZeroMemory(&newface_packet, sizeof(newface_packet));
	newface_packet.Size = sizeof(packet_newface_enter);
	newface_packet.Type = PACKET_NEWFACE_ENTER;
	std::memcpy(&newface_packet.User, client.UserName.c_str(), client.UserName.size());

	//1. �����ϰ��� �ϴ� ä���� �����鿡�� �� ������ �˸��鼭 �̸��� ��´�.
	std::string userNames;
	unsigned int userCount = 0;
	ul.lock();
	for (auto* clientInChannel : channel->GetClientsInChannel())
	{
		SendPacket(clientInChannel->Serial, reinterpret_cast<unsigned char*>(&newface_packet));
		userNames += clientInChannel->UserName + NAME_DELIMITER;
		++userCount;
	}
	ul.unlock();

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
	users_packet.Size = sizeof(users_packet);
	users_packet.Type = PACKET_CHANNEL_USERS;
	std::memcpy(&users_packet.ChannelName, client.ChannelName.c_str(), client.ChannelName.size());
	users_packet.UserCountInPacket = userCount;
	std::memcpy(&users_packet.UserNames, userNames.c_str(), userNames.size());

	SendPacket(serial, reinterpret_cast<unsigned char *>(&users_packet));

	return true;
}

Client* Framework::FindClientFromName(const std::string& clientName)
{
	Client* retClient = nullptr;
	for (auto& client : clients)
	{
		if (client.second.UserName == clientName)
		{
			retClient = &client.second;
			break;
		}
	}

	return retClient;
}

Channel* Framework::FindChannelFromName(const std::string& channelName)
{
	Channel* channel = nullptr;
	for (auto& ch : publicChannels)
	{
		if (channelName == ch.GetChannelName())
		{
			channel = &ch;
			return channel;
		}
	}

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

		// listen
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
			}

			int newSerial = framework->GetNextValidSerial();

			//�� Ŭ���̾�Ʈ ����, �ڷᱸ���� ����
			framework->GetClients().insert(std::make_pair(newSerial, Client()));
			Client& client = framework->GetClients()[newSerial];
			client.Serial = newSerial;
			client.ClientSocket = newClientSocket;
			client.RecvOverlap.Operation = OPERATION_RECV;
			client.RecvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(client.RecvOverlap.Iocp_buffer);
			client.RecvOverlap.WsaBuf.len = sizeof(client.RecvOverlap.Iocp_buffer);
			

			CreateIoCompletionPort(reinterpret_cast<HANDLE>(newClientSocket),
				framework->GetIocpHandle(), newSerial, 0);

			DWORD flags = 0;
			int ret = WSARecv(newClientSocket, &client.RecvOverlap.WsaBuf, 1, NULL,
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
				while (true); //debug
			}

			auto clientIter = framework->GetClients().find(serial);
			if (clientIter == framework->GetClients().cend())
			{
				std::cout << "WorkerThreadStart() - Invalid serial number" << std::endl;
				exit(-1);
			}
			Client& client = clientIter->second;

			if (0 == iosize)
			{
				closesocket(client.ClientSocket);
				framework->ProcessUserClose(serial);
				continue;
			}

			if (OPERATION_RECV == overlapExp->Operation)
			{
				unsigned char* buf_ptr = client.RecvOverlap.Iocp_buffer;
				int remained = iosize;
				while (0 < remained)
				{
					if (0 == client.PacketSize)
						client.PacketSize = GetPacketSize(buf_ptr);
					int required = client.PacketSize - client.PreviousSize;

					if (remained >= required)
					{	//��Ŷ ���� �Ϸ�
						memcpy(client.PacketBuff + client.PreviousSize, buf_ptr, required);
						framework->ProcessPacket(serial, client.PacketBuff);
						buf_ptr += required;
						remained -= required;
						client.PacketSize = 0;
						client.PreviousSize = 0;
					}
					else
					{
						memcpy(client.PacketBuff + client.PreviousSize, buf_ptr, required);
						buf_ptr += remained;
						client.PreviousSize += remained;
						remained = 0;
					}
				}

				DWORD flags = 0;
				WSARecv(client.ClientSocket,
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
				exit(-1);
			}
		}
	}

} //unnamed namespace





