#pragma once
#include <WinSock2.h>
#include <string>
#include <mutex>
#include "../Common/protocol.h"

enum Overlap_Operation {
	OPERATION_RECV = 1
	, OPERATION_SEND
};

struct Overlap_Exp
{
	::WSAOVERLAPPED Original_Overlap;
	int Operation;
	size_t Serial;
	WSABUF WsaBuf;
	unsigned char Iocp_Buffer[Packet_Base::MAX_BUF_SIZE];
};

struct Client
{
	size_t				Serial;
	SOCKET				ClientSocket;
	bool				IsConnect;
	Overlap_Exp			RecvOverlap;
	int					PacketSize;
	int					PreviousCursor;
	unsigned char		PacketBuff[Packet_Base::MAX_BUF_SIZE];

	std::mutex			clientMutex;
	std::string			UserName;
	std::string			ChannelName;

	Client()
		: Serial((std::numeric_limits<size_t>::max)())
		, ClientSocket(INVALID_SOCKET)
		, IsConnect(false)
		, PacketSize(0)
		, PreviousCursor(0)
	{
		::ZeroMemory(&RecvOverlap, sizeof(RecvOverlap));
		::ZeroMemory(PacketBuff, sizeof(PacketBuff));

		RecvOverlap.WsaBuf.buf = reinterpret_cast<CHAR*>(RecvOverlap.Iocp_Buffer);
		RecvOverlap.WsaBuf.len = sizeof(RecvOverlap.Iocp_Buffer);
	}
};

