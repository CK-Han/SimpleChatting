#pragma once
#include <winsock2.h>
#include <windows.h>
#include "../Common/Protocol.h"

class Socket
{
public:
	Socket();
	~Socket();

	static const int		WM_SOCKET = WM_USER + 1;

	bool		Initialize(HWND mainWindow, const char* serverIP);
	bool		IsInitialized() const { return isInitialized;	}
	WSABUF&		GetSendWsaBuf() { return sendWsaBuf; }

	void ReadPacket(SOCKET sock);
	void SendPacket(unsigned char* packet);

	void ProcessPacket(unsigned char* packet);


private:
	HWND			hWnd;

	SOCKET					clientSocket;
	bool					isInitialized;
	WSABUF					sendWsaBuf;
	unsigned char			sendBuf[MAX_BUFF_SIZE];
	WSABUF					recvWsaBuf;
	unsigned char			recvBuf[MAX_BUFF_SIZE];
	unsigned char			packetBuf[MAX_BUFF_SIZE];

	int				inPacketSize{ 0 };
	int				savedPacketSize{ 0 };
};

