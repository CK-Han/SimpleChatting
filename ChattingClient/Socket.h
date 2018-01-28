#pragma once
#include <winsock2.h>
#include <windows.h>
#include "Protocol.h"

class Socket
{
public:
	static const int		WM_SOCKET = WM_USER + 1;

	static Socket* GetInstance()
 	{
		static Socket soc;
		return &soc;
	}

	bool Initialize(HWND mainWindow, const char* serverIP);
	bool IsInitialized() const { return isInitialized;	}

	void ReadPacket(SOCKET sock);
	void SendPacket(unsigned char* packet);

	void ProcessPacket(unsigned char* packet);


	WSABUF*		GetSendWsaBuf() { return &sendWsaBuf; }

private:
	Socket();
	~Socket();

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

