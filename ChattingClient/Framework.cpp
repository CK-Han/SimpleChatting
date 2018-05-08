#include "Socket.h"
#include "Framework.h"
#include "../Common/stream.h"
#include <sstream>
#include <vector>
#include <cctype>
#include <locale>

/**
	@brief		editInput의 Enter 입력 처리를 위한 서브클래싱용 프로시저
	@details	Enter 입력만을 처리, Focus 활성화(클릭)시 내용 비우기
*/
LRESULT CALLBACK EditSubProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_RETURN:
			Framework::GetInstance()->ProcessUserInput();
			return 0;
		}
		break;
	
	case WM_LBUTTONDOWN:
		::SetWindowText(Framework::GetInstance()->GetEditInput(), "");
		break;
	}

	return ::CallWindowProc(Framework::GetInstance()->GetOldInputProc(), hWnd, msg, wParam, lParam);
}

/**
	@brief		사용자 입력이 Command인지 확인하기 위한 문자열 파싱
	@details	문자열로부터 구분자를 통해 토큰으로 나누어, 그 내용을 std::vector에 담아 반환
				std::stringstream을 활용하여 파싱한다.
	@param input		사용자로부터 입력된 문자열
	@param delimeter	문자열을 파싱하기 위한 구분자
	@return	std::vector<std::string>
			in-out 인자보다는 이동을 가진 컨테이너를 반환하도록 한다.
	
	@todo	제공되는 표준 파싱함수를 활용하는 방향을 고려해보자.
			더불어, string, char의 문자 집합에 와이드 문자에 대해서도 처리할 수 있어야 한다.
			그것이 필요해진다면, string::value_type 을 통해 일반화시킨 템플릿 함수여야 할 것이다.
*/
std::vector<std::string>
	SplitString(const std::string& input, char delimeter)
{
	std::vector<std::string> tokens;

	std::istringstream iss(input);
	std::string token;
	while (std::getline(iss, token, delimeter))
	{
		tokens.push_back(token);
	}

	return tokens;
};

/**
	@brief		멤버변수 초기화, WSAStartup
*/
Framework::Framework()
	: clientWidth(0)
	, clientHeight(0)
	, isRunning(false)
	, isLogin(false)
	, mainWindow(NULL)
	, mainInstance(NULL)
	, editInput(NULL)
	, listLog(NULL)
	, editChannelName(NULL)
	, listUsers(NULL)
	, textCommands(NULL)
	, oldInputProc(nullptr)
{
	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
}

/**
	@brief		WSACleanup을 진행한다.
*/
Framework::~Framework()
{
	WSACleanup();
}

/**
	@brief		소켓 connect 이후 호출되는 프레임워크 초기화
	@details	윈도우 핸들, 인스턴스 초기화 및 패킷 처리 프로시저 맵 초기화
				클라이언트에 그려질 리스트 박스 및 버튼, edit, static 박스 초기화

	@return		성공적 초기화시에만 true 
				윈도우 핸들이 유효하다면, 이후 초기화 실패시에는 false 반환과 함께 messagebox 출력
*/
bool Framework::Run(HWND hWnd, HINSTANCE instance)
{
	if (hWnd == NULL || instance == NULL)
		return false;

	if (isRunning == true)
	{
		::MessageBox(hWnd, "이미 초기화되었습니다.", "Error", MB_OK);
		return false;
	}

	//패킷 처리함수
	procedures.insert(std::make_pair(Packet_System::typeAdder.GetType(), &Framework::Process_SystemMessage));
	procedures.insert(std::make_pair(Packet_Login::typeAdder.GetType(), &Framework::Process_Login));
	procedures.insert(std::make_pair(Packet_Channel_List::typeAdder.GetType(), &Framework::Process_ChannelList));
	procedures.insert(std::make_pair(Packet_Channel_Enter::typeAdder.GetType(), &Framework::Process_ChannelEnter));
	procedures.insert(std::make_pair(Packet_Channel_Users::typeAdder.GetType(), &Framework::Process_ChannelUsers));
	procedures.insert(std::make_pair(Packet_Newface_Enter::typeAdder.GetType(), &Framework::Process_NewfaceEnter));
	procedures.insert(std::make_pair(Packet_User_Leave::typeAdder.GetType(), &Framework::Process_UserLeave));
	procedures.insert(std::make_pair(Packet_Chatting::typeAdder.GetType(), &Framework::Process_Chatting));
	procedures.insert(std::make_pair(Packet_New_Master::typeAdder.GetType(), &Framework::Process_NewMaster));

	mainWindow = hWnd;
	mainInstance = instance;
	
	RECT rect{ 0 };
	::GetClientRect(mainWindow, &rect);
	clientWidth = rect.right - rect.left;
	clientHeight = rect.bottom - rect.top;

	static const unsigned int	CHAT_WIDTH = static_cast<unsigned int>(clientWidth * CHAT_WIDTH_RATIO);
	static const unsigned int	LOG_HEIGHT = static_cast<unsigned int>(clientHeight * LOG_HEIGHT_RATIO);

	static const unsigned int	CHANNEL_WIDTH = static_cast<unsigned int>(clientWidth * CHANNEL_WIDTH_RATIO);
	static const unsigned int	USERS_HEIGHT = static_cast<unsigned int>(clientHeight * USERS_HEIGHT_RATIO);
	static const unsigned int	COMMAND_HEIGHT = static_cast<unsigned int>(clientHeight * COMMAND_HEIGHT_RATIO);

	editInput = ::CreateWindow("edit", "로그인 전 상태입니다. 사용하고자 하는 아이디 입력 후 Enter를 눌러주세요. 공백, 특수문자 불가"
		, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT
		, rect.left + BLANK, rect.bottom - BLANK - CHAT_HEIGHT, CHAT_WIDTH, CHAT_HEIGHT
		, mainWindow, 0, mainInstance, NULL);

	oldInputProc = (WNDPROC)SetWindowLongPtr(editInput, GWLP_WNDPROC, (LONG_PTR)EditSubProc);

	listLog = ::CreateWindow("listbox", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOVSCROLL | WS_VSCROLL | LBS_DISABLENOSCROLL
		, rect.left + BLANK, rect.top + BLANK, CHAT_WIDTH, LOG_HEIGHT
		, mainWindow, 0, mainInstance, NULL);

	editChannelName = ::CreateWindow("edit", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_READONLY
		, rect.right - BLANK - CHANNEL_WIDTH, rect.bottom - BLANK - USERS_HEIGHT - BLANK - CHAT_HEIGHT, CHANNEL_WIDTH, CHAT_HEIGHT
		, mainWindow, 0, mainInstance, NULL);
	
	listUsers = ::CreateWindow("listbox", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOVSCROLL | WS_VSCROLL | LBS_DISABLENOSCROLL
		, rect.right - BLANK - CHANNEL_WIDTH, rect.bottom - BLANK - USERS_HEIGHT, CHANNEL_WIDTH, USERS_HEIGHT
		, mainWindow, 0, mainInstance, NULL);

	textCommands = ::CreateWindow("static"
		, "귓속말     : /w ID 할말 \n"
			"채널목록 : /channels\n"
			"채널변경 : /channel CHANNELNAME\n"
			"강퇴         : /kick ID", WS_CHILD | WS_VISIBLE,
		rect.right - BLANK - CHANNEL_WIDTH, BLANK, CHANNEL_WIDTH, COMMAND_HEIGHT
		, hWnd, 0, mainInstance, NULL);

	isRunning = true;
	return isRunning;
}

/**
	@brief		SELECT ERROR 및 FD_CLOSE와 FD_READ에 대해서 처리한다.
*/
void Framework::ProcessWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case Socket::WM_SOCKET:
	{
		if (WSAGETSELECTERROR(lParam)) 
		{
			closesocket((SOCKET)wParam);
			exit(-1);
			break;
		}
		switch (WSAGETSELECTEVENT(lParam))
		{
		case FD_READ:
			userSocket.ReadPacket((SOCKET)wParam);
			break;
		case FD_CLOSE:
			closesocket((SOCKET)wParam);
			exit(-1);
			break;
		}
	}
	break;

	default:
		break;
	}
}

/**
	@brief		사용자 입력(Enter) 처리
	@details	Enter 입력시 edit을 비우고, 입력을 파싱, 커맨드 요청인지 채팅인지 확인하여 처리한다.
				성공적으로 login되기 전 까지는, 모든 입력은 login에 대한 요청으로 처리된다.
*/
void Framework::ProcessUserInput()
{
	char input[Packet_Base::MAX_MESSAGE_SIZE];
	::GetWindowText(editInput, input, Packet_Base::MAX_MESSAGE_SIZE);
	::SetWindowText(editInput, "");

	std::string str(input);
	str = str.substr(0, Packet_Base::MAX_MESSAGE_SIZE);
	if (str.empty())
		return;	
	else if (isLogin == false)
	{
		RequestLogin(str);
		return;
	}

	static const std::string sysMsg("***System*** 잘못된 명령어 사용입니다. 다시 확인해주세요.");

	auto tokens = SplitString(str, ' ');
	switch (GetCommandType(tokens))
	{
	case CommandType::WHISPER:
	{
		//4 -> "/w ID " 에서 공백과 /w의 길이
		RequestWhisper(tokens[1], str.substr(4 + tokens[1].size()));
		break;
	}
	
	case CommandType::CHANNELLIST:
		RequestChannelList();
		break;

	case CommandType::CHANNELCONNECT:
		RequestChannelChange(tokens[1]);
		break;

	case CommandType::KICKUSER:
		RequestKick(tokens[1]);
		break;

	case CommandType::CHATTING:
		RequestChatting(str);
		break;

	case CommandType::MISUSE:
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
		break;

	default: //error
		::MessageBox(mainWindow, "ProcessInput() - GetCommandType() - Unknown Type", "Error", MB_OK);
		break;
	} //end switch
}

////////////////////////////////////////////////////////////////////////////
//Private Functions

/**
	@brief		파싱된 사용자 입력으로부터 내용이 커맨드 요청인지 채팅인지 확인하여 타입 반환
*/
Framework::CommandType 
	Framework::GetCommandType(const std::vector<std::string>& tokens) const
{
	if (tokens.empty())
		return CommandType::MISUSE;;

	CommandType resultType = CommandType::MISUSE;

	std::string command = tokens[0];
	size_t tokenCount = tokens.size();
	if (command == "/w")
	{
		if (tokenCount >= 3)
			resultType = CommandType::WHISPER;
	}
	else if (command == "/channels")
	{
		if (tokenCount == 1)
			resultType = CommandType::CHANNELLIST;
	}
	else if (command == "/channel")
	{
		if (tokenCount == 2)
			resultType = CommandType::CHANNELCONNECT;
	}
	else if (command == "/kick")
	{
		if (tokenCount == 2)
			resultType = CommandType::KICKUSER;
	}
	else if (command.front() == '/')	///명령어의 시작 키워드인 '/'이나 해당사항 없음
	{
		resultType = CommandType::MISUSE; 
	}
	else
	{
		resultType = CommandType::CHATTING;
	}

	return resultType;
}

/**
	@brief		사용할 Id가 유효한지 확인한다.
	@details	std::isalnum으로 한 문자씩 확인하며, 조합되는 문자인 경우
				2 바이트를 읽어 wide 문자로 변경, 한국어 표기에 맞는 값인지 확인한다.
	
	@todo		보다 적절한 표준 함수를 찾아서 적용하도록 한다.
*/
bool Framework::IsValidUserName(const std::string& id) const
{
	static const unsigned short KOREAN_CODE_BEGIN = 44032;
	static const unsigned short KOREAN_CODE_END = 55199;
	static const std::locale	KOREAN_LOCALE("Korean");
	
	for (auto iter = id.cbegin(); iter != id.cend(); ++iter)
	{
		char ch = *iter;

		if (false == std::isalnum(ch, KOREAN_LOCALE))
		{
			if ((++iter) != id.cend())
			{
				std::string strKor(1, ch);
				strKor += *iter;

				wchar_t kor;
				int resConvert = ::MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, strKor.c_str(), 2, &kor, 1);
				if (resConvert != 1
					|| kor < KOREAN_CODE_BEGIN
					|| KOREAN_CODE_END < kor)
					return false;
			}
			else
				return false;
		}
	}
	
	return true;
}

/**
	@brief		리스트 박스에 새로운 내용이 insert될 시, 커서를 그 내용에 맞추어 최신을 유지한다.
*/
void Framework::SeekLastAddedCursor(HWND listBox)
{
	LRESULT count = ::SendMessage(listBox, LB_GETCOUNT, 0, 0);
	::SendMessage(listBox, LB_SETCURSEL, WPARAM(count - 1), 0);
}


////////////////////////////////////////////////////////////////////////////
//Receive From Server

/**
	@brief		조립된 패킷으로부터 프로시저를 호출
	@param size 패킷 크기, GetPacketSize() 할 수 있으나 패킷 조립단계 확인했으므로 값을 넘겨주도록 함

	@throw StreamReadUnderflow - 프로시저 내 패킷 Deserialize에서 발생할 수 있음
*/
void Framework::ProcessPacket(const void* packet, int size)
{
	if (packet == nullptr || size > Packet_Base::MAX_BUF_SIZE)
		return;

	auto type = GetPacketType(packet);
	StreamReader stream(packet, size);

	auto procedure = procedures.find(type);
	if (procedure == procedures.end())
		::MessageBox(mainWindow, "ProcessPacket() Unknown packet Type", "Error!", MB_OK);
	else
		(this->*procedures[type])(stream);
}


/**
	@brief		서버로부터 받은 시스템메세지를 채팅창에 출력한다.

	@throw		StreamReadUnderflow - Packet_System::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_SystemMessage(StreamReader& in)
{
	Packet_System packet;
	packet.Deserialize(in);

	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(packet.systemMessage.c_str()));
	SeekLastAddedCursor(listLog);
}

/**
	@brief		로그인 요청에 대한 서버의 응답을 처리한다.

	@throw		StreamReadUnderflow - Packet_Login::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_Login(StreamReader& in)
{
	Packet_Login packet;
	packet.Deserialize(in);
	
	if (packet.isCreated == false)
	{
		MessageBox(mainWindow, "중복되어 사용 불가능한 아이디입니다. 다시 입력해 주세요", "생성 불가", MB_OK);
		return;
	}

	userName = packet.userName;
	isLogin = true;
}


/**
	@brief		채널 리스트 요청에 대한 응답을 처리한다.
	@details	공개채널 리스트와, 커스텀채널의 개수가 들어있으며 이를 채팅창에 출력한다.

	@throw		StreamReadUnderflow - Packet_Channel_List::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_ChannelList(StreamReader& in)
{
	Packet_Channel_List packet;
	packet.Deserialize(in);

	size_t publicChannelCount = packet.publicChannelNames.size();

	std::string sysMsg("***System*** ");
	sysMsg += std::to_string(publicChannelCount);
	sysMsg += " 개의 공개 채널과 ";
	sysMsg += std::to_string(packet.customChannelCount);
	sysMsg += " 개의 커스텀 채널이 존재합니다.";
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));

	for (size_t i = 0; i < publicChannelCount; ++i)
	{
		std::string strNames;
		strNames += std::to_string(i + 1);
		strNames += ". " + packet.publicChannelNames[i];
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(strNames.c_str()));
	}

	SeekLastAddedCursor(listLog);
}

/**
	@brief		유저의 요청, 혹은 서버에 의해 이동되었을때 그 채널에 대한 정보를 처리한다.
	@details	서버에 의한 처리는 다른 사람의 강퇴 혹은 첫 로그인이다.
				채널 유저 리스트 박스를 비우고 채널 이름 에디트 창을 변경한다.

	@throw		StreamReadUnderflow - Packet_Channel_Enter::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_ChannelEnter(StreamReader& in)
{ 
	Packet_Channel_Enter packet;
	packet.Deserialize(in);

	currentChannelMaster = packet.channelMaster;
	userChannel = packet.channelName;

	::SetWindowText(editChannelName, userChannel.c_str());
	::SendMessage(listUsers, LB_RESETCONTENT, 0, 0);
	
	std::string sysMsg("***System*** ");
	sysMsg += packet.channelName;
	sysMsg += " 에 들어왔습니다.";
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
	SeekLastAddedCursor(listLog);
}

/**
	@brief		채널에 존재하는 유저들의 리스트를 얻고 이를 처리한다.
	@details	커스텀채널인 경우는 방장이 존재하고, 채널 리스트에는 본인이 포함된다.
				방장은 별표 표시, 본인은 (You)로 표시한다.

	@throw		StreamReadUnderflow - Packet_Channel_User::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_ChannelUsers(StreamReader& in)
{
	Packet_Channel_Users packet;
	packet.Deserialize(in);

	size_t userCount = packet.userNames.size();
	if (userCount == 0 
		|| packet.channelName != userChannel)
		return;

	for (size_t i = 0; i < userCount; ++i)
	{
		std::string name = packet.userNames[i];
		if (packet.userNames[i] == currentChannelMaster)
			name += "★";
		if (packet.userNames[i] == userName)
			name += " (You)";

		::SendMessage(listUsers, LB_ADDSTRING, 0, (LPARAM)name.c_str());
	}
	SeekLastAddedCursor(listUsers);
}

/**
	@brief		내 채널에 새로운 유저가 방문한 경우에 대한 처리이다.

	@throw		StreamReadUnderflow - Packet_Newface_Enter::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_NewfaceEnter(StreamReader& in)
{
	Packet_Newface_Enter packet;
	packet.Deserialize(in);

	if(packet.userName != userName)
		::SendMessage(listUsers, LB_ADDSTRING, 0, LPARAM(packet.userName.c_str()));
}

/**
	@brief		채널에 어떠한 유저가 나간 경우에 대한 처리이다. 자신도 포함된다.
	@details	다른 유저인 경우는 리스트박스에서 그 유저를 지우고
				본인인 경우 우선적으로 채널 이름을 clear하고, 서버가 공개채널로 연결해주길 기다린다.
				강퇴 여부에 따라 채팅창에 기록을 남긴다.

	@throw		StreamReadUnderflow - Packet_UserLeave::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_UserLeave(StreamReader& in)
{
	Packet_User_Leave packet;
	packet.Deserialize(in);
	
	std::string sysMsg("***System*** ");
	if (packet.isKicked)
	{
		if (packet.userName == userName)
		{
			sysMsg += " 방장에 의해 강퇴당했습니다.";
			userChannel.clear(); //void 채널
			::SetWindowText(editChannelName, "");
			::SendMessage(listUsers, LB_RESETCONTENT, 0, 0);
		}
		else
			sysMsg += packet.userName + std::string(" 님이 강퇴당했습니다.");
		
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}

	//list box에는 이름 뒤에 '★' 혹은 "(You)"가 붙어있을 수 있어 EXACT하게 찾지 못할 수 있다.
	LRESULT slot = ::SendMessage(listUsers, LB_FINDSTRINGEXACT, 0, LPARAM(packet.userName.c_str()));
	if(slot == LB_ERR)
		slot = ::SendMessage(listUsers, LB_FINDSTRING, 0, LPARAM(packet.userName.c_str()));

	if (slot != LB_ERR)
		::SendMessage(listUsers, LB_DELETESTRING, WPARAM(slot), 0);
	else
	{
		sysMsg = "***System*** 채널 오류, 채널에 다시 접속해주시기 바랍니다.";
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}
}

/**
	@brief		타인 혹은 본인의 채팅 내용에 대해 처리한다.
	@details	귓속말 여부를 확인하여 채팅창 출력을 달리한다.

	@throw		StreamReadUnderflow - Packet_Chatting::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_Chatting(StreamReader& in)
{
	Packet_Chatting packet;
	packet.Deserialize(in);

	std::string chatMsg;
	if (packet.isWhisper)
	{
		if (packet.talker == userName)
			chatMsg += (std::string(packet.listener) + " 님에게 : ");
		else
			chatMsg += (std::string(packet.talker) + " 님의 귓속말 : ");
	}
	else
		chatMsg += std::string(packet.talker) + " : ";

	chatMsg += packet.chat;
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(chatMsg.c_str()));
	SeekLastAddedCursor(listLog);
}

/**
	@brief		채널의 새로운 방장이 선정된 경우에 대한 처리이다.
	@details	유저 리스트 박스에서 방장 이름을 찾아, 별을 달아준다.
				채팅창에 방장이 변경되었음을 출력한다.

	@throw		StreamReadUnderflow - Packet_New_Master::Deserialize() 중 발생할 수 있다.
*/
void Framework::Process_NewMaster(StreamReader& in)
{
	Packet_New_Master packet;
	packet.Deserialize(in);

	if (packet.channelName != userChannel) return;

	currentChannelMaster = packet.master;
	LRESULT slot = ::SendMessage(listUsers, LB_FINDSTRINGEXACT, 0, LPARAM(packet.master.c_str()));

	if (slot != -1)
	{
		::SendMessage(listUsers, LB_DELETESTRING, WPARAM(slot), 0);
		std::string newMasterName(currentChannelMaster + "★");
		if (currentChannelMaster == userName)
			newMasterName += " (You)";

		::SendMessage(listUsers, LB_ADDSTRING, 0, LPARAM(newMasterName.c_str()));

		std::string sysMsg("***System*** 방장이 ");
		sysMsg += currentChannelMaster + " 님으로 변경되었습니다.";
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}
}


////////////////////////////////////////////////////////////////////////////
//Send To Server

/**
	@brief		사용하고자 하는 id의 유효성을 판단하고, 유효한 경우 서버에 요청한다.
				유효하지 않은 경우, 메시지박스로 확인할 수 있다.
*/
void Framework::RequestLogin(const std::string& id)
{
	if (isLogin == true)
	{	//error
		::MessageBox(mainWindow, "RequestLogin() - isLogin true", "Error!", MB_OK);
		return;
	}

	if (id.size() > Packet_Base::MAX_USERNAME_SIZE)
	{
		::MessageBox(mainWindow, "너무 긴 이름입니다.", "생성 불가", MB_OK);
		return;
	}

	if (false == IsValidUserName(id))
	{
		::MessageBox(mainWindow, "사용 불가능한 아이디입니다. 한글, 알파벳, 숫자만 사용 가능합니다.", "생성 불가", MB_OK);
		return;
	}

	Packet_Login loginPacket;
	loginPacket.isCreated = false;
	loginPacket.userName = id;

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	loginPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

/**
	@brief		상대방에게 귓속말을 하고자 서버에 요청한다. 본인한텐 귓말할 수 없다.
*/
void Framework::RequestWhisper(const std::string& listener, const std::string& chat)
{
	if (listener == userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** 본인에게 귓속말 할 수 없습니다!"));
		SeekLastAddedCursor(listLog);
		return;
	}

	Packet_Chatting chatPacket;
	chatPacket.isWhisper = true;
	chatPacket.talker = userName;
	chatPacket.listener = listener;
	chatPacket.chat = chat;

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	chatPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

/**
	@brief		서버에 존재하는 공개채널 리스트 및 커스텀채널 개수 정보를 요청한다.

	@warning	데이터를 담지 않는 경우라도, Packet 변수의 초기화를 누락하지 않도록 한다.
*/
void Framework::RequestChannelList()
{
	//client to server - 리스트를 요청하므로 따로 데이터를 담지 않음
	Packet_Channel_List channelListPacket;
	channelListPacket.customChannelCount = 0;
	channelListPacket.publicChannelNames.clear();

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	channelListPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

/**
	@brief		채널 변경을 요청한다. 내 채널이름과 같은 이름을 입력했다면 실패한다.
*/
void Framework::RequestChannelChange(const std::string& channelName)
{
	if (channelName == userChannel)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** 이미 계신 채널입니다."));
		SeekLastAddedCursor(listLog);
		return;
	}

	//client to server - 들어가고자하는 채널이름만 초기화
	Packet_Channel_Enter enterPacket;
	enterPacket.channelName = channelName;
	enterPacket.channelMaster.clear();

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	enterPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

/**
	@brief		target 강퇴를 요청한다. 강퇴 요청의 유효성을 판단 후 Send한다.
*/
void Framework::RequestKick(const std::string& target)
{
	if (currentChannelMaster != userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** 당신은 방장이 아닙니다!"));
		SeekLastAddedCursor(listLog);
		return;
	}
	if (target == userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** 본인을 강퇴할 수 없습니다!"));
		SeekLastAddedCursor(listLog);
		return;
	}

	Packet_Kick_User kickPacket;
	kickPacket.kicker = userName;
	kickPacket.target = target;
	kickPacket.channelName = userChannel;

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	kickPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

/**
	@brief		내 채널에 채팅내용 전달을 요청한다. 내 채널이 void(연결되지 않은 상태)인 경우 무시된다.
*/
void Framework::RequestChatting(const std::string& chat)
{
	if (userChannel.empty())
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** 채널에 연결되지 않았습니다!"));
		SeekLastAddedCursor(listLog);
		return;
	}

	Packet_Chatting chatPacket;
	chatPacket.isWhisper = false;
	chatPacket.listener.clear();	//전체채팅이므로
	chatPacket.talker = userName;
	chatPacket.chat = chat;

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	chatPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}