#include "DummyHandler.h"
#include <iostream>
#include <fstream>

namespace
{
	void WorkerThreadStart();
	void TimerThreadStart();

} //unnamed namespace

thread_local std::default_random_engine DummyHandler::RANDOM_ENGINE;

/**
	@brief		멤버변수 초기화, iocp 초기화 및 윈속 초기화를 진행한다.
*/
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
	@brief		프로그램 종료, 소켓 close 및 thread join ,WSACleanup
*/
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
/**
	@brief		정의된 START DUMMY 개수만큼 connect 및 아이디 생성, 프로시저 등록 및 스레드 생성
	@return		공개채널 리스트 파일 읽기, 더미 활성화, 프로시저 등록, 스레드 생성 완료 시 true
				false 반환시 콘솔에 실패한 정보 출력
*/
bool DummyHandler::Start(const std::string& ip)
{
	if (isInitialized == true)	return false;

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
		if(token.empty() == false)
			publicChannels.push_back(token);
	}
	publicChannelFile.close();

	//SendOverlap 초기화
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

	//더미 클라이언트 활성화, 로그인
	if (false == AddDummy(START_DUMMY_COUNT, ip))
	{
		std::cout << "Start() fail!\n";
		return false;
	}

	//패킷 처리함수 map
	packetProcedures.insert(std::make_pair(Packet_Login::typeAdder.GetType(), &DummyHandler::Process_Login));
	packetProcedures.insert(std::make_pair(Packet_Channel_Enter::typeAdder.GetType(), &DummyHandler::Process_ChannelEnter));
	packetProcedures.insert(std::make_pair(Packet_User_Leave::typeAdder.GetType(), &DummyHandler::Process_UserLeave));

	//스레드 생성, 시작
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
	@brief		더미 추가, connect 및 login 요청
	@return		false - 생성하려는 dummy가 MAX_DUMMY_COUNT를 벗어나는 경우
				true - 함수 종료시 반환, 더미의 처리가 실패한 경우에는 콘솔에 내용 출력
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

		//recv 등록
		::ZeroMemory(&recvOverlap, sizeof(recvOverlap));
		recvOverlap.WsaBuf.len = sizeof(recvOverlap.Iocp_Buffer);
		recvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(recvOverlap.Iocp_Buffer);
		recvOverlap.Operation = Overlap_Exp::OPERATION_RECV;
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
	@brief		count만큼 더미 연결 종료, 종료되지 않은 가장 앞 순번부터 진행한다.
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

/**
	@brief		Send를 위해 사용가능한 OverlapExp의 인덱스를 얻는다.

	@return		[0, MAX_OVERLAPEXP_COUNT) -> 사용가능한 인덱스
				SERIAL_ERROR -> try_pop이 지정한 timeout동안 성공하지 못한 경우
*/
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

/**
	@brief		전송(사용)완료된 OverlapExp를 반환한다.
*/
void DummyHandler::ReturnUsedOverlapExp(SERIAL_TYPE serial)
{
	if (MAX_OVERLAPEXP_COUNT <= serial) return;
	validOverlapExpSerials.push(serial);
}

////////////////////////////////////////////////////////////////////////////
//Packet Handling

/**
	@brief		모든 Send 요청에 대한 최종 단계 - WsaSend 호출
	@details	전송을 위한 Overlap 확장 구조체는 concurrent queue로부터 시리얼을 받아서 사용한다.
				
	@param serial 보내고자 하는 클라이언트 시리얼
	@param packet 보내고자 하는 패킷, Serialize 되어있다.

	@warning	concurrent queue의 try_pop이 지연되어 timeout 될 수 있다.
*/
void DummyHandler::SendPacket(SERIAL_TYPE serial, const void* packet)
{
	if (IsValidSerial(serial) == false
		|| packet == nullptr)
		return;

	SERIAL_TYPE overlapSerial = GetSerialForUseOverlapExp();
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
	@brief		패킷 타입을 확인하여 그에 맞는 등록된 프로시저 실행
	@warning	처리해야할 프로시저만 등록되어있으며, type이 정의되지 않은 값이더라도
				그에 대한 처리(ex. 오류 출력)를 진행하지 않음

	@throw StreamReadUnderflow - 프로시저 내 패킷 Deserialize에서 발생할 수 있음
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
	@brief		id 생성 성공, 타이머에 랜덤 패킷 발송을 등록한다.
	@details	더미를 수정하므로, dummyLock을 진행한다.

	@throw StreamReadUnderflow - Packet_Login::Deserialize()에서 발생할 수 있음
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

/**
	@brief		채널에 연결되었음을 확인, 처리한다.
	@details	더미를 수정하므로, dummyLock을 진행한다.

	@throw StreamReadUnderflow - Packet_Channel_Enter::Deserialize()에서 발생할 수 있음
*/
void DummyHandler::Process_ChannelEnter(SERIAL_TYPE serial, StreamReader& in)
{
	if (IsValidSerial(serial) == false)	return;

	Packet_Channel_Enter enterPacket;
	enterPacket.Deserialize(in);

	std::unique_lock<std::mutex> ulDummy(dummies[serial].first.GetLock());
	dummies[serial].first.userChannel = enterPacket.channelName;
}

/**
	@brief		더미 자신이 강퇴당했을 때에 처리를 진행한다.
	@details	서버에서 강퇴에 의해 다른 채널로 연결되기 전 까지는 내 채널을 void로 설정한다.

	@throw StreamReadUnderflow - Packet_User_Leave::Deserialize()에서 발생할 수 있음
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
		dummies[serial].first.userChannel.clear(); //void 채널
	}
}

////////////////////////////////////////////////////////////////////////////
//Request

/**
	@brief		함수 내 설정한 확률에 따라 서버에 임의의 패킷을 전송한 뒤 다시 타이머에 등록한다.
	@details	std::default_random_engine 및 std::uniform_real_distribution이 사용된다.
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
	@brief		타이머 큐에 해당 더미가 일정 시간 뒤 임의의 패킷을 발송하도록 등록한다.
	@details	timer thread와의 동기화를 위해 timerLock을 진행한다.
*/
void DummyHandler::AddRandomPacketEvent(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	Event_Info eventInfo;
	eventInfo.Serial = static_cast<int>(serial);
	eventInfo.Event_Type = Overlap_Exp::OPERATION_RANDPACKET;
	
	std::unique_lock<std::mutex> ulTimer(timerLock);
	eventInfo.Wakeup_Time = ::GetTickCount() + PACKET_DELAY_TIME;
	timerQueue.push(eventInfo);
}

/**
	@brief		귓속말 요청을 전송한다. 대상은 접속중인 임의의 다른 더미이다.
	@details	임의 대상을 못찾거나 본인인 경우에는 발송하지 않는다.

	@see		DummyHandler::GetRandomUser() const;
*/
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

/**
	@brief		서버 내의 채널리스트 및 커스텀채널 개수 정보를 요청한다
	
	@warning	client는 사용하지 않는 변수도 초기화를 누락하지 않도록 한다.
*/
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

/**
	@brief		채널 변경을 요청한다. 임의의 채널 이름을 받아 요청한다.
	@details	채널 이름을 얻지 못했거나 본인 채널과 동일하면 요청하지 않는다.

	@see		DummyHandler::GetRandomChannel() const;
*/
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
	@brief		강퇴요청 패킷을 전송한다. 대상이 채널이 다르거나 본인이 방장이 아니라도 진행된다.
	@details	임의 유저를 얻지 못했거나 본인이면 진행하지 않는다.

	@see		DummyHandler::GetRandomUser() const;
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

/**
	@brief		채널 채팅요청을 전송한다. void채널이면 전송하지 않는다.
*/
void DummyHandler::RequestChatting(SERIAL_TYPE serial)
{
	if (IsValidSerial(serial) == false
		|| dummies[serial].first.isLogin == false) return;

	if (dummies[serial].first.userChannel.empty()) return;	//void 채널 상태

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
	@brief		connect 되어있는 임의의 더미 중 하나의 이름을 얻는다.

	@return		더미가 로그인되어있다면 그 더미의 userName
				connect이나 login되지 않았다면 empty string 반환

	@warning	'임의' 이므로, 요청한 본인의 이름을 받을 수도 있다.
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
	@brief		connect 되어있는 임의의 더미 중 하나가 연결된 채널 이름 or 공개채널 or 임의의 채널 이름을 얻는다.
	
	@return		위 세 경우 중 하나의 채널 이름값을 얻는다.
				임의의 더미가 login 상태가 아니라면 empty string을 반환

	@warning	'임의'이므로 반환의 결과가 본인 채널과 동일할 수 있다.
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
	/**
		@brief		워커스레드 동작함수, 패킷 조립 및 처리함수 호출, 자원 반환 진행
		@details	iocp로 동작한다. overlap 구조체를 확장하여 사용하며(Overlap_Exp), key는 클라이언트 시리얼이다.
					Overlap_Exp의 OPERATION 열거형으로 이벤트를 구분하여 처리한다.
					recv는 패킷을 조립하고, 해당 프로시저를 호출한 뒤, 다시 recv를 등록한다.
					send는 사용했던 Overlap_Exp를 반환하는 작업을 진행한다.

					랜덤패킷의 경우, send가 완료되어 반환된 이후에 다시 랜덤패킷 요청을 진행하므로
					Overlap 컨커런트 큐가 비는 상황은 발생하지 않는다.

					전체 종료 요청처리를 위해 GQCS_TIMEOUT_MILLISECONDS 을 둔다.

		@todo		패킷 조립코드는 더 좋고 간결한 방법을 찾아낼때마다 적용하고 테스트해야 한다.
		@author		cgHan
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
				&iosize, &serial, reinterpret_cast<LPOVERLAPPED*>(&overlapExp), DummyHandler::GQCS_TIMEOUT_MILLISECONDS);

			if (result == false)
			{
				if (overlapExp == nullptr)
					continue; //timeout
				else
				{
					std::cout << "WorkerThreadStart() - GetQueuedCompletionStatus error\n";
					return;
				}
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

			if (Overlap_Exp::OPERATION_RECV == overlapExp->Operation)
			{
				unsigned char* ioPtr = overlapExp->Iocp_Buffer;
				int remained = 0;

				if ((overlapInfo.SavedPacketSize + iosize) > Packet_Base::MAX_BUF_SIZE)
				{
					int empty = Packet_Base::MAX_BUF_SIZE - overlapInfo.SavedPacketSize;
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.SavedPacketSize, ioPtr, empty);

					remained = iosize - empty;
					ioPtr += empty;
					overlapInfo.SavedPacketSize += empty;
				}
				else
				{
					std::memcpy(overlapInfo.PacketBuff + overlapInfo.SavedPacketSize, ioPtr, iosize);
					overlapInfo.SavedPacketSize += iosize;
				}

				do
				{
					overlapInfo.PacketSize = GetPacketSize(overlapInfo.PacketBuff);

					if (overlapInfo.PacketSize <= overlapInfo.SavedPacketSize)
					{	//조립가능
						handler->ProcessPacket(serial, overlapInfo.PacketBuff, overlapInfo.PacketSize);
						std::memmove(overlapInfo.PacketBuff, overlapInfo.PacketBuff + overlapInfo.PacketSize
							, overlapInfo.SavedPacketSize - overlapInfo.PacketSize);

						overlapInfo.SavedPacketSize -= overlapInfo.PacketSize;
						overlapInfo.PacketSize = 0;

						if (remained > 0
							&& (overlapInfo.SavedPacketSize + remained) <= sizeof(overlapInfo.PacketBuff))
						{
							std::memcpy(overlapInfo.PacketBuff + overlapInfo.SavedPacketSize, ioPtr, remained);
							overlapInfo.SavedPacketSize += remained;
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
			else if (Overlap_Exp::OPERATION_SEND == overlapExp->Operation)
			{	
				handler->ReturnUsedOverlapExp(overlapExp->Serial);
			}
			else if (Overlap_Exp::OPERATION_RANDPACKET == overlapExp->Operation)
			{
				handler->ReturnUsedOverlapExp(overlapExp->Serial);
				//랜덤패킷 발송, 이 함수내에서 다시 타이머 등록한다.
				handler->SendRandomPacket(serial);
			}
			else
			{
				std::cout << "Unknown IOCP event!\n";
			}
		}
	}

	/**
		@brief		timer_queue를 사용하여 랜덤패킷 발송을 요청한다.
		@details	DummyHandler와 동기화를 위해 TimerLock을 사용한다.
					priority_queue로, Wakeup_Time이 가장 이른 이벤트가 먼저 호출된다.
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
				overlapExp.Operation = Overlap_Exp::OPERATION_RANDPACKET;
				PostQueuedCompletionStatus(handler->GetIocpHandle(), 1,
					ev.Serial, &overlapExp.Original_Overlap);
				
				ul.lock();
			}
		}
	}

} //unnamed namespace
