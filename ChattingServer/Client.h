#pragma once
#include <WinSock2.h>
#include <string>
#include <mutex>
#include "../Common/protocol.h"

/**
	@struct Overlap_Exp
	@brief		iocp �ۼ��ſ� ���Ǵ� overlap ����ü�� Ȯ��
	@details	Serial�� �ִ밪(numeric_limits)�� ���� ������ ����Ѵ�.
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
	@brief		ä�� Ŭ���̾�Ʈ ����
	@details	Serial�� �ִ밪(numeric_limits)�� ���� ������ ����Ѵ�.
				PacketSize�� ������ ������ ��Ŷ ũ���̸�
				SavedPacketSize ��Ŷ���ۿ� �� ����� ũ���̴�.
				��, PacketSize <= SavedPacketSize �̸� ��Ŷ ������ �����ϴ�.
	
	@warning	Overlap_Exp�� Serial�� Client�� Serial�� �����ؾ��Ѵ�.
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

