#include "DummyHandler.h"
#pragma comment (lib, "ws2_32.lib")
#include <iostream>


namespace
{
	void WorkerThreadStart();
	void TimerThreadStart();

} //unnamed namespace

std::default_random_engine DummyHandler::RANDOM_ENGINE;

DummyHandler::DummyHandler()
	: hIocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0))
	, isInitialized(false)
	, lastSerial(0)
	, lastCloseSerial(0)
	, dummies(MAX_DUMMY_COUNT)
	, timerQueue(Event_Compare())
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);

	//공개채널 리스트
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
}

DummyHandler::~DummyHandler()
{
	Close();
}

void DummyHandler::Close()
{
	isShutdown = true;

	std::cout << "소켓 종료를 위해 잠시 대기합니다...\n";
	for (auto& dummy : dummies)
	{
		if (dummy.first.isLogin)
		{
			dummy.first.Close();
			::Sleep(1);
		}
	}
	::Sleep(2000);


	if (isShutdown)
	{
		for (auto& th : workerThreads)
			th->join();
		timerThread->join();
	}

	::WSACleanup();
}

////////////////////////////////////////////////////////////////////////////

bool DummyHandler::Start(const std::string& ip)
{
	if (isInitialized == true)
		return false;

	//더미 클라이언트 활성화, 로그인
	if (true == AddDummy(lastSerial, START_DUMMY_COUNT, ip))
	{
		isInitialized = true;
	}
	else
	{
		std::cout << "Start() fail!\n";
		return false;
	}

	for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
		workerThreads.emplace_back(new std::thread(WorkerThreadStart));

	timerThread = std::unique_ptr<std::thread>(new std::thread(TimerThreadStart));

	return isInitialized;
}

bool DummyHandler::AddDummy(int beginSerial, int count, const std::string& ip)
{
	if (beginSerial < 0 || MAX_DUMMY_COUNT <= beginSerial)
	{
		std::cout << "AddDummy() - invalid serial\n";
		return false;
	}
	else if (lastSerial + count >= MAX_DUMMY_COUNT)
	{
		std::cout << "AddDummy() - dummies are full\n";
		return false;
	}

	for (int i = beginSerial; i < beginSerial + count; ++i)
	{
		Dummy& dummy = dummies[i].first;
		Overlap_Exp& recvOverlap = dummies[i].second.recvOverlapExp;

		if (false == dummy.Connect(ip.c_str()))
		{
			std::cout << "AddDummy() Connect Error, Serial : " << i << '\n';
			return false;
		}
	
		::CreateIoCompletionPort(reinterpret_cast<HANDLE>(dummy.clientSocket), hIocp, i, 0);

		//recv 등록
		::ZeroMemory(&recvOverlap, sizeof(recvOverlap));
		recvOverlap.WsaBuf.len = sizeof(recvOverlap.Iocp_Buffer);
		recvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(recvOverlap.Iocp_Buffer);
		recvOverlap.Operation = OPERATION_RECV;
		DWORD flags = 0;
		int ret = ::WSARecv(dummy.GetSocket(),
			&recvOverlap.WsaBuf, 1, NULL, &flags,
			&recvOverlap.Original_Overlap, NULL);

		if (0 != ret)
		{
			int error_no = WSAGetLastError();
			if (WSA_IO_PENDING != error_no)
				std::cout << "AddDummy() - WSARecv " << "error code : " << error_no << std::endl;
		}

		packet_login loginPacket;
		::ZeroMemory(&loginPacket, sizeof(loginPacket));
		loginPacket.Size = sizeof(loginPacket);
		loginPacket.Type = PACKET_LOGIN;
		loginPacket.Created = false;
		std::string id("Dummy" + std::to_string(i));
		std::memcpy(loginPacket.User, id.c_str(), id.size());
		
		SendPacket(i, reinterpret_cast<unsigned char*>(&loginPacket));

		++lastSerial;
	}

	std::cout << count << "dummies login!\n";
	return true;
}

bool DummyHandler::CloseDummy(int count)
{
	for (int i = 0; i < count; ++i)
	{
		if (lastCloseSerial + i >= lastSerial)
			return false;
		
		Dummy& dummy = dummies[lastCloseSerial + i].first;
		if (dummy.isLogin)
		{
			dummy.Close();
			lastCloseSerial++;
			::Sleep(1);
		}
	}

	std::cout << count << "dummies closed\n";
	return true;
}

////////////////////////////////////////////////////////////////////////////
//Packet Handling
void DummyHandler::SendPacket(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial
		|| packet == nullptr)
		return;

	try
	{
		Overlap_Exp* overlapExp = new Overlap_Exp;
		::ZeroMemory(overlapExp, sizeof(Overlap_Exp));

		overlapExp->Operation = OPERATION_SEND;
		overlapExp->WsaBuf.buf = reinterpret_cast<CHAR *>(overlapExp->Iocp_Buffer);
		overlapExp->WsaBuf.len = GetPacketSize(packet);
		memcpy(overlapExp->Iocp_Buffer, packet, overlapExp->WsaBuf.len);

		int ret = ::WSASend(dummies[serial].first.GetSocket(), &overlapExp->WsaBuf, 1, NULL, 0,
			&overlapExp->Original_Overlap, NULL);
	
		if (0 != ret)
		{
			int error_no = WSAGetLastError();
			if (WSA_IO_PENDING != error_no)
				std::cout << "SendPacket() - WSASend " << "error code : " << error_no << std::endl;
		}
	}
	catch (...)
	{
		std::cout << "SendPacket() - exception\n";
	}
}

void DummyHandler::ProcessPacket(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial 
		|| packet == nullptr)
		return;

	//더미를 수정하는, 즉 로그인 처리와 채널관련 처리만 진행하면 된다.
	switch (GetPacketType(packet))
	{
	case PACKET_LOGIN:
		ProcessLogin(serial, packet);
		break;
	case PACKET_CHANNEL_ENTER:
		ProcessChannelEnter(serial, packet);
		break;

	default:
		break;
	}
}


////////////////////////////////////////////////////////////////////////////
//Process
void DummyHandler::ProcessLogin(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial
		|| packet == nullptr)
		return;

	packet_login* my_packet = reinterpret_cast<packet_login*>(packet);

	std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
	dummies[serial].first.userName = my_packet->User;
	dummies[serial].first.isLogin = true;
	ulDummy.unlock();

	AddRandomPacketEvent(serial);
}

void DummyHandler::ProcessChannelEnter(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial
		|| packet == nullptr)
		return;

	packet_channel_enter* my_packet = reinterpret_cast<packet_channel_enter*>(packet);

	std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
	dummies[serial].first.userChannel = my_packet->ChannelName;
}

////////////////////////////////////////////////////////////////////////////
//Request
void DummyHandler::AddRandomPacketEvent(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	Event_Info eventInfo;
	eventInfo.Serial = serial;
	eventInfo.Event_Type = OPERATION_RANDPACKET;
	
	std::unique_lock<std::mutex> ulTimer(timerLock);
	eventInfo.Wakeup_Time = ::GetTickCount() + PACKET_DELAY_TIME;
	timerQueue.push(eventInfo);
}

void DummyHandler::RequestRandomPacket(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;
	if (dummies[serial].first.isLogin == false) return;

	std::uniform_real_distribution<double> urd(0.0, 1.0);
	
	static constexpr double	COEF_CHATTING = 0.20;				
	static constexpr double	COEF_WHISPER = 0.40;				
	static constexpr double	COEF_CHANNELLIST = 0.60;			
	static constexpr double	COEF_CHANNELCHANGE = 0.80;			
	static constexpr double	COEF_KICK = 1.0;					

	double coef = urd(RANDOM_ENGINE);
	
	if (0.0 <= coef && coef < COEF_CHATTING)
		RequestChatting(serial);
	else if (COEF_CHATTING <= coef && coef < COEF_WHISPER)
		RequestWhisper(serial);
	else if (COEF_WHISPER <= coef && coef < COEF_CHANNELLIST)
		RequestChannelList(serial);
	else if (COEF_CHANNELLIST <= coef && coef < COEF_CHANNELCHANGE)
		RequestChannelChange(serial);
	else if (COEF_CHANNELCHANGE <= coef && coef <= COEF_KICK)
		RequestKick(serial);

	AddRandomPacketEvent(serial);
}


void DummyHandler::RequestWhisper(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	Dummy& dummy = dummies[serial].first;
	
	std::string randomListener = GetRandomUser();
	if (randomListener.empty() 
		|| (randomListener == dummy.userName)) 
		return;

	std::string chat("This is whisper! ID : " + std::to_string(serial));
	
	packet_chatting chattingPacket;
	::ZeroMemory(&chattingPacket, sizeof(chattingPacket));
	chattingPacket.Size = sizeof(chattingPacket);
	chattingPacket.Type = PACKET_CHATTING;
	chattingPacket.IsWhisper = true;

	std::memcpy(chattingPacket.Talker, dummy.userName.c_str(), dummy.userName.size());
	std::memcpy(chattingPacket.Listner, randomListener.c_str(), randomListener.size());
	std::memcpy(chattingPacket.Chat, chat.c_str(), chat.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(&chattingPacket));
}

void DummyHandler::RequestChannelList(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	packet_channel_list channelListPacket;
	::ZeroMemory(&channelListPacket, sizeof(channelListPacket));
	channelListPacket.Size = sizeof(channelListPacket);
	channelListPacket.Type = PACKET_CHANNEL_LIST;

	SendPacket(serial, reinterpret_cast<unsigned char*>(&channelListPacket));
}

void DummyHandler::RequestChannelChange(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	std::string randomChannel = GetRandomChannel();
	if (randomChannel.empty()
		|| (randomChannel == dummies[serial].first.userChannel))
		return;

	packet_channel_enter enterPacket;
	::ZeroMemory(&enterPacket, sizeof(enterPacket));
	enterPacket.Size = sizeof(enterPacket);
	enterPacket.Type = PACKET_CHANNEL_ENTER;
	std::memcpy(enterPacket.ChannelName, randomChannel.c_str(), randomChannel.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(&enterPacket));
}

void DummyHandler::RequestKick(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	Dummy& dummy = dummies[serial].first;
	
	packet_kick_user kickPacket;
	::ZeroMemory(&kickPacket, sizeof(kickPacket));
	kickPacket.Size = sizeof(kickPacket);
	kickPacket.Type = PACKET_KICK_USER;
	
	//수정할사항 - 같은 채널에 있는 유저인지 확인해야함
	std::string randomTarget = GetRandomUser();
	if (randomTarget.empty()
		|| (randomTarget == dummy.userName))
		return;

	std::memcpy(kickPacket.Target, randomTarget.c_str(), randomTarget.size());
	std::memcpy(kickPacket.Kicker, dummy.userName.c_str(), dummy.userName.size());
	std::memcpy(kickPacket.Channel, dummy.userChannel.c_str(), dummy.userChannel.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(&kickPacket));
}

void DummyHandler::RequestChatting(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	Dummy& dummy = dummies[serial].first;
	
	packet_chatting chatPacket;
	::ZeroMemory(&chatPacket, sizeof(chatPacket));
	chatPacket.Size = sizeof(chatPacket);
	chatPacket.Type = PACKET_CHATTING;
	chatPacket.IsWhisper = false;

	std::string chat("This is test! ID : " + std::to_string(serial));

	std::memcpy(chatPacket.Talker, dummy.userName.c_str(), dummy.userName.size());
	std::memcpy(chatPacket.Chat, chat.c_str(), chat.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(&chatPacket));
}


////////////////////////////////////////////////////////////////////////////
//Private 
std::string DummyHandler::GetRandomUser() const
{
	std::uniform_int_distribution<int> uid(lastCloseSerial, lastSerial - 1);
	
	const int slot = uid(RANDOM_ENGINE);
	const Dummy& dummy = dummies[slot].first;
	if (dummy.isLogin)
		return dummy.userName;
	else
		return "";
}

std::string DummyHandler::GetRandomChannel() const
{
	std::uniform_int_distribution<int> uid(lastCloseSerial, lastSerial - 1);
	
	static const char* chars = "0123456789"
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static const int charsCount = ::strlen(chars);

	const int slot = uid(RANDOM_ENGINE);
	const Dummy& randomDummy = dummies[slot].first;
	if (randomDummy.isLogin == false)
		return "";

	std::uniform_real_distribution<double> urd(0.0, 1.0);
	static constexpr double	COEF_PUBLIC = 0.70;
	static constexpr double	COEF_TOUSER = 0.90;
	static constexpr double COEF_CUSTOM = 1.0;
	
	double coef = urd(RANDOM_ENGINE);
	if (0.0 <= coef && coef < COEF_PUBLIC)
		return publicChannels[slot % publicChannels.size()];
	else if (COEF_PUBLIC <= coef && coef < COEF_TOUSER)
		return randomDummy.userChannel;
	else //커스텀 채널로
	{
		std::string channelName;
		const int channelLength = (slot % (MAX_CHANNELNAME_LENGTH - 1)) + 1;

		for (int i = 0; i < channelLength; ++i)
		{
			channelName += chars[uid(RANDOM_ENGINE) % charsCount];
		}

		return channelName;
	}
}




namespace
{
	void WorkerThreadStart()
	{
		DummyHandler* handler = DummyHandler::GetInstance();

		while (false == handler->IsShutdown())
		{
			DWORD iosize;
			DWORD serial;
			Overlap_Exp* overlapExp;

			BOOL result = GetQueuedCompletionStatus(handler->GetIocpHandle(),
				&iosize, &serial, reinterpret_cast<LPOVERLAPPED*>(&overlapExp), INFINITE);

			if (result == false
				&& overlapExp->Original_Overlap.Pointer != nullptr)
			{
				std::cout << "WorkerThreadStart() - GetQueuedCompletionStatus error\n";
				return;
			}
			if (serial < 0 || DummyHandler::MAX_DUMMY_COUNT <= serial)
			{
				std::cout << "WorkerThreadStart() - invalid serial error\n";
				return;
			}

			Dummy& dummy = handler->GetDummies()[serial].first;
			Overlap_Info& overlapInfo = handler->GetDummies()[serial].second;

			if (0 == iosize)
			{
				dummy.Close();
				continue;
			}

			if (OPERATION_RECV == overlapExp->Operation)
			{
				unsigned char* buf_ptr = overlapInfo.recvOverlapExp.Iocp_Buffer;
				int remained = iosize;
				while (0 < remained)
				{
					if (0 == overlapInfo.PacketSize)
						overlapInfo.PacketSize = GetPacketSize(buf_ptr);
					int required = overlapInfo.PacketSize - overlapInfo.PreviousSize;

					if (remained >= required)
					{	//패킷 조립 완료
						memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousSize, buf_ptr, required);
						handler->ProcessPacket(serial, overlapInfo.PacketBuff);
						buf_ptr += required;
						remained -= required;
						overlapInfo.PacketSize = 0;
						overlapInfo.PreviousSize = 0;
					}
					else
					{
						memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousSize, buf_ptr, required);
						buf_ptr += remained;
						overlapInfo.PreviousSize += remained;
						remained = 0;
					}
				}

				DWORD flags = 0;
				int recvRet = ::WSARecv(dummy.GetSocket(),
					&overlapInfo.recvOverlapExp.WsaBuf, 1, NULL, &flags,
					&overlapInfo.recvOverlapExp.Original_Overlap, NULL);
				if (0 != recvRet)
				{
					int error_no = WSAGetLastError();
					if (WSA_IO_PENDING != error_no)
						std::cout << "WorkerThreadStart() - WSARecv " << "error code : " << error_no << std::endl;
				}
			}
			else if (OPERATION_SEND == overlapExp->Operation)
			{	
				delete overlapExp;
			}
			else if (OPERATION_RANDPACKET == overlapExp->Operation)
			{
				delete overlapExp;
				//랜덤패킷 발송, 이 함수내에서 다시 타이머 등록한다.
				handler->RequestRandomPacket(serial);
			}
			else
			{
				std::cout << "Unknown IOCP event!\n";
			}
		}
	}

	void TimerThreadStart()
	{
		DummyHandler* handler = DummyHandler::GetInstance();
		std::mutex& timerLock = handler->GetTimerLock();
		auto& timerQueue = handler->GetTimerQueue();

		while (false == handler->IsShutdown())
		{
			::Sleep(1);
			std::unique_lock<std::mutex> ul(timerLock);

			while (false == timerQueue.empty())
			{
				if (timerQueue.top().Wakeup_Time > ::GetTickCount()) 
					break;
				
				Event_Info ev = timerQueue.top();
				timerQueue.pop();
				ul.unlock();

				try
				{
					Overlap_Exp* overlapExp = new Overlap_Exp;
					::ZeroMemory(overlapExp, sizeof(Overlap_Exp));
					overlapExp->Operation = OPERATION_RANDPACKET;
					PostQueuedCompletionStatus(handler->GetIocpHandle(), 1,
						ev.Serial, &overlapExp->Original_Overlap);
				}
				catch (...)
				{
					std::cout << "TimerThreadStart() - exception\n";
				}

				ul.lock();
			}
		}
	}

} //unnamed namespace
