#include "Socket.h"
#include "Framework.h"
#include "../Common/stream.h"
#include <sstream>
#include <vector>
#include <cctype>
#include <locale>

//editInput�� Enter �Է� ó���� ���� ����Ŭ���̿� ���ν���
LRESULT CALLBACK EditSubProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_RETURN:
			Framework::GetInstance()->ProcessInput();
			return 0;
		}
		break;
	
	case WM_LBUTTONDOWN:
		::SetWindowText(Framework::GetInstance()->GetEditInput(), "");
		break;
	}

	return ::CallWindowProc(Framework::GetInstance()->GetOldInputProc(), hWnd, msg, wParam, lParam);
}

//�Է��� Command���� Ȯ���ϱ� ���� ���ڿ� �Ľ�
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

Framework::~Framework()
{
	WSACleanup();
}


bool Framework::Run(HWND hWnd, HINSTANCE instance)
{
	if (hWnd == NULL || instance == NULL)
		return false;

	if (isRunning == true)
	{
		::MessageBox(hWnd, "�̹� �ʱ�ȭ�Ǿ����ϴ�.", "Error", MB_OK);
		return false;
	}

	//��Ŷ ó���Լ�
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

	editInput = ::CreateWindow("edit", "�α��� �� �����Դϴ�. ����ϰ��� �ϴ� ���̵� �Է� �� Enter�� �����ּ���. ����, Ư������ �Ұ�"
		, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT
		, rect.left + BLANK, rect.bottom - BLANK - CHAT_HEIGHT, CHAT_WIDTH, CHAT_HEIGHT
		, mainWindow, 0, mainInstance, NULL);

	oldInputProc = (WNDPROC)SetWindowLong(editInput, GWL_WNDPROC, (LONG)EditSubProc);

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
		, "�ӼӸ�     : /w ID �Ҹ� \n"
			"ä�θ�� : /channels\n"
			"ä�κ��� : /channel CHANNELNAME\n"
			"����         : /kick ID", WS_CHILD | WS_VISIBLE,
		rect.right - BLANK - CHANNEL_WIDTH, BLANK, CHANNEL_WIDTH, COMMAND_HEIGHT
		, hWnd, 0, mainInstance, NULL);

	isRunning = true;
	return isRunning;
}

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

void Framework::ProcessInput()
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

	static const std::string sysMsg("***System*** �߸��� ��ɾ� ����Դϴ�. �ٽ� Ȯ�����ּ���.");

	auto tokens = SplitString(str, ' ');
	switch (GetCommandType(tokens))
	{
	case CommandType::WHISPER:
	{
		//4 -> '/w ID ' ���� ����� /w�� ����
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
	else if (command.front() == '/')	//��ɾ���� '/'�̳� �ش���� ����
	{
		resultType = CommandType::MISUSE; 
	}
	else
	{
		resultType = CommandType::CHATTING;
	}

	return resultType;
}

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

void Framework::SeekLastAddedCursor(HWND listBox)
{
	int count = ::SendMessage(listBox, LB_GETCOUNT, 0, 0);
	::SendMessage(listBox, LB_SETCURSEL, WPARAM(count - 1), 0);
}


////////////////////////////////////////////////////////////////////////////
//Receive From Server

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


void Framework::Process_SystemMessage(StreamReader& in)
{
	Packet_System packet;
	packet.Deserialize(in);

	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(packet.systemMessage.c_str()));
	SeekLastAddedCursor(listLog);
}

void Framework::Process_Login(StreamReader& in)
{
	Packet_Login packet;
	packet.Deserialize(in);
	
	if (packet.isCreated == false)
	{
		MessageBox(mainWindow, "�ߺ��Ǿ� ��� �Ұ����� ���̵��Դϴ�. �ٽ� �Է��� �ּ���", "���� �Ұ�", MB_OK);
		return;
	}

	userName = packet.userName;
	isLogin = true;
}

void Framework::Process_ChannelList(StreamReader& in)
{
	Packet_Channel_List packet;
	packet.Deserialize(in);

	size_t publicChannelCount = packet.publicChannelNames.size();

	std::string sysMsg("***System*** ");
	sysMsg += std::to_string(publicChannelCount);
	sysMsg += " ���� ���� ä�ΰ� ";
	sysMsg += std::to_string(packet.customChannelCount);
	sysMsg += " ���� Ŀ���� ä���� �����մϴ�.";
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
	sysMsg += " �� ���Խ��ϴ�.";
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
	SeekLastAddedCursor(listLog);
}

void Framework::Process_ChannelUsers(StreamReader& in)
{
	Packet_Channel_Users packet;
	packet.Deserialize(in);

	size_t userCount = packet.userNames.size();
	if (userCount == 0)
		return;

	for (size_t i = 0; i < userCount; ++i)
	{
		std::string name = packet.userNames[i];
		if (packet.userNames[i] == currentChannelMaster)
			name += "��";
		if (packet.userNames[i] == userName)
			name += " (You)";

		::SendMessage(listUsers, LB_ADDSTRING, 0, (LPARAM)name.c_str());
	}
	SeekLastAddedCursor(listUsers);
}

void Framework::Process_NewfaceEnter(StreamReader& in)
{
	Packet_Newface_Enter packet;
	packet.Deserialize(in);

	if(packet.userName != userName)
		::SendMessage(listUsers, LB_ADDSTRING, 0, LPARAM(packet.userName.c_str()));
}

void Framework::Process_UserLeave(StreamReader& in)
{
	Packet_User_Leave packet;
	packet.Deserialize(in);

	std::string sysMsg("***System*** ");
	if (packet.isKicked)
	{
		if (packet.userName == userName)
		{
			sysMsg += " ���忡 ���� ������߽��ϴ�.";
			userChannel.clear(); //void ä��
			::SendMessage(listUsers, LB_RESETCONTENT, 0, 0);
		}
		else
			sysMsg += packet.userName + std::string(" ���� ������߽��ϴ�.");
		
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}

	int slot = ::SendMessage(listUsers, LB_FINDSTRINGEXACT, 0, LPARAM(packet.userName.c_str()));
	if(slot == LB_ERR)
		slot = ::SendMessage(listUsers, LB_FINDSTRING, 0, LPARAM(packet.userName.c_str()));

	if (slot != LB_ERR)
		::SendMessage(listUsers, LB_DELETESTRING, WPARAM(slot), 0);
	else
	{
		sysMsg += "ä�� ����, ä�ο� �ٽ� �������ֽñ� �ٶ��ϴ�.";
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}
}

void Framework::Process_Chatting(StreamReader& in)
{
	Packet_Chatting packet;
	packet.Deserialize(in);

	std::string chatMsg;
	if (packet.isWhisper)
	{
		if (packet.talker == userName)
			chatMsg += (std::string(packet.listener) + " �Կ��� : ");
		else
			chatMsg += (std::string(packet.talker) + " ���� �ӼӸ� : ");
	}
	else
		chatMsg += std::string(packet.talker) + " : ";

	chatMsg += packet.chat;
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(chatMsg.c_str()));
	SeekLastAddedCursor(listLog);
}

void Framework::Process_NewMaster(StreamReader& in)
{
	Packet_New_Master packet;
	packet.Deserialize(in);

	if (packet.channelName != userChannel)
		return;

	currentChannelMaster = packet.master;
	int slot = ::SendMessage(listUsers, LB_FINDSTRING, 0, LPARAM(packet.master.c_str()));

	if (slot != -1)
	{
		::SendMessage(listUsers, LB_DELETESTRING, WPARAM(slot), 0);
		std::string newMasterName(currentChannelMaster + "��");
		if (currentChannelMaster == userName)
			newMasterName += " (You)";

		::SendMessage(listUsers, LB_ADDSTRING, 0, LPARAM(newMasterName.c_str()));

		std::string sysMsg("***System*** ������ ");
		sysMsg += currentChannelMaster + " ������ ����Ǿ����ϴ�.";
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}
}


////////////////////////////////////////////////////////////////////////////
//Send To Server

void Framework::RequestLogin(const std::string& id)
{
	if (isLogin == true)
	{	//error
		::MessageBox(mainWindow, "RequestLogin() - isLogin true", "Error!", MB_OK);
		return;
	}

	if (id.size() > Packet_Base::MAX_USERNAME_SIZE)
	{
		::MessageBox(mainWindow, "�ʹ� �� �̸��Դϴ�.", "���� �Ұ�", MB_OK);
		return;
	}

	if (false == IsValidUserName(id))
	{
		::MessageBox(mainWindow, "��� �Ұ����� ���̵��Դϴ�. �ѱ�, ���ĺ�, ���ڸ� ��� �����մϴ�.", "���� �Ұ�", MB_OK);
		return;
	}

	Packet_Login loginPacket;
	loginPacket.isCreated = false;
	loginPacket.userName = id;

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	loginPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

void Framework::RequestWhisper(const std::string& listener, const std::string& chat)
{
	if (listener == userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ���ο��� �ӼӸ� �� �� �����ϴ�!"));
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

void Framework::RequestChannelList()
{
	//client to server - ����Ʈ�� ��û�ϹǷ� ���� �����͸� ���� ����
	Packet_Channel_List channelListPacket;
	channelListPacket.customChannelCount = 0;
	channelListPacket.publicChannelNames.clear();

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	channelListPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

void Framework::RequestChannelChange(const std::string& channelName)
{
	if (channelName == userChannel)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** �̹� ��� ä���Դϴ�."));
		SeekLastAddedCursor(listLog);
		return;
	}

	//client to server - �������ϴ� ä���̸��� �ʱ�ȭ
	Packet_Channel_Enter enterPacket;
	enterPacket.channelName = channelName;
	enterPacket.channelMaster.clear();

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	enterPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}

void Framework::RequestKick(const std::string& target)
{
	if (currentChannelMaster != userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ����� ������ �ƴմϴ�!"));
		SeekLastAddedCursor(listLog);
		return;
	}
	if (target == userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ������ ������ �� �����ϴ�!"));
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

void Framework::RequestChatting(const std::string& chat)
{
	if (userChannel.empty())
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ä�ο� ������� �ʾҽ��ϴ�!"));
		SeekLastAddedCursor(listLog);
		return;
	}

	Packet_Chatting chatPacket;
	chatPacket.isWhisper = false;
	chatPacket.listener.clear();	//��üä���̹Ƿ�
	chatPacket.talker = userName;
	chatPacket.chat = chat;

	StreamWriter stream(userSocket.GetSendWsaBuf().buf, Packet_Base::MAX_BUF_SIZE);
	chatPacket.Serialize(stream);

	userSocket.SendPacket(stream.GetBuffer());
}