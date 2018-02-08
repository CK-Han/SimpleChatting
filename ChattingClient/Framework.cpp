#include "Socket.h"
#include "Framework.h"
#include <sstream>
#include <vector>
#include <cctype>


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
	, mainWindow(nullptr)
	, mainInstance(nullptr)
	, isInitialized(false)
	, isLogin(false)
{
	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
}

Framework::~Framework()
{
	WSACleanup();
}


void Framework::Initialize(HWND hWnd, HINSTANCE instance)
{
	if (isInitialized == true)
	{
		::MessageBox(hWnd, "�̹� �ʱ�ȭ�Ǿ����ϴ�.", "Error", MB_OK);
		return;
	}

	mainWindow = hWnd;
	mainInstance = instance;
	
	RECT rect;
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

	isInitialized = true;
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
	char input[MAX_CHATTING_SIZE];
	::GetWindowText(editInput, input, MAX_CHATTING_SIZE);
	::SetWindowText(editInput, "");

	std::string str(input);
	str = str.substr(0, MAX_CHATTING_SIZE);
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

	for (auto iter = id.cbegin(); iter != id.cend(); ++iter)
	{
		char ch = *iter;
		if (ch < 0)
		{
			if ((++iter) != id.cend())
			{
				std::string strKor(1, ch);
				strKor += *iter;

				wchar_t kor;
				int resConvert = ::MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, strKor.c_str(), 2, &kor, 1);
				if (resConvert != 1
					|| kor < KOREAN_CODE_BEGIN
					|| KOREAN_CODE_END < kor)
					return false;
			}
			else
				return false;
		}
		else if (false == std::isalnum(ch))
		{
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

void Framework::ProcessPacket(unsigned char* packet)
{
	switch (GetPacketType(packet))
	{
	case PACKET_SYSTEM:
		ProcessSystemMessage(packet);
		break;
	case PACKET_LOGIN:
		ProcessLogin(packet);
		break;
	case PACKET_CHANNEL_LIST:
		ProcessChannelList(packet);
		break;
	case PACKET_CHANNEL_ENTER:
		ProcessChannelEnter(packet);
		break;
	case PACKET_CHANNEL_USERS:
		ProcessChannelUsers(packet);
		break;
	case PACKET_NEWFACE_ENTER:
		ProcessNewfaceEnter(packet);
		break;
	case PACKET_USER_LEAVE:
		ProcessUserLeave(packet);
		break;
	case PACKET_CHATTING:
		ProcessChatting(packet);
		break;
	case PACKET_NEW_MASTER:
		ProcessNewMaster(packet);
		break;

	default:
		::MessageBox(mainWindow, "ProcessPacket() Unknown packet Type", "Error!", MB_OK);
		break;
	}
}


void Framework::ProcessSystemMessage(unsigned char* packet)
{
	packet_system* my_packet = reinterpret_cast<packet_system*>(packet);
	
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(my_packet->SystemMessage));
	SeekLastAddedCursor(listLog);
}

void Framework::ProcessLogin(unsigned char* packet)
{
	packet_login* my_packet = reinterpret_cast<packet_login*>(packet);
	if (my_packet->Created == false)
	{
		MessageBox(mainWindow, "�ߺ��Ǿ� ��� �Ұ����� ���̵��Դϴ�. �ٽ� �Է��� �ּ���", "���� �Ұ�", MB_OK);
		return;
	}

	userName = my_packet->User;
	isLogin = true;
}

void Framework::ProcessChannelList(unsigned char* packet)
{
	packet_channel_list* my_packet = reinterpret_cast<packet_channel_list*>(packet);
	
	std::string sysMsg("***System*** ");
	sysMsg += std::to_string((int)my_packet->PublicChannelCount);
	sysMsg += " ���� ���� ä�ΰ� ";
	sysMsg += std::to_string(my_packet->CustomChannelCount);
	sysMsg += " ���� Ŀ���� ä���� �����մϴ�.";
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));

	auto publicNames = SplitString(my_packet->PublicChannelNames, NAME_DELIMITER);
	unsigned int num = 1;
	for (int i = 0; i < my_packet->PublicChannelCount; ++i)
	{
		std::string strNames;
		strNames += std::to_string(num++);
		strNames += ". " + publicNames[i];
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(strNames.c_str()));
	}

	SeekLastAddedCursor(listLog);
}

void Framework::ProcessChannelEnter(unsigned char* packet)
{ 
	packet_channel_enter* my_packet = reinterpret_cast<packet_channel_enter*>(packet);
	currentChannelMaster = my_packet->ChannelMaster;
	::SetWindowText(editChannelName, my_packet->ChannelName);
	userChannel = my_packet->ChannelName;

	::SendMessage(listUsers, LB_RESETCONTENT, 0, 0);
	
	std::string sysMsg("***System*** ");
	sysMsg += my_packet->ChannelName;
	sysMsg += " �� ���Խ��ϴ�.";
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
	SeekLastAddedCursor(listLog);
}

void Framework::ProcessChannelUsers(unsigned char* packet)
{
	packet_channel_users* my_packet = reinterpret_cast<packet_channel_users*>(packet);
	if (my_packet->UserCountInPacket == 0)
		return;

	auto usersInChannel = SplitString(my_packet->UserNames, NAME_DELIMITER);
	for (int i = 0; i < my_packet->UserCountInPacket; ++i)
	{
		std::string name = usersInChannel[i];
		if (usersInChannel[i] == currentChannelMaster)
			name += "��";
		if (usersInChannel[i] == userName)
			name += " (You)";

		::SendMessage(listUsers, LB_ADDSTRING, 0, (LPARAM)name.c_str());
	}
	SeekLastAddedCursor(listUsers);
}

void Framework::ProcessNewfaceEnter(unsigned char* packet)
{
	packet_newface_enter* my_packet = reinterpret_cast<packet_newface_enter*>(packet);
	if(my_packet->User != userName)
		::SendMessage(listUsers, LB_ADDSTRING, 0, LPARAM(my_packet->User));
}

void Framework::ProcessUserLeave(unsigned char* packet)
{
	packet_user_leave* my_packet = reinterpret_cast<packet_user_leave*>(packet);
	
	std::string sysMsg("***System*** ");
	if (my_packet->IsKicked)
	{
		sysMsg += my_packet->User + std::string(" ���� ������߽��ϴ�.");
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}

	int slot = ::SendMessage(listUsers, LB_FINDSTRING, 0, LPARAM(my_packet->User));

	if(slot != LB_ERR)
		::SendMessage(listUsers, LB_DELETESTRING, WPARAM(slot), 0);
	else
	{
		sysMsg += "ä�� ����, ä�ο� �ٽ� �������ֽñ� �ٶ��ϴ�.";
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(sysMsg.c_str()));
		SeekLastAddedCursor(listLog);
	}
	
}

void Framework::ProcessChatting(unsigned char* packet)
{
	packet_chatting* my_packet = reinterpret_cast<packet_chatting*>(packet);
	
	std::string chatMsg;
	if (my_packet->IsWhisper == true)
	{
		if (my_packet->Talker == userName)
			chatMsg += (std::string(my_packet->Listner) + " �Կ��� : ");
		else
			chatMsg += (std::string(my_packet->Talker) + " ���� �ӼӸ� : ");
	}
	else
		chatMsg += std::string(my_packet->Talker) + " : ";

	chatMsg += my_packet->Chat;
	::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM(chatMsg.c_str()));
	SeekLastAddedCursor(listLog);
}

void Framework::ProcessNewMaster(unsigned char* packet)
{
	packet_new_master* my_packet = reinterpret_cast<packet_new_master*>(packet);
	if (my_packet->Channel != userChannel)
		return;

	currentChannelMaster = my_packet->Master;
	int slot = ::SendMessage(listUsers, LB_FINDSTRING, 0, LPARAM(my_packet->Master));

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

	if (id.size() > MAX_USERNAME_LENGTH)
	{
		::MessageBox(mainWindow, "�ʹ� �� �̸��Դϴ�.", "���� �Ұ�", MB_OK);
		return;
	}

	if (false == IsValidUserName(id))
	{
		::MessageBox(mainWindow, "��� �Ұ����� ���̵��Դϴ�.", "���� �Ұ�", MB_OK);
		return;
	}

	packet_login* my_packet = reinterpret_cast<packet_login *>(userSocket.GetSendWsaBuf().buf);
	::ZeroMemory(my_packet, sizeof(packet_login));
	my_packet->Size = sizeof(packet_login);
	my_packet->Type = PACKET_LOGIN;
	my_packet->Created = false;
	std::memcpy(&(my_packet->User), id.c_str(), id.size());

	userSocket.SendPacket(reinterpret_cast<unsigned char*>(my_packet));
}

void Framework::RequestWhisper(const std::string& listener, const std::string& chat)
{
	if (listener == userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ���ο��� �ӼӸ� �� �� �����ϴ�!"));
		SeekLastAddedCursor(listLog);
		return;
	}

	packet_chatting* my_packet = reinterpret_cast<packet_chatting *>(userSocket.GetSendWsaBuf().buf);
	::ZeroMemory(my_packet, sizeof(packet_chatting));
	my_packet->Size = sizeof(packet_chatting);
	my_packet->Type = PACKET_CHATTING;
	my_packet->IsWhisper = true;

	std::memcpy(&(my_packet->Talker), userName.c_str(), userName.size());
	std::memcpy(&(my_packet->Listner), listener.c_str(), listener.size());
	std::memcpy(&(my_packet->Chat), chat.c_str(), chat.size());

	userSocket.SendPacket(reinterpret_cast<unsigned char*>(my_packet));
}

void Framework::RequestChannelList()
{
	//client to server - Size, Type�� ����
	packet_channel_list* my_packet = reinterpret_cast<packet_channel_list *>(userSocket.GetSendWsaBuf().buf);
	my_packet->Size = sizeof(my_packet->Size) + sizeof(my_packet->Type);
	my_packet->Type = PACKET_CHANNEL_LIST;

	userSocket.SendPacket(reinterpret_cast<unsigned char*>(my_packet));
}

void Framework::RequestChannelChange(const std::string& channelName)
{
	if (channelName == userChannel)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** �̹� ��� ä���Դϴ�."));
		SeekLastAddedCursor(listLog);
		return;
	}

	//client to server - ä���̸��� �ʱ�ȭ
	packet_channel_enter* my_packet = reinterpret_cast<packet_channel_enter *>(userSocket.GetSendWsaBuf().buf);
	::ZeroMemory(my_packet, sizeof(packet_channel_enter));
	my_packet->Size = sizeof(packet_channel_enter);
	my_packet->Type = PACKET_CHANNEL_ENTER;
	std::memcpy(&(my_packet->ChannelName), channelName.c_str(), channelName.size());

	userSocket.SendPacket(reinterpret_cast<unsigned char*>(my_packet));
}

void Framework::RequestKick(const std::string& target)
{
	if (currentChannelMaster != userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ����� ������ �ƴմϴ�!"));
		return;
	}
	if (target == userName)
	{
		::SendMessage(listLog, LB_ADDSTRING, 0, LPARAM("***System*** ������ ������ �� �����ϴ�!"));
		return;
	}
	SeekLastAddedCursor(listLog);
	

	packet_kick_user* my_packet = reinterpret_cast<packet_kick_user *>(userSocket.GetSendWsaBuf().buf);
	::ZeroMemory(my_packet, sizeof(packet_kick_user));
	my_packet->Size = sizeof(packet_kick_user);
	my_packet->Type = PACKET_KICK_USER;
	std::memcpy(&(my_packet->Target), target.c_str(), target.size());
	std::memcpy(&(my_packet->Kicker), userName.c_str(), userName.size());
	std::memcpy(&(my_packet->Channel), userChannel.c_str(), userChannel.size());

	userSocket.SendPacket(reinterpret_cast<unsigned char*>(my_packet));
}

void Framework::RequestChatting(const std::string& chat)
{
	packet_chatting* my_packet = reinterpret_cast<packet_chatting*>(userSocket.GetSendWsaBuf().buf);
	::ZeroMemory(my_packet, sizeof(packet_chatting));
	my_packet->Size = sizeof(packet_chatting);
	my_packet->Type = PACKET_CHATTING;
	my_packet->IsWhisper = false;

	std::memcpy(&(my_packet->Talker), userName.c_str(), userName.size());
	std::memcpy(&(my_packet->Chat), chat.c_str(), chat.size());

	userSocket.SendPacket(reinterpret_cast<unsigned char*>(my_packet));
}