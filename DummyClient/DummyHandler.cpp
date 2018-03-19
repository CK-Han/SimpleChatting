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
	, timerQueue(Event_Compare())
{
	WSADATA	wsadata;
	::WSAStartup(MAKEWORD(2, 2), &wsadata);
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

	//더미 클라이언트 생성, 로그인
	dummies.resize(MAX_DUMMY_COUNT);
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
		std::cout << "유효하지 않은 serial 값\n";
		return false;
	}
	else if (lastSerial + count > MAX_DUMMY_COUNT)
	{
		std::cout << "최대 더미 수를 초과하였습니다.\n";
		return false;
	}

	for (int i = beginSerial; i < beginSerial + count; ++i)
	{
		Dummy& dummy = dummies[i].first;
		Overlap_Exp& recvOverlap = dummies[i].second.recvOverlapExp;
		Overlap_Exp& sendOverlap = dummies[i].second.sendOverlapExp;
		Overlap_Exp& timerOverlap = dummies[i].second.timerOverlapExp;

		if (false == dummy.Connect(ip.c_str()))
		{
			std::cout << "AddDummy() Error 더미클라이언트 Connect 실패, Serial : " << i << '\n';
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


		::ZeroMemory(&sendOverlap, sizeof(sendOverlap));
		sendOverlap.WsaBuf.len = sizeof(sendOverlap.Iocp_Buffer);
		sendOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(sendOverlap.Iocp_Buffer);
		sendOverlap.Operation = OPERATION_SEND;
		
		::ZeroMemory(&timerOverlap, sizeof(timerOverlap));
		timerOverlap.WsaBuf.len = sizeof(timerOverlap.Iocp_Buffer);
		timerOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(timerOverlap.Iocp_Buffer);
		timerOverlap.Operation = OPERATION_RANDPACKET;

		packet_login* my_packet = reinterpret_cast<packet_login*>(sendOverlap.Iocp_Buffer);
		my_packet->Size = sizeof(packet_login);
		my_packet->Type = PACKET_LOGIN;
		my_packet->Created = false;
		std::string id("Dummy" + std::to_string(i));
		std::memcpy(&(my_packet->User), id.c_str(), id.size());
		
		SendPacket(i, reinterpret_cast<unsigned char*>(my_packet));

		++lastSerial;
		//::Sleep(10);
	}

	std::cout << count << "명 로그인 완료\n";
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

	std::cout << count << "명 종료 완료\n";
	return true;
}

////////////////////////////////////////////////////////////////////////////
//Packet Handling
void DummyHandler::SendPacket(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial || packet == nullptr)
		return;

	Dummy& dummy = dummies[serial].first;
	Overlap_Exp& sendOverlapExp = dummies[serial].second.sendOverlapExp;
	sendOverlapExp.WsaBuf.buf = reinterpret_cast<CHAR*>(sendOverlapExp.Iocp_Buffer);
	sendOverlapExp.WsaBuf.len = GetPacketSize(packet);
	sendOverlapExp.Operation = OPERATION_SEND;

	int ret = ::WSASend(dummy.clientSocket, &sendOverlapExp.WsaBuf, 1, NULL, 0,
		&sendOverlapExp.Original_Overlap, NULL);

	if (0 != ret)
	{
		int error_no = WSAGetLastError();
		if (WSA_IO_PENDING != error_no)
			std::cout << "SendPacket() - WSASend " << "error code : " << error_no << std::endl;
	}
}

void DummyHandler::ProcessPacket(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial || packet == nullptr)
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
	if (serial < 0 || MAX_DUMMY_COUNT <= serial)
		return;

	packet_login* my_packet = reinterpret_cast<packet_login*>(packet);
	dummies[serial].first.userName = my_packet->User;
	dummies[serial].first.isLogin = true;

	AddRandomPacketEvent(serial);
}

void DummyHandler::ProcessChannelEnter(int serial, unsigned char* packet)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	packet_channel_enter* my_packet = reinterpret_cast<packet_channel_enter*>(packet);
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

	std::unique_lock<std::mutex> ul(timerLock);
	eventInfo.Wakeup_Time = ::GetTickCount() + PACKET_DELAY_TIME;
	timerQueue.push(eventInfo);
}

void DummyHandler::RequestRandomPacket(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;
	if (dummies[serial].first.isLogin == false) return;

	std::uniform_real_distribution<double> nd(0.0, 1.0);
	
	static constexpr double	COEF_CHATTING = 0.20;				
	static constexpr double	COEF_WHISPER = 0.40;				
	static constexpr double	COEF_CHANNELLIST = 0.60;			
	static constexpr double	COEF_CHANNELCHANGE = 0.80;			
	static constexpr double	COEF_KICK = 1.0;					

	double coef = nd(RANDOM_ENGINE);
	Dummy& dummy = dummies[serial].first;

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
	Overlap_Exp& sendOverlapExp = dummies[serial].second.sendOverlapExp;

	std::string randomListener = GetRandomUser();
	if (randomListener.empty() 
		|| (randomListener == dummy.userName)) 
		return;

	std::string chat("This is test! ID : " + std::to_string(serial));
	
	packet_chatting* my_packet = reinterpret_cast<packet_chatting *>(sendOverlapExp.Iocp_Buffer);
	::ZeroMemory(my_packet, sizeof(packet_chatting));
	my_packet->Size = sizeof(packet_chatting);
	my_packet->Type = PACKET_CHATTING;
	my_packet->IsWhisper = true;

	std::memcpy(&(my_packet->Talker), dummy.userName.c_str(), dummy.userName.size());
	std::memcpy(&(my_packet->Listner), randomListener.c_str(), randomListener.size());
	std::memcpy(&(my_packet->Chat), chat.c_str(), chat.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(my_packet));
}

void DummyHandler::RequestChannelList(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial)
		return;

	Overlap_Exp& sendOverlapExp = dummies[serial].second.sendOverlapExp;
	packet_channel_list* my_packet = reinterpret_cast<packet_channel_list *>(sendOverlapExp.Iocp_Buffer);
	::ZeroMemory(my_packet, sizeof(packet_channel_list));
	my_packet->Size = sizeof(packet_channel_list);
	my_packet->Type = PACKET_CHANNEL_LIST;

	SendPacket(serial, reinterpret_cast<unsigned char*>(my_packet));
}

void DummyHandler::RequestChannelChange(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial)
		return;

	std::string randomChannel = GetRandomChannel();
	if (randomChannel.empty()
		|| (randomChannel == dummies[serial].first.userChannel))
		return;

	Overlap_Exp& sendOverlapExp = dummies[serial].second.sendOverlapExp;
	packet_channel_enter* my_packet = reinterpret_cast<packet_channel_enter *>(sendOverlapExp.Iocp_Buffer);
	::ZeroMemory(my_packet, sizeof(packet_channel_enter));
	my_packet->Size = sizeof(packet_channel_enter);
	my_packet->Type = PACKET_CHANNEL_ENTER;
	std::memcpy(&(my_packet->ChannelName), randomChannel.c_str(), randomChannel.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(my_packet));
}

void DummyHandler::RequestKick(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial) return;

	Dummy& dummy = dummies[serial].first;
	Overlap_Exp& sendOverlapExp = dummies[serial].second.sendOverlapExp;

	packet_kick_user* my_packet = reinterpret_cast<packet_kick_user *>(sendOverlapExp.Iocp_Buffer);
	::ZeroMemory(my_packet, sizeof(packet_kick_user));
	my_packet->Size = sizeof(packet_kick_user);
	my_packet->Type = PACKET_KICK_USER;

	std::string randomTarget = GetRandomUser();
	if (randomTarget.empty()
		|| (randomTarget == dummy.userName))
		return;

	std::memcpy(&(my_packet->Target), randomTarget.c_str(), randomTarget.size());
	std::memcpy(&(my_packet->Kicker), dummy.userName.c_str(), dummy.userName.size());
	std::memcpy(&(my_packet->Channel), dummy.userChannel.c_str(), dummy.userChannel.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(my_packet));
}

void DummyHandler::RequestChatting(int serial)
{
	if (serial < 0 || MAX_DUMMY_COUNT <= serial)
		return;

	Dummy& dummy = dummies[serial].first;
	Overlap_Exp& sendOverlapExp = dummies[serial].second.sendOverlapExp;

	packet_chatting* my_packet = reinterpret_cast<packet_chatting *>(sendOverlapExp.Iocp_Buffer);
	::ZeroMemory(my_packet, sizeof(packet_chatting));
	my_packet->Size = sizeof(packet_chatting);
	my_packet->Type = PACKET_CHATTING;
	my_packet->IsWhisper = false;

	std::string chat("This is test! ID : " + std::to_string(serial));

	std::memcpy(&(my_packet->Talker), dummy.userName.c_str(), dummy.userName.size());
	std::memcpy(&(my_packet->Chat), chat.c_str(), chat.size());

	SendPacket(serial, reinterpret_cast<unsigned char*>(my_packet));
}


////////////////////////////////////////////////////////////////////////////
//Private 
std::string DummyHandler::GetRandomUser() const
{
	std::uniform_int_distribution<int> uid(0, lastSerial - 1);
	
	const int slot = uid(RANDOM_ENGINE);
	const Dummy& dummy = dummies[slot].first;
	if (dummy.isLogin)
		return dummy.userName;
	else
		return "";
}

std::string DummyHandler::GetRandomChannel() const
{
	std::uniform_int_distribution<int> uid(0, lastSerial - 1);
	
	static const char* chars = "0123456789"
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static const int charsCount = ::strlen(chars);

	const int slot = uid(RANDOM_ENGINE);
	const Dummy& randomDummy = dummies[slot].first;
	if (randomDummy.isLogin == false)
		return "";

	if (slot >= (MAX_CHANNELNAME_LENGTH / 4))
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
						std::cout << "AddDummy() - WSARecv " << "error code : " << error_no << std::endl;
				}
			}
			else if (OPERATION_SEND == overlapExp->Operation)
			{	
				::ZeroMemory(overlapExp->Iocp_Buffer, sizeof(overlapExp->Iocp_Buffer));
			}
			else if (OPERATION_RANDPACKET == overlapExp->Operation)
			{
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

				handler->GetDummies()[ev.Serial].second.timerOverlapExp.Operation = ev.Event_Type;
				PostQueuedCompletionStatus(handler->GetIocpHandle(), sizeof(Overlap_Exp),
					ev.Serial, &(handler->GetDummies()[ev.Serial].second.timerOverlapExp.Original_Overlap));

				ul.lock();
			}
		}
	}

} //unnamed namespace
