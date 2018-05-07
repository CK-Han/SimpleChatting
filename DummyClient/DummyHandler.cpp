#include "DummyHandler.h"
#include <iostream>
#include <fstream>

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
}

DummyHandler::~DummyHandler()
{
	Close();
}

/**
	@brief		���α׷� ����, ���� close �� thread join ,WSACleanup
*/
void DummyHandler::Close()
{
	isShutdown = true;

	std::cout << "���� ���Ḧ ���� ��� ����մϴ�...\n";
	for (auto& dummy : dummies)
	{
		if (dummy.first.isLogin)
		{
			dummy.first.Close();
		}
		::Sleep(1);
	}
	::Sleep(2000);

	for (auto& th : workerThreads)
		th->join();
	timerThread->join();

	::WSACleanup();
}

////////////////////////////////////////////////////////////////////////////
/**
	@brief		���ǵ� START DUMMY ������ŭ connect �� ���̵� ����, ���ν��� ��� �� ������ ����
	@return		����ä�� ����Ʈ ���� �б�, ���� Ȱ��ȭ, ���ν��� ���, ������ ���� �Ϸ� �� true
				false ��ȯ�� �ֿܼ� ������ ���� ���
*/
bool DummyHandler::Start(const std::string& ip)
{
	if (isInitialized == true)	return false;

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
		if(token.empty() == false)
			publicChannels.push_back(token);
	}
	publicChannelFile.close();

	//SendOverlap �ʱ�ȭ
	for (auto i = 0; i < MAX_OVERLAPEXP_COUNT; ++i)
		validOverlapExpSerials.push(i);

	try
	{
		sendOverlapExps.reserve(MAX_OVERLAPEXP_COUNT);
		for (auto i = 0; i < MAX_OVERLAPEXP_COUNT; ++i)
			sendOverlapExps.emplace_back(new Overlap_Exp);
	}
	catch (const std::bad_alloc&)
	{
		std::cout << "Start() - SendOverlap bad alloc\n";
	}

	//���� Ŭ���̾�Ʈ Ȱ��ȭ, �α���
	if (false == AddDummy(START_DUMMY_COUNT, ip))
	{
		std::cout << "Start() fail!\n";
		return false;
	}

	//��Ŷ ó���Լ� map
	packetProcedures.insert(std::make_pair(Packet_Login::typeAdder.GetType(), &DummyHandler::Process_Login));
	packetProcedures.insert(std::make_pair(Packet_Channel_Enter::typeAdder.GetType(), &DummyHandler::Process_ChannelEnter));
	packetProcedures.insert(std::make_pair(Packet_User_Leave::typeAdder.GetType(), &DummyHandler::Process_UserLeave));

	//������ ����, ����
	try
	{
		for (auto i = 0; i < NUM_WORKER_THREADS; ++i)
			workerThreads.emplace_back(new std::thread(WorkerThreadStart));

		timerThread = std::unique_ptr<std::thread>(new std::thread(TimerThreadStart));
	}
	catch (const std::bad_alloc&)
	{
		std::cout << "Start() - thread bad alloc\n";
	}

	isInitialized = true;
	return isInitialized;
}

/**
	@brief		���� �߰�, connect �� login ��û
	@return		false - �����Ϸ��� dummy�� MAX_DUMMY_COUNT�� ����� ���
				true - �Լ� ����� ��ȯ, ������ ó���� ������ ��쿡�� �ֿܼ� ���� ���
*/
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
	SERIAL_TYPE serialOffset = lastSerial;
	for (SERIAL_TYPE i = 0; i < count; ++i)
	{
		SERIAL_TYPE newSerial = serialOffset + i;
		Dummy& dummy = dummies[newSerial].first;
		Overlap_Exp& recvOverlap = dummies[newSerial].second.RecvOverlapExp;

		if (false == dummy.Connect(ip.c_str()))
		{
			std::cout << "AddDummy() Connect Error, Serial : " << newSerial << '\n';
			continue;
		}
	
		auto H = ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(dummy.clientSocket), hIocp, newSerial, 0);
		H;
		++lastSerial;

		//recv ���
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
		Sleep(1);
	}

	std::cout << lastSerial - serialOffset << " dummies login!\n";
	return true;
}


/**
	@brief		���� ���� ����, ������� ���� ���� �� �������� ����
*/
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

DummyHandler::SERIAL_TYPE DummyHandler::GetSerialForUseOverlapExp()
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

void DummyHandler::ReturnUsedOverlapExp(SERIAL_TYPE serial)
{
	if (MAX_OVERLAPEXP_COUNT <= serial) return;
	validOverlapExpSerials.push(serial);
}

////////////////////////////////////////////////////////////////////////////
//Packet Handling

/**
	@brief		Serialize�� ��Ŷ ������ ����
	@details	Overlap_Exp ����ü ����, �ʱ�ȭ, overlap WSASend�� ���� 
*/
void DummyHandler::SendPacket(SERIAL_TYPE serial, const void* packet)
{
	if (IsValidSerial(serial) == false
		|| packet == nullptr)
		return;

	/*
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
	*/

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
	memcpy(sendOverlapExps[overlapSerial]->Iocp_Buffer, packet, sendOverlapExps[overlapSerial]->WsaBuf.len);

	int ret = ::WSASend(dummies[serial].first.GetSocket(), &sendOverlapExps[overlapSerial]->WsaBuf, 1, NULL, 0,
		&sendOverlapExps[overlapSerial]->Original_Overlap, NULL);

	if (0 != ret)
	{
		int error_no = WSAGetLastError();
		if (WSA_IO_PENDING != error_no
			&& WSAECONNABORTED != error_no)
			std::cout << "SendPacket() - WSASend " << "error code : " << error_no << std::endl;
	}
}

/**
	@brief		��Ŷ Ÿ���� ���� ��ϵ� ���ν��� ����
	@warning	ó���ؾ��� ���ν����� ��ϵǾ�������, type�� ���ǵ��� ���� ���̴��� �׿� ���� ó���� �������� ����
*/
void DummyHandler::ProcessPacket(SERIAL_TYPE serial, const void* packet, int size)
{
	if (IsValidSerial(serial) == false
		|| packet == nullptr)
		return;

	auto type = GetPacketType(packet);
	StreamReader stream(packet, size);

	auto procedure = packetProcedures.find(type);
	if (procedure != packetProcedures.end())
		(this->*packetProcedures[type])(serial, stream);
}


////////////////////////////////////////////////////////////////////////////
//Process

/**
	@brief		id ���� ����, Ÿ�̸ӿ� ���� ��Ŷ �߼��� ����Ѵ�.
*/
void DummyHandler::Process_Login(SERIAL_TYPE serial, StreamReader& in)
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

void DummyHandler::Process_ChannelEnter(SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidSerial(serial) == false)	return;

	Packet_Channel_Enter enterPacket;
	enterPacket.Deserialize(in);

	std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
	dummies[serial].first.userChannel = enterPacket.channelName;
}

/**
	@brief		���� �ڽ��� ��������� ���� ó���� �����Ѵ�.
	@details	�������� ���� ���� �ٸ� ä�η� ����Ǳ� �� ������ ä���� void�� �����Ѵ�.
*/
void DummyHandler::Process_UserLeave(SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidSerial(serial) == false)	return;

	Packet_User_Leave leavePacket;
	leavePacket.Deserialize(in);
	
	if (leavePacket.isKicked == true
		|| leavePacket.userName == dummies[serial].first.userName)
	{
		std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
		dummies[serial].first.userChannel.clear(); //void ä��
	}
}

////////////////////////////////////////////////////////////////////////////
//Request

/**
	@brief		�Լ� �� ������ Ȯ���� ���� ������ ������ ��Ŷ�� ������ �� �ٽ� Ÿ�̸ӿ� ����Ѵ�.
*/
void DummyHandler::SendRandomPacket(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	static const std::uniform_real_distribution<double> urd(0.0, 1.0);

	static const double	COEF_CHATTING = 0.20;
	static const double	COEF_WHISPER = 0.40;
	static const double	COEF_CHANNELLIST = 0.60;
	static const double	COEF_CHANNELCHANGE = 0.80;
	static const double	COEF_KICK = 1.0;

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

/**
	@brief		Ÿ�̸� ť�� �ش� ���̰� �ٽ� ������ ��Ŷ�� �߼��ϵ��� ����Ѵ�.
	@details	Ŭ���� �� PACKET_DELAY_TIME�� ����Ͽ� ���
*/
void DummyHandler::AddRandomPacketEvent(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Event_Info eventInfo;
	eventInfo.Serial = static_cast<int>(serial);
	eventInfo.Event_Type = OPERATION_RANDPACKET;
	
	std::unique_lock<std::mutex> ulTimer(timerLock);
	eventInfo.Wakeup_Time = ::GetTickCount() + PACKET_DELAY_TIME;
	timerQueue.push(eventInfo);
}


void DummyHandler::RequestWhisper(SERIAL_TYPE serial)
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

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter chatStream(buf, sizeof(buf));
	chatPacket.Serialize(chatStream);

	SendPacket(serial, chatStream.GetBuffer());
}

void DummyHandler::RequestChannelList(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Packet_Channel_List listPacket;
	listPacket.customChannelCount = 0;
	listPacket.publicChannelNames.clear();

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter listStream(buf, sizeof(buf));
	listPacket.Serialize(listStream);

	SendPacket(serial, listStream.GetBuffer());
}

void DummyHandler::RequestChannelChange(SERIAL_TYPE serial)
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

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter enterStream(buf, sizeof(buf));
	enterPacket.Serialize(enterStream);

	SendPacket(serial, enterStream.GetBuffer());
}

/**
	@brief		�����û ��Ŷ�� �����Ѵ�. ����� ä���� �ٸ��ų� ������ ������ �ƴ϶� ����ȴ�.
*/
void DummyHandler::RequestKick(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Dummy& dummy = dummies[serial].first;
	
	std::string randomTarget = GetRandomUser();
	if (randomTarget.empty()
		|| (randomTarget == dummy.userName))
		return;

	Packet_Kick_User kickPacket;
	kickPacket.target = randomTarget;
	kickPacket.kicker = dummy.userName;
	kickPacket.channelName = dummy.userChannel;

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter kickStream(buf, sizeof(buf));
	kickPacket.Serialize(kickStream);

	SendPacket(serial, kickStream.GetBuffer());
}

void DummyHandler::RequestChatting(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	if (dummies[serial].first.userChannel.empty()) return;	//void ä�� ����

	Dummy& dummy = dummies[serial].first;
	std::string chat("This is test! ID : " + std::to_string(serial));

	Packet_Chatting chatPacket;
	chatPacket.isWhisper = false;
	chatPacket.talker = dummy.userName;
	chatPacket.listener.clear();
	chatPacket.chat = chat;

	unsigned char buf[Packet_Base::MAX_BUF_SIZE];
	StreamWriter chatStream(buf, sizeof(buf));
	chatPacket.Serialize(chatStream);

	SendPacket(serial, chatStream.GetBuffer());
}


////////////////////////////////////////////////////////////////////////////
//Private 

/**
	@brief		connect �Ǿ��ִ� ������ ���� �� �ϳ��� �̸��� ��´�.
	@return		���̰� �α��εǾ��ִٸ� �� ������ userName
				connect�̳� login���� �ʾҴٸ� empty string ��ȯ
*/
std::string DummyHandler::GetRandomUser() const
{
	std::uniform_int_distribution<SERIAL_TYPE> uid(lastToCloseSerial, lastSerial - 1);
	
	const SERIAL_TYPE slot = uid(RANDOM_ENGINE);
	const Dummy& dummy = dummies[slot].first;
	if (dummy.isLogin)
		return dummy.userName;
	else
		return "";
}

/**
	@brief		connect �Ǿ��ִ� ������ ���� �� �ϳ��� ����� ä�� �̸� or ����ä�� or ������ ä�� �̸��� ��´�.
	@return		�� �� ��� �� �ϳ��� ä�� �̸����� ��´�.
				������ ���̰� login ���°� �ƴ϶�� empty string�� ��ȯ
*/
std::string DummyHandler::GetRandomChannel() const
{
	std::uniform_int_distribution<SERIAL_TYPE> uid(lastToCloseSerial, lastSerial - 1);
	
	static const char* chars = "0123456789"
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static const size_t charsCount = ::strlen(chars);

	const SERIAL_TYPE slot = uid(RANDOM_ENGINE);
	const Dummy& randomDummy = dummies[slot].first;
	if (randomDummy.isLogin == false)
		return "";

	std::uniform_real_distribution<double> urd(0.0, 1.0);
	static const double	COEF_PUBLIC = 0.70;
	static const double	COEF_TOUSER = 0.90;
	static const double COEF_CUSTOM = 1.0;
	
	double coef = urd(RANDOM_ENGINE);
	if (0.0 <= coef && coef < COEF_PUBLIC)
		return publicChannels[slot % publicChannels.size()];
	else if (COEF_PUBLIC <= coef && coef < COEF_TOUSER)
		return randomDummy.userChannel;
	else //Ŀ���� ä�η�
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
	/**
		@brief		��Ŀ������ �����Լ�, ��Ŷ ���� �� ó���Լ� ȣ��, �ڿ� ��ȯ ����
	*/
	void WorkerThreadStart()
	{
		DummyHandler* handler = DummyHandler::GetInstance();

		while (false == handler->IsShutdown())
		{
			DWORD iosize = 0;
			unsigned long long serial = 0;
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
			if (0 == iosize)
			{
				handler->GetDummies()[serial].first.Close();
				continue;
			}
		
			Dummy& dummy = handler->GetDummies()[serial].first;
			Overlap_Info& overlapInfo = handler->GetDummies()[serial].second;

			if (OPERATION_RECV == overlapExp->Operation)
			{
				unsigned char* ioPtr = overlapExp->Iocp_Buffer;
				int remained = 0;

				if ((overlapInfo.PreviousCursor + iosize) > Packet_Base::MAX_BUF_SIZE)
				{
					int empty = Packet_Base::MAX_BUF_SIZE - overlapInfo.PreviousCursor;
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousCursor, ioPtr, empty);

					remained = iosize - empty;
					ioPtr += empty;
					overlapInfo.PreviousCursor += empty;
				}
				else
				{
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousCursor, ioPtr, iosize);
					overlapInfo.PreviousCursor += iosize;
				}

				do
				{
					overlapInfo.PacketSize = GetPacketSize(overlapInfo.PacketBuff);

					if (overlapInfo.PacketSize <= overlapInfo.PreviousCursor)
					{	//��������
						handler->ProcessPacket(serial, overlapInfo.PacketBuff, overlapInfo.PacketSize);
						std::memmove(overlapInfo.PacketBuff, overlapInfo.PacketBuff + overlapInfo.PacketSize
							, overlapInfo.PreviousCursor - overlapInfo.PacketSize);

						overlapInfo.PreviousCursor -= overlapInfo.PacketSize;
						overlapInfo.PacketSize = 0;

						if (remained > 0
							&& (overlapInfo.PreviousCursor + remained) <= sizeof(overlapInfo.PacketBuff))
						{
							std::memcpy(overlapInfo.PacketBuff + overlapInfo.PreviousCursor, ioPtr, remained);
							overlapInfo.PreviousCursor += remained;
							remained = 0;
						}
					}
				} while (overlapInfo.PacketSize == 0);
			
				DWORD flags = 0;
				int recvRet = ::WSARecv(dummy.GetSocket(),
					&overlapInfo.RecvOverlapExp.WsaBuf, 1, NULL, &flags,
					&overlapInfo.RecvOverlapExp.Original_Overlap, NULL);
				if (0 != recvRet)
				{
					int error_no = WSAGetLastError();
					if (WSA_IO_PENDING != error_no)
						std::cout << "WorkerThreadStart() - WSARecv " << "error code : " << error_no << std::endl;
				}
			}
			else if (OPERATION_SEND == overlapExp->Operation)
			{	
				//delete overlapExp;
				handler->ReturnUsedOverlapExp(overlapExp->Serial);
			}
			else if (OPERATION_RANDPACKET == overlapExp->Operation)
			{
				//delete overlapExp;
				handler->ReturnUsedOverlapExp(overlapExp->Serial);
				//������Ŷ �߼�, �� �Լ������� �ٽ� Ÿ�̸� ����Ѵ�.
				handler->SendRandomPacket(serial);
			}
			else
			{
				std::cout << "Unknown IOCP event!\n";
			}
		}
	}

	/**
		@brief		timer_queue�� ����Ͽ� ������Ŷ ���� ����
	*/
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


				auto overlapSerial = handler->GetSerialForUseOverlapExp();
				if (DummyHandler::SERIAL_ERROR == overlapSerial)
				{
					std::cout << "SendPacket() - GetSerialForUseOverlapExp timeout\n";
					break;
				}

				auto& overlapExp = handler->GetSendOverlapExp(overlapSerial);
				::ZeroMemory(&overlapExp, sizeof(overlapExp));
				overlapExp.Serial = overlapSerial;
				overlapExp.Operation = OPERATION_RANDPACKET;
				PostQueuedCompletionStatus(handler->GetIocpHandle(), 1,
					ev.Serial, &overlapExp.Original_Overlap);
				
				/*
				try
				{
					Overlap_Exp* overlapExp = new Overlap_Exp;
					::ZeroMemory(overlapExp, sizeof(Overlap_Exp));
					overlapExp->Operation = OPERATION_RANDPACKET;
					PostQueuedCompletionStatus(handler->GetIocpHandle(), 1,
						ev.Serial, &overlapExp->Original_Overlap);
				}
				catch (const std::bad_alloc&)
				{
					std::cout << "TimerThreadStart() - new exception\n";
				}
				*/
				ul.lock();
			}
		}
	}

} //unnamed namespace
