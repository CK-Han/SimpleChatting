#pragma once
#include <windows.h>
#include <string>
#include <list>
#include <vector>
#include <map>
#include "../Common/Protocol.h"

/**
	@class		Framework
	@brief		채팅 클라이언트 메인 프레임워크
	@details	button, edit control, list box를 사용해 간단히 만든 채팅 클라이언트
	@author		cgHan
	@date		2018/05/05
	@version	0.0.1
*/
class Framework
{
private:
	using Packet_Procedure = void (Framework::*)(StreamReader&);

	static constexpr unsigned int		BLANK = 10;
	static constexpr unsigned int		CHAT_HEIGHT = 25;
	static constexpr float				CHAT_WIDTH_RATIO = 0.70f;
	static constexpr float				LOG_HEIGHT_RATIO = 0.85f;
	static constexpr float				CHANNEL_WIDTH_RATIO = 0.25f;
	static constexpr float				USERS_HEIGHT_RATIO = 0.70f;
	static constexpr float				COMMAND_HEIGHT_RATIO = 0.15f;

	enum class CommandType { 
		MISUSE = 0
		, WHISPER
		, CHANNELLIST
		, CHANNELCONNECT
		, KICKUSER
		, CHATTING
	};

public:
	static Framework* GetInstance() 
	{
		static Framework framework;
		return &framework;
	}

	bool Run(HWND hWnd, HINSTANCE instance);
	
	void ProcessWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam);
	void ProcessUserInput();
	void ProcessPacket(const void* packet, int size);

	WNDPROC GetOldInputProc() const { return oldInputProc; }
	HWND	GetEditInput() const { return editInput; }
	Socket& GetUserSocket() { return userSocket; }

private:
	Framework();
	~Framework();

	CommandType GetCommandType(const std::vector<std::string>& tokens) const;
	bool		IsValidUserName(const std::string& id) const;
	void		SeekLastAddedCursor(HWND listBox);

	void Process_SystemMessage(StreamReader&);
	void Process_Login(StreamReader&);
	void Process_ChannelList(StreamReader&);
	void Process_ChannelEnter(StreamReader&);
	void Process_ChannelUsers(StreamReader&);
	void Process_NewfaceEnter(StreamReader&);
	void Process_UserLeave(StreamReader&);
	void Process_Chatting(StreamReader&);
	void Process_NewMaster(StreamReader&);

	void RequestLogin(const std::string& id);
	void RequestWhisper(const std::string& listener, const std::string& chat);
	void RequestChannelList();
	void RequestChannelChange(const std::string& channelName);
	void RequestKick(const std::string& target);
	void RequestChatting(const std::string& chat);

private:
	unsigned int		clientWidth;
	unsigned int		clientHeight;
	bool				isRunning;
	bool				isLogin;

	HWND				mainWindow;
	HINSTANCE			mainInstance;

	HWND				editInput;
	HWND				listLog;
	HWND				editChannelName;
	HWND				listUsers;
	HWND				textCommands;

	WNDPROC				oldInputProc;

	Socket				userSocket;
	std::string			userName;
	std::string			userChannel;
	std::string			currentChannelMaster;

	std::map<Packet_Base::ValueType /*type*/, Packet_Procedure> procedures;
};