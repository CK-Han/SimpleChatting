#pragma once
#include <WinSock2.h>
#include <string>
#include <mutex>
#include "../Common/protocol.h"

/**
	@struct Overlap_Exp
	@brief		iocp 송수신에 사용되는 overlap 구조체의 확장
	@details	Serial의 최대값(numeric_limits)을 오류 값으로 사용한다.
*/
struct Overlap_Exp
{
	enum Overlap_Operation {
		OPERATION_RECV = 1
		, OPERATION_SEND
	};

	::WSAOVERLAPPED Original_Overlap;
	int Operation;
	size_t Serial;
	WSABUF WsaBuf;
	unsigned char Iocp_Buffer[Packet_Base::MAX_BUF_SIZE];

	Overlap_Exp()
		: Operation(0)
		, Serial((std::numeric_limits<size_t>::max)())
	{
		::ZeroMemory(&Original_Overlap, sizeof(Original_Overlap));
		::ZeroMemory(Iocp_Buffer, sizeof(Iocp_Buffer));

		WsaBuf.buf = reinterpret_cast<CHAR*>(Iocp_Buffer);
		WsaBuf.len = sizeof(Iocp_Buffer);
	}
};


/**
	@struct Client
	@brief		채팅 클라이언트 정의
	@details	Serial의 최대값(numeric_limits)을 오류 값으로 사용한다.
				PacketSize는 다음에 조립할 패킷 크기이며
				SavedPacketSize 패킷버퍼에 총 저장된 크기이다.
				즉, PacketSize <= SavedPacketSize 이면 패킷 조립이 가능하다.
	
	@warning	Overlap_Exp의 Serial과 Client의 Serial을 구분해야한다.
*/
struct Client
{
	size_t				Serial;
	SOCKET				ClientSocket;
	bool				IsConnect;
	Overlap_Exp			RecvOverlap;

	int					PacketSize;
	int					SavedPacketSize;
	unsigned char		PacketBuff[Packet_Base::MAX_BUF_SIZE];

	std::mutex			clientMutex;
	std::string			UserName;
	std::string			ChannelName;

	Client()
		: Serial((std::numeric_limits<size_t>::max)())
		, ClientSocket(INVALID_SOCKET)
		, IsConnect(false)
		, PacketSize(0)
		, SavedPacketSize(0)
	{
		::ZeroMemory(PacketBuff, sizeof(PacketBuff));
	}
};

