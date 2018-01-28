#pragma once

#define MAX_BUFF_SIZE			4000
#define MY_SERVER_PORT			9000

#define NAME_DELIMITER			'$'
#define MAX_SYSTEMMSG_LENGTH		64
#define MAX_USERNAME_LENGTH			16
#define MAX_USERNAME_SENDED			200
#define MAX_CHANNEL_USERS			200
#define MAX_PUBLIC_CHANNEL_COUNT	12
#define MAX_CHANNELNAME_LENGTH		32
#define MAX_CHATTING_SIZE			128

enum Packet_Type {
	PACKET_SYSTEM = 0
	, PACKET_LOGIN
	, PACKET_CHANNEL_LIST
	, PACKET_CHANNEL_ENTER
	, PACKET_CHANNEL_USERS
	, PACKET_NEWFACE_ENTER
	, PACKET_USER_LEAVE
	, PACKET_KICK_USER
	, PACKET_CHATTING
	, PACKET_NEW_MASTER
};

namespace
{
	struct packet_base
	{
		unsigned short	Size;
		unsigned char	Type;
	};

	unsigned short	 GetPacketSize(unsigned char* packet)
	{
		packet_base* base = reinterpret_cast<packet_base*>(packet);
		return base->Size;
	}

	unsigned char	 GetPacketType(unsigned char* packet)
	{
		packet_base* base = reinterpret_cast<packet_base*>(packet);
		return base->Type;
	}
}


struct packet_system
{
	unsigned short	Size;
	unsigned char	Type;

	char			SystemMessage[MAX_SYSTEMMSG_LENGTH];
};

struct packet_login
{
	unsigned short	Size;
	unsigned char	Type;

	bool			Created; //only server uses, sends to client
	char			User[MAX_USERNAME_LENGTH];
};

//커스텀 채널의 이름들은 확인할 수 없다. '$' 로 공개채널 이름 구분
struct packet_channel_list
{
	unsigned short	Size;
	unsigned char	Type;

	char			PublicChannelCount;
	int				CustomChannelCount;

	char			PublicChannelNames[MAX_CHANNELNAME_LENGTH * (MAX_PUBLIC_CHANNEL_COUNT + 1)];
};

struct packet_channel_enter
{
	unsigned short	Size;
	unsigned char	Type;

	char		ChannelName[MAX_CHANNELNAME_LENGTH];
	char		ChannelMaster[MAX_USERNAME_LENGTH];
};

//NAME_DELIMITER '$' 으로 아이디 구분, 최대 MAX_USERNAME_SENDED 만큼 보낸다.
struct packet_channel_users
{
	unsigned short	Size;
	unsigned char	Type;

	int			UserCountInPacket;
	char		ChannelName[MAX_CHANNELNAME_LENGTH];
	char		UserNames[MAX_USERNAME_LENGTH * (MAX_USERNAME_SENDED + 1)];
};

struct packet_newface_enter
{
	unsigned short	Size;
	unsigned char	Type;

	char			User[MAX_USERNAME_LENGTH];
};

struct packet_user_leave
{
	unsigned short	Size;
	unsigned char	Type;

	bool			IsKicked;
	char			User[MAX_USERNAME_LENGTH];
};

struct packet_kick_user
{
	unsigned short	Size;
	unsigned char	Type;

	char			Kicker[MAX_USERNAME_LENGTH];
	char			Target[MAX_USERNAME_LENGTH];
	char			Channel[MAX_CHANNELNAME_LENGTH];
};

struct packet_chatting
{
	unsigned short	Size;
	unsigned char	Type;

	bool			IsWhisper;
	char			Talker[MAX_USERNAME_LENGTH];
	char			Listner[MAX_USERNAME_LENGTH];
	char			Chat[MAX_CHATTING_SIZE];
};

struct packet_new_master
{
	unsigned short	Size;
	unsigned char	Type;

	char			Channel[MAX_CHANNELNAME_LENGTH];
	char			Master[MAX_USERNAME_LENGTH];
};