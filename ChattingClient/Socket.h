#pragma once
#include <winsock2.h>
#include <windows.h>
#include "../Common/Protocol.h"


/**
	@class		Socket
	@brief		WSAAsyncSelect로 작성된 클라이언트용 통신 클래스
	@details	
*/
class Socket
{
public:
	Socket();
	~Socket();

	static const int		WM_SOCKET = WM_USER + 1;

	bool		Initialize(HWND mainWindow, const char* serverIP);
	WSABUF&		GetSendWsaBuf() { return sendWsaBuf; }

	void ReadPacket(SOCKET sock);
	void SendPacket(const void* packet);

	void ProcessPacket(const void* packet, int size);


private:
	HWND					hWnd;

	SOCKET					clientSocket;
	bool					isInitialized;
	WSABUF					sendWsaBuf;
	unsigned char			sendBuf[Packet_Base::MAX_BUF_SIZE];
	WSABUF					recvWsaBuf;
	unsigned char			recvBuf[Packet_Base::MAX_BUF_SIZE];
	unsigned char			packetBuf[Packet_Base::MAX_BUF_SIZE];

	int						inPacketSize;
	int						savedPacketSize;
};

