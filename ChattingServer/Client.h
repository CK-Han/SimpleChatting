#pragma once
#include <WinSock2.h>
#include <string>
#include "../Common/Protocol.h"

enum Overlap_Operation {
	OPERATION_RECV = 1
	, OPERATION_SEND
};

struct Overlap_Exp
{
	::WSAOVERLAPPED Original_Overlap;
	int Operation;
	WSABUF WsaBuf;
	unsigned char Iocp_Buffer[MAX_BUFF_SIZE];
};

struct Client
{
	int					Serial;
	SOCKET				ClientSocket;
	bool				IsLogin;
	Overlap_Exp			RecvOverlap;
	int					PacketSize;
	int					PreviousCursor;
	unsigned char		PacketBuff[MAX_BUFF_SIZE];

	std::string			UserName;
	std::string			ChannelName;

	Client()
		: Serial(-1)
		, ClientSocket(INVALID_SOCKET)
		, IsLogin(false)
		, PacketSize(0)
		, PreviousCursor(0)
	{
		::ZeroMemory(&RecvOverlap, sizeof(RecvOverlap));
		::ZeroMemory(PacketBuff, sizeof(PacketBuff));

		RecvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(RecvOverlap.Iocp_Buffer);
		RecvOverlap.WsaBuf.len = sizeof(RecvOverlap.Iocp_Buffer);
	}
};

