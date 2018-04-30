#include "DummyHandler.h"
#pragma comment (lib, "ws2_32.lib")
#include <iostream>


namespace
{
	void WorkerThreadStart();
	void TimerThreadStart();

} //unnamed namespace

thread_local std::default_random_engine DummyHandler::RANDOM_ENGINE;

DummyHandler::DummyHandler()
	: hIocp(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0))
	, isInitialized(false)
	, isShutdown(false)
	, lastSerial(0)
	, lastToCloseSerial(0)
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

	for (auto& th : workerThreads)
		th->join();
	timerThread->join();

	::WSACleanup();
}

////////////////////////////////////////////////////////////////////////////

bool DummyHandler::Start(const std::string& ip)
{
	if (isInitialized == true)	return false;

	//더미 클라이언트 활성화, 로그인
	if (false == AddDummy(START_DUMMY_COUNT, ip))
	{
		std::cout << "Start() fail!\n";
		return false;
	}

	packetProcedures.insert(std::make_pair(Packet_Login::typeAdder.GetType(), &DummyHandler::Process_Login));
	packetProcedures.insert(std::make_pair(Packet_Channel_Enter::typeAdder.GetType(), &DummyHandler::Process_ChannelEnter));

	try
	{
		for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
			workerThreads.emplace_back(new std::thread(WorkerThreadStart));

		timerThread = std::unique_ptr<std::thread>(new std::thread(TimerThreadStart));
	}
	catch (const std::bad_alloc&)
	{
		std::cout << "Start() - thread new bad alloc\n";
	}

	isInitialized = true;
	return isInitialized;
}

bool DummyHandler::AddDummy(unsigned int count
	, const std::string& ip)
{
	if (lastSerial + count > MAX_DUMMY_COUNT)
	{
		std::cout << "AddDummy() - too many dummies\n";
		return false;
	}

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	Packet_Login loginPacket;
	int serialOffset = lastSerial;
	for (unsigned int i = 0; i < count; ++i)
	{
		int newSerial = serialOffset + i;
		Dummy& dummy = dummies[newSerial].first;
		Overlap_Exp& recvOverlap = dummies[newSerial].second.recvOverlapExp;

		if (false == dummy.Connect(ip.c_str()))
		{
			std::cout << "AddDummy() Connect Error, Serial : " << newSerial << '\n';
			continue;
		}
	
		::CreateIoCompletionPort(reinterpret_cast<HANDLE>(dummy.clientSocket), hIocp, newSerial, 0);
		++lastSerial;

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

		std::string id("Dummy" + std::to_string(newSerial));
		loginPacket.isCreated = false;
		loginPacket.userName = id;
		StreamWriter loginStream(buf, sizeof(buf));
		loginPacket.Serialize(loginStream);
		
		SendPacket(newSerial, loginStream.GetBuffer());

		::Sleep(1);
	}

	std::cout << lastSerial - serialOffset << " dummies login!\n";
	return true;
}

bool DummyHandler::CloseDummy(unsigned int count)
{
	int closedDummyCount = 0;
	for (unsigned int i = 0; i < count; ++i)
	{
		if (IsValidSerial(lastToCloseSerial + i) == false)
			return false;
		
		Dummy& dummy = dummies[lastToCloseSerial + i].first;
		if (dummy.isLogin)
		{
			dummy.Close();
			lastToCloseSerial++;
			closedDummyCount++;
			::Sleep(1);
		}
	}

	std::cout << closedDummyCount << " dummies closed\n";
	return true;
}

bool DummyHandler::IsValidSerial(int serial) const
{
	return (lastToCloseSerial <= serial && serial < lastSerial) ? true : false;
}

////////////////////////////////////////////////////////////////////////////
//Packet Handling
void DummyHandler::SendPacket(int serial, const void* packet) const
{
	if (IsValidSerial(serial) == false
		|| packet == nullptr)
		return;

	Overlap_Exp* overlapExp = nullptr;
	try
	{
		overlapExp = new Overlap_Exp;
	}
	catch (const std::bad_alloc&)
	{
		std::cout << "SendPacket() - new exception\n";
		return;
	}

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

void DummyHandler::ProcessPacket(int serial, const void* packet, int size)
{
	if (IsValidSerial(serial) == false
		|| packet == nullptr)
		return;

	auto type = GetPacketType(packet);
	StreamReader stream(packet, size);

	auto procedure = packetProcedures.find(type);
	if (procedure == packetProcedures.end())
		; //무시하는 패킷들
	else
		(this->*packetProcedures[type])(serial, stream);
}


////////////////////////////////////////////////////////////////////////////
//Process
void DummyHandler::Process_Login(int serial, StreamReader& in)
{
	if (IsValidSerial(serial) == false) return;

	Packet_Login loginPacket;
	loginPacket.Deserialize(in);

	std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
	dummies[serial].first.userName = loginPacket.userName;
	dummies[serial].first.isLogin = loginPacket.isCreated;
	ulDummy.unlock();

	AddRandomPacketEvent(serial);
}

void DummyHandler::Process_ChannelEnter(int serial, StreamReader& in)
{
	if (IsValidSerial(serial) == false)	return;

	Packet_Channel_Enter enterPacket;
	enterPacket.Deserialize(in);

	std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
	dummies[serial].first.userChannel = enterPacket.channelName;
}

////////////////////////////////////////////////////////////////////////////
//Request
void DummyHandler::SendRandomPacket(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

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

void DummyHandler::AddRandomPacketEvent(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Event_Info eventInfo;
	eventInfo.Serial = serial;
	eventInfo.Event_Type = OPERATION_RANDPACKET;
	
	std::unique_lock<std::mutex> ulTimer(timerLock);
	eventInfo.Wakeup_Time = ::GetTickCount() + PACKET_DELAY_TIME;
	timerQueue.push(eventInfo);
}

void DummyHandler::RequestWhisper(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Dummy& dummy = dummies[serial].first;
	
	std::string randomListener = GetRandomUser();
	if (randomListener.empty() 
		|| (randomListener == dummy.userName))
		return;

	std::string chat("This is whisper! ID : " + std::to_string(serial));
	
	Packet_Chatting chatPacket;
	chatPacket.isWhisper = true;
	chatPacket.talker = dummy.userName;
	chatPacket.listener = randomListener;
	chatPacket.chat = chat;

	unsigned char* buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter chatStream(buf, sizeof(buf));
	chatPacket.Serialize(chatStream);

	SendPacket(serial, chatStream.GetBuffer());
}

void DummyHandler::RequestChannelList(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Packet_Channel_List listPacket;
	listPacket.customChannelCount = 0;
	listPacket.publicChannelNames.clear();

	unsigned char* buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter listStream(buf, sizeof(buf));
	listPacket.Serialize(listStream);

	SendPacket(serial, listStream.GetBuffer());
}

void DummyHandler::RequestChannelChange(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	std::string randomChannel = GetRandomChannel();
	if (randomChannel.empty()
		|| (randomChannel == dummies[serial].first.userChannel))
		return;

	Packet_Channel_Enter enterPacket;
	enterPacket.channelName = randomChannel;
	enterPacket.channelMaster.clear();

	unsigned char* buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter enterStream(buf, sizeof(buf));
	enterPacket.Serialize(enterStream);

	SendPacket(serial, enterStream.GetBuffer());
}

void DummyHandler::RequestKick(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Dummy& dummy = dummies[serial].first;
	
	//채널이 다르거나 방장이 아니라도 강퇴 요청은 전송한다.
	std::string randomTarget = GetRandomUser();
	if (randomTarget.empty()
		|| (randomTarget == dummy.userName))
		return;

	Packet_Kick_User kickPacket;
	kickPacket.target = randomTarget;
	kickPacket.kicker = dummy.userName;
	kickPacket.channelName = dummy.userChannel;

	unsigned char* buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter kickStream(buf, sizeof(buf));
	kickPacket.Serialize(kickStream);

	SendPacket(serial, kickStream.GetBuffer());
}

void DummyHandler::RequestChatting(int serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Dummy& dummy = dummies[serial].first;
	std::string chat("This is test! ID : " + std::to_string(serial));

	Packet_Chatting chatPacket;
	chatPacket.isWhisper = false;
	chatPacket.talker = dummy.userName;
	chatPacket.listener.clear();
	chatPacket.chat = chat;

	unsigned char* buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter chatStream(buf, sizeof(buf));
	chatPacket.Serialize(chatStream);

	SendPacket(serial, chatStream.GetBuffer());
}


////////////////////////////////////////////////////////////////////////////
//Private 
std::string DummyHandler::GetRandomUser() const
{
	std::uniform_int_distribution<int> uid(lastToCloseSerial, lastSerial - 1);
	
	const int slot = uid(RANDOM_ENGINE);
	const Dummy& dummy = dummies[slot].first;
	if (dummy.isLogin)
		return dummy.userName;
	else
		return "";
}

std::string DummyHandler::GetRandomChannel() const
{
	std::uniform_int_distribution<int> uid(lastToCloseSerial, lastSerial - 1);
	
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
		const int channelLength = (slot % (Packet_Base::MAX_CHANNELNAME_SIZE - 1)) + 1;

		for (int i = 0; i < channelLength; ++i)
		{
			channelName += chars[uid(RANDOM_ENGINE) % charsCount];
		}

		return channelName;
	}
}


////////////////////////////////////////////////////////////////////////////
namespace
{
	void WorkerThreadStart()
	{
		DummyHandler* handler = DummyHandler::GetInstance();

		while (false == handler->IsShutdown())
		{
			DWORD iosize = 0;
			DWORD serial = 0;
			Overlap_Exp* overlapExp;

			BOOL result = GetQueuedCompletionStatus(handler->GetIocpHandle(),
				&iosize, &serial, reinterpret_cast<LPOVERLAPPED*>(&overlapExp), INFINITE);

			if (result == false
				&& overlapExp->Original_Overlap.Pointer != nullptr)
			{
				std::cout << "WorkerThreadStart() - GetQueuedCompletionStatus error\n";
				return;
			}
			if (DummyHandler::GetInstance()->IsValidSerial(serial) == false)
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
				unsigned char* ptrBuf = overlapExp->Iocp_Buffer;
				int remained = 0;

				if ((overlapInfo.PreviousCursor + iosize) > Packet_Base::MAX_BUF_SIZE)
				{
					int empty = Packet_Base::MAX_BUF_SIZE - overlapInfo.PreviousCursor;
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousCursor, ptrBuf, empty);

					remained = iosize - empty;
					ptrBuf += empty;
					overlapInfo.PreviousCursor += empty;
				}
				else
				{
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousCursor, ptrBuf, iosize);
					overlapInfo.PreviousCursor += iosize;
				}

				do
				{
					overlapInfo.PacketSize = GetPacketSize(overlapInfo.PacketBuff);

					if (overlapInfo.PacketSize <= overlapInfo.PreviousCursor)
					{	//조립가능
						handler->ProcessPacket(serial, overlapInfo.PacketBuff, overlapInfo.PacketSize);
						std::memmove(overlapInfo.PacketBuff, overlapInfo.PacketBuff + overlapInfo.PacketSize
							, overlapInfo.PreviousCursor - overlapInfo.PacketSize);

						overlapInfo.PreviousCursor -= overlapInfo.PacketSize;
						overlapInfo.PacketSize = 0;
					}
				} while (overlapInfo.PacketSize == 0);

				if (remained > 0)
				{
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousCursor, ptrBuf, remained);
					overlapInfo.PreviousCursor += remained;
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
				handler->SendRandomPacket(serial);
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
