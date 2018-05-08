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
	@brief		iocp 초기화, 윈속 초기화, Initialize 호출을 진행한다.
*/
Framework::Framework()
	: hIocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0))
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	isShutDown = !Initialize();
}


/**
	@brief		thread join, 윈속 해제를 진행한다.
*/
Framework::~Framework()
{
	ShutDown();
}

/**
	@brief		정적 자료들 모두 생성, 파일 입력, 프로시저 등록, 스레드 생성을 진행한다.

	@return		bad alloc에 대한 처리문이 존재하며, 전파하지 않고 false를 반환한다.
				모두 정상적으로 초기화 시 true를 반환한다.
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

	//공개채널 리스트 파일 읽기
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
	@brief		프레임워크 소멸시 호출, 스레드 종료 및 윈속 해제를 진행한다.
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
	@brief		모든 Send 요청에 대한 최종 단계 - WsaSend
	@details	전송을 위한 Overlap 확장 구조체는 concurrent queue로부터 시리얼을 받아서 사용한다.
				
	@param serial 보내고자 하는 클라이언트 시리얼
	@param packet 보내고자 하는 패킷, Serialize 되어있다.

	@warning	concurrent queue의 try_pop이 지연되어 timeout 될 수 있다.

	@todo		queue가 텅 비지 않도록 동적으로 추가하는 방식, 혹은 메모리 풀의 적용
				혹은 적절한 동접과 그에 맞는 큐의 크기 설정에 대한 생각
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
	@brief		가장 간단히, server message를 보내는 함수 - Packet_System
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
	@brief		조립된 패킷으로부터 프로시저를 호출
	@param size 패킷 크기, GetPacketSize() 할 수 있으나 패킷 조립단계 확인했으므로 값을 넘겨받도록 함

	@throw StreamReadUnderflow - 프로시저 내 패킷 Deserialize에서 발생할 수 있음
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
	@brief		클라이언트 측 종료요청에 대한 처리
	@details	나간 사람 정보에 대한 broadcast, 채널 파기를 진행하고, closesocket 응답, 자료구조 정리
				clientNameLock 및 클라이언트 시리얼 큐 의해 동기화되며
				모든 처리가 완료되면 사용했던 클라이언트 시리얼을 반납한다.
*/
void Framework::ProcessUserClose(Framework::SERIAL_TYPE serial)
{
	if (IsValidClientSerial(serial) == false) return;
	
	auto& client = GetClient(serial);
	if (client.IsConnect == false) return;
	//클라이언트 락 필요 X -> clientNameLock와 validClientSerials 큐에 의해 동기화된다.
	client.IsConnect = false;

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

	client.UserName.clear();
	client.ChannelName.clear();
	validClientSerials.push(serial);
}

/**
	@brief		connect된 클라이언트의 첫 요청 - 아이디 생성 처리
	@details	clientNameLock으로 동기화하여, 중복되지 않는 아이디라면 선점하고 락을 해제한다.
				login되지 않은 client의 이름은 빈(empty) 이름이다.
				정상적으로 생성되었다면 공개채널로 이동시킨다.

	@throw StreamReadUnderflow - Packet_Login::Deserialize()에서 발생할 수 있음
*/
void Framework::Process_Login(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false
		|| clients[serial]->IsConnect == false) 
		return;
	//클라이언트 락 필요 X -> 패킷 처리 순서 지켜짐, 클라이언트가 login 처리 전에 요청을 여러개 보낼 수 있으나 순서가 지켜진다.
	
	//이미 login된 상태
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

	//아이디 생성되어 로그인했다면, 공개채널으로 연결 및 통지해야한다.
	if (to_packet.isCreated)
	{
		ConnectToRandomPublicChannel(serial);
	}
}

/**
	@brief		공개채널 리스트 및 커스텀채널 개수 요청에 대한 처리
	@details	공개채널은 불변이므로 동기화없이 이름을 취합하고
				커스텀채널은 가변이지만, 개수만을 확인하므로 lock 없이 진행한다.
				커스텀채널의 활성화여부는, Initialize 되었는지, 즉 이름이 존재하는지로 확인한다.

	@warning	커스텀 채널의 개수가 정확한 수치가 아니다.
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

	//커스텀채널의 개수 확인시 lock을 하는것이 정확한 결과이나
	//이름을 확인하지 않고 어느정도의 커스텀채널이 있는지 확인하는 것이므로, lock을 하지 않는다.
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
	@brief		채널 내 전체채팅 혹은 귓속말 요청에 대한 처리
	@details	전체채팅의 경우, 클라이언트가 채팅을 친 채널의 변경에 대해 동기화되어야 하므로
				클라이언트 락을 진행하여 해당 채널에 전달되도록 작성되었다.
				
				귓속말의 경우 클라이언트 락이 필요하지 않으므로 listener의 유효성 검사를 진행한 뒤
				둘에게 다시 보내주도록 한다.

	@warning	전체채팅의 경우 clientLock -> channelLock 총 두개의 잠금이 있으므로 구현에 신경쓰도록 한다.
				다른 처리함수에도 이 순서가 지켜져야 한다.

	@throw StreamReadUnderflow - Packet_Chatting::Deserialize()에서 발생할 수 있음
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
		//클라이언트 락 필요 -> 채널 이름 확인, 사용
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
			SendSystemMessage(serial, "***System*** 해당 유저는 접속중이 아닙니다.");
	}
}

/**
	@brief		방장인 유저가 강퇴를 요청했을때에 대한 처리
	@details	요청한 kicker가 아닌 강퇴당할 target을 잠근 후 강퇴 요청의 유효성을 확인한다.
				
				이를 통해 다음과 같은 상황 및 유사한 경우들을 방지한다.
					A가 B를 강퇴하고자 하는 처리와 B는 다른 채널로 이동하려는 처리가 동시에 발생 시
					요청이 유효한 것을 확인한 뒤 강퇴를 처리하기 전 B가 채널을 바꾸면, B는 바꾼 채널에서 강퇴당한다.

				요청이 유효하다면, target을 채널로부터 내보내고 공개채널로 이동시킨다.

	@warning	kicker는 잠글 이유도 없으며 잠궈선 안된다. 데드락 발생의 원인이다.

	@throw StreamReadUnderflow - Packet_Kick_User::Deserialize()에서 발생할 수 있음
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
		SendSystemMessage(serial, "***System*** 해당 유저는 접속중이 아닙니다.");
		return;
	}

	auto& target = clients[targetSerial];
	//target 락 필요 -> 채널 이름 확인, 사용
	std::unique_lock<std::mutex> ulClient(target->clientMutex);

	auto channel = FindChannelFromName(kickPacket.channelName);
	if (channel == nullptr)
	{
		SendSystemMessage(serial, "***System*** 요청 처리 실패, 다시 입력해주세요.");
		return;
	}
	
	if (channel->GetChannelMaster() != kickPacket.kicker
		|| target->ChannelName != kickPacket.channelName)
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

/**
	@brief		채널 이동 요청에 대한 처리
	@details	이동하고자 하는 채널 이름이 존재하면 이동이고, 없으면 커스텀채널 생성이다.
				존재한다면, 현재 채널에서 떠나는 처리와 새 채널에 들어가는 처리를 진행하고
				없다면, 커스텀채널을 생성하고 현재 채널에서 떠나는 처리와 생성한 채널에 들어간다.
				현재 채널이 void라면 떠나는 처리는 진행하지 않는다.

	@warning	clientLock과 ConnectToChannel()내의 channelLock 총 두 개의 잠금이 발생한다.

	@throw StreamReadUnderflow - Packet_Channel_Enter::Deserialize()에서 발생할 수 있음
*/
void Framework::Process_ChannelChange(Framework::SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidClientSerial(serial) == false) return;

	auto& client = GetClient(serial);
	if (client.IsConnect == false) return;
	//클라이언트 락 필요 -> 채널 확인, 이동
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
	@brief		공개채널 중 입장할 수 있는 임의의 채널의 인덱스를 얻는다.
	@details	std::default_random_engine으로 난수 발생, Knuth Shuffle을 사용하여 작성되었다.
				총 PUBLIC_BUSY_COUNT 번의 중복없는 난수(채널 인덱스) 확인 후, 그것이 가득 찼다면 혼잡으로 정의한다.

	@return		[0, MAX_PUBLIC_CHANNEL_COUNT)와 SERIAL_ERROR를 반환한다.
				SERIAL_ERROR는 공개채널이 혼잡한 것으로 판단, void채널로 유도하도록 한다.

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
	@brief		해당 채널에 모든 유저들에게 패킷을 전송한다.

	@warning	해당 채널에 대한 잠금은 호출 전에 진행되어야 한다.
*/
void Framework::BroadcastToChannel(const std::shared_ptr<Channel>& channel, const void* packet)
{
	if (channel == nullptr || packet == nullptr) return;

	for (const auto* client : channel->GetClientsInChannel())
		SendPacket(client->Serial, packet);
}

/**
	@brief		해당 유저를 채널에서 퇴장시키고, 채널에 남은 유저들에게 알린다.
	@details	channelLock이 진행되며, 채널 퇴장에 의해 커스텀채널이 Close되는 경우 customChannelLock도 진행한다.
				leaver가 떠남을 알리며, 방장이 바뀐 경우 이에 대한 알림 또한 진행한다.
				강퇴당한 경우, leaver 본인에게도 알린다.

	@param isKicked 강퇴에 의해 채널을 떠나는지 여부이다.

	@warning	channelLock 및 customChannelLock 총 두 개의 잠금에 주의한다.
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

	//퇴장에 의해 방장이 변경될 수 있다.
	std::string beforeMasterName = channel->GetChannelMaster();
	channel->Exit(&client);	

	if (client.UserName == beforeMasterName)
	{	//본 조건문이 진행되는 경우는 channel이 반드시 CustomChannel 이다.
		isMasterChanged = true;

		if (channel->GetUserCount() == 0)
		{	//채널이 파기되었다.
			std::unique_lock<std::mutex> ulCustom(customChannelsLock);
			validCustomChannelSerials.push(usedCustomChannelNames[channelName]);
			usedCustomChannelNames.erase(channelName);
			ulCustom.unlock();

			return; //후속처리(채널에 남은 유저들에게 정보 전송)가 필요하지 않아 return
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
	@brief		임의의 공개채널로 유저를 이동시킨다.
	@details	채널이 혼잡한 경우, 클라이언트의 채널을 clear하고 Void 채널로 취급

	@warning	clientLock이 진행되고 호출되어야 한다. 
				예외로, 처음 로그인 단계에서의 호출은 순서가 지켜지므로 문제되지 않는다.
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
		{	//모든 공개채널이 혼잡한 상태로, 채널에 입장하지 못한 상태
			//스타크래프트1 배틀넷의 Void 채널과 유사
			clients[serial]->ChannelName.clear();
			SendSystemMessage(serial, "***System*** 공개채널이 혼잡하여 연결하지 못했습니다.");
			return;
		}
		else
			isConnected = ConnectToChannel(serial, publicChannels[randSlot]->GetChannelName());
	}
}

/**
	@brief		유저를 해당 채널로 연결한다.
	@details	channelLock 진행 후 유저를 연결하며, 새로 연결된 유저는 채널 정보와 유저리스트를
				채널에 있던 유저는 새로운 유저 정보를 받게 된다.

	@return		CHANNEL_CONNECT 열거형 타입을 반환한다.
				실패는 인수가 유효하지 않거나, 찾으려는 채널이 없거나, 연결하려는 채널이 가득찬 경우이고
				성공은 정상적으로 채널 이동하고 그에 대한 처리가 완료되는 경우이다.

	@warning	clientLock이 선행되어야 한다.
				clientLock과 channelLock 두 개가 있으므로 주의한다.
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

	//1. 새 유저는 이동하고자 하는 채널 정보를 얻는다.
	Packet_Channel_Enter enterPacket;
	enterPacket.channelName = client.ChannelName;
	enterPacket.channelMaster = channel->GetChannelMaster();
	StreamWriter enterStream(buf, sizeof(buf));
	enterPacket.Serialize(enterStream);

	SendPacket(serial, enterStream.GetBuffer());

	//2. 연결하고자 하는 채널의 유저들에게 새 유저를 알리면서 + 그 이름들을 종합한다.
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
	
	//3. 마지막으로 새 유저는 채널에 존재하는 유저들의 리스트를 얻는다.
	StreamWriter usersStream(buf, sizeof(buf));
	usersPacket.Serialize(usersStream);
	SendPacket(serial, usersStream.GetBuffer());

	return CHANNEL_CONNECT::SUCCESS;
}

/**
	@brief		이름으로부터 클라이언트의 시리얼을 얻는다.
	@details	clientNameLock을 통해 동기화한다.

	@return		SERIAL_ERROR 는 중복된 이름인 경우이며
				그 외에 값은 이름과 일치하는 클라이언트의 시리얼이다.
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
	@brief		채널 이름으로부터 해당되는 채널의 shared_ptr를 얻는다.
	@details	공개방은 불변으로 잠금없이 이름이 일치하는지 검사를 진행하며
				커스텀채널은 잠금 이후 이름을 비교한다.
				
	@return		nullptr 반환 -> 일치하는 이름의 채널이 없는 경우
				유효한 값 반환 -> 해당 채널의 '다형성을 띤' 포인터이다.
*/
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

/**
	@brief		concurrent queue로부터 type에 맞는, 사용가능한 시리얼 값을 얻는다.

	@param	type enum class이며 클라이언트, 커스텀채널, Overlap확장 구조체 중 하나이다.

	@return		정해진 TIMEOUT 내에 try_pop 성공 시 유효한 Serial 값을 얻는다.
				TIMEOUT되면, SERIAL_ERROR 반환
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
	@brief		사용한 OverlapExp를 반환한다. WSASend가 완료된 시점에서 WorkerThread에 의해 호출된다.
*/
void Framework::ReturnUsedOverlapExp(SERIAL_TYPE serial)
{
	if (MAX_OVERLAPEXP_COUNT <= serial) return;
	validOverlapExpSerials.push(serial);
}

/**
	@brief		새로운 커스텀채널을 생성한다.
	@details	concurrent queue로부터 새 커스텀채널 인덱스를 얻는다.
				성공적으로 얻었다면, 전체 커스텀채널을 잠그고 사용중인 커스텀채널 이름을 등록한다.
				
				AddNewCustomChannel() 호출 자체는 동기화 되지 않으므로 둘 이상의 유저가
				같은 이름의 커스텀채널을 생성하려 할 수 있다. 허나 먼저 잠금을 얻는 유저의 내용이 처리되며
				늦게 처리되는 유저는 시리얼을 반환하고 그 채널로 이동하게된다.(Process_ChannelChange() 참고)

	@see		Framework::Process_ChannelChange()
	@warning	컨커런트 큐의 try_pop이 지체될 수 있다.
				허나, MAX_CLIENT_COUNT와 MAX_CUSTOM_COUNT가 같으므로 현재는 그렇지 않다.

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
	{	//다른 유저가 먼저 채널 생성, 시리얼 반환
		validCustomChannelSerials.push(customSerial);
		return;
	}

	usedCustomChannelNames.insert(std::make_pair(channelName, customSerial));
	customChannels[customSerial]->InitializeChannel(channelName);
}


/**
	@brief		커스텀채널의 이름을 얻는다.

	@param doLock	잠금을 진행하여 정확한 정보를 얻을지에 대한 여부이다.

	@return 현재 초기화된 모든 커스텀채널의 이름이 담겨있다.
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
	@brief	총 접속중인 유저의 수를 얻는다.

	@param doLock	잠금을 진행하여 정확한 정보를 얻을지에 대한 여부이다.
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
		@brief		acceptThread 동작함수, 클라이언트 connect를 처리한다.
		@details	acceptThread는 단일스레드이며 select 모델로 동작한다.
					서버 종료요청 처리를 위한 타임아웃이 있다. (Framework::ACCEPT_TIMEOUT_SECONDS)
					프레임워크 클라이언트 시리얼 concurrent queue에서 시리얼값을 받아 사용한다.
					이후 클라이언트 초기화, iocp 등록, WSARecv 등록을 진행하며 마무리한다.

		@see		Framework::GetSerialForNewClient()

		@warning	연결 실패의 처리가 시리얼 컨커런트 큐의 타임아웃 (NEWBIESERIAL_TIMEOUT_MILLISECONDS), 
					즉 큐의 혼잡도에 의해 처리되고 있어 MAX_CLIENT_COUNT를 준수하지 않음을 주의한다.
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

			//새 클라이언트 초기화, 등록
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
		@brief		주 작업스레드의 동작함수이다.
		@details	iocp로 동작한다. overlap 구조체를 확장하여 사용하며(Overlap_Exp), key는 클라이언트 시리얼이다.
					Overlap_Exp의 OPERATION 열거형으로 이벤트를 구분하여 처리한다.
					recv는 패킷을 조립하고, 해당 프로시저를 호출한 뒤, 다시 recv를 등록한다.
					send는 사용했던 Overlap_Exp를 반환하는 작업을 진행한다.

					서버 종료요청을 처리하기 위해 타임아웃을 두었다(GQCS_TIMEOUT_MILLISECONDS)

		@todo		패킷 조립코드는 더 좋고 간결한 방법을 찾아낼때마다 적용하고 테스트해야 한다.
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
					{	//조립가능
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
			{	//전송완료, overlap_exp 반환
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





