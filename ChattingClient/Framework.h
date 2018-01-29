#pragma once
#include <windows.h>
#include <string>
#include <list>
#include <vector>
#include "../Common/Protocol.h"

class Framework
{
private:
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

	void Initialize(HWND hWnd, HINSTANCE instance);
	
	void ProcessWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam);
	void ProcessInput();
	void ProcessPacket(unsigned char* packet);

	WNDPROC GetOldInputProc() const { return oldInputProc; }
	HWND	GetEditInput() const { return editInput; }
	Socket& GetUserSocket() { return userSocket; }

private:
	Framework();
	~Framework();

	CommandType GetCommandType(const std::vector<std::string>& tokens) const;
	bool		IsValidUserName(const std::string& id) const;
	void		SeekLastAddedCursor(HWND listBox);

	void ProcessSystemMessage(unsigned char* packet);
	void ProcessLogin(unsigned char* packet);
	void ProcessChannelList(unsigned char* packet);
	void ProcessChannelEnter(unsigned char* packet);
	void ProcessChannelUsers(unsigned char* packet);
	void ProcessNewfaceEnter(unsigned char* packet);
	void ProcessUserLeave(unsigned char* packet);
	void ProcessChatting(unsigned char* packet);
	void ProcessNewMaster(unsigned char* packet);

	void RequestLogin(const std::string& id);
	void RequestWhisper(const std::string& listener, const std::string& chat);
	void RequestChannelList();
	void RequestChannelChange(const std::string& channelName);
	void RequestKick(const std::string& target);
	void RequestChatting(const std::string& chat);

private:
	unsigned int		clientWidth;
	unsigned int		clientHeight;
	bool				isInitialized;
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
};