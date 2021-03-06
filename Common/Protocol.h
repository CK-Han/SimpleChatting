#pragma once

#include <map>
#include <vector>
#include <string>
#include <functional>

class StreamWriter;
class StreamReader;

/**
	@class Packet_Base
	@brief		통신을 위해 기본적인 내용이 정의된 패킷 베이스 클래스
	@details	상수 나열 - 통신에 필요한 상수들을 정의한다. 
				별칭 - 패킷들이 공통적으로 사용할 기본 타입설정
				패킷 타입 관리 - 정의된 Hash를 사용, 해시값으로 타입을 구분하도록 한다.
				공통적인 직렬화 관련 동작 정의 - Begin, End 및 Stream에 정의되지 않은 동작 (컨테이너들)

	@warning	StreamWriter 및 Reader를 다루는 만큼, 예외에 신경써서 작업해야한다.
				해시 충돌을 염두해두어야 한다.

	@todo		예외 상황 처리에 대해 생각해야한다. 현재는 단순히 std::cerr에 기록한다.
*/
class Packet_Base
{
public:
	using ValueType = unsigned short;
	using StringType = std::string;
	using HashType = std::hash<StringType>;

protected:
	class TypeAdder
	{
	public:
		TypeAdder(const StringType& packetName);
		ValueType GetType() const { return type; }

	private:
		const ValueType type;
	};


public:
	virtual void Serialize(StreamWriter&) const = 0;
	virtual void Deserialize(StreamReader&) = 0;

	virtual ~Packet_Base() {}


protected:
	void SerializeBegin(StreamWriter&, ValueType type) const;
	void SerializeEnd(StreamWriter&) const;

	void DeserializeBegin(StreamReader&, ValueType type);
	void DeserializeEnd(StreamReader&);

	void SerializeString(StreamWriter&, const StringType&, ValueType maxSize) const;
	void DeserializeString(StreamReader&, StringType&);

private:
	static ValueType RegisterType(const StringType& packetName);
	
	static std::map<ValueType, StringType> registeredTypes;


///////////////////상수 나열//////////////////////
public:
	static const unsigned short		PORT_NUMBER				 = 6000;
	static const unsigned short		MAX_BUF_SIZE			 = 4096;

	static const unsigned short		MAX_MESSAGE_SIZE		 = 128;
	static const unsigned short		MAX_USERNAME_SIZE		 = 16;
	static const unsigned short		MAX_CHANNELNAME_SIZE	 = 32;
};

Packet_Base::ValueType GetPacketSize(const void* buf);
Packet_Base::ValueType GetPacketType(const void* buf);

///////////////////////////////////////////////////////////////////////////////////////

/**
	@brief		서버 -> 클라이언트 시스템 메세지
*/
struct Packet_System
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType		systemMessage;
};

/**
	@brief		클라이언트 -> 서버 : 이 이름으로 접속하고싶다
				서버 -> 클라이언트 : isCreated로 생성 되었는지 알림
*/
struct Packet_Login
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	bool			isCreated;
	StringType		userName;
};


/**
	@brief		클라이언트 -> 서버 : 리스트 요청, 데이터를 담지 않음
				서버 -> 클라이언트 : 리스트 전달, 커스텀채널은 개수만 확인

	@warning	데이터를 담지 않더라도, 멤버변수 초기화를 누락하지 않아야 한다.
*/
struct Packet_Channel_List
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	ValueType					customChannelCount;
	std::vector<StringType>		publicChannelNames;
};

/**
	@brief		클라이언트 -> 서버 : 채널 연결 요청
				서버 -> 클라이언트 : 채널 연결 확인, 방장이 누구인지 알림

	@warning	클라 -> 서버의 경우더라도 멤버변수 초기화를 누락하지 않도록 주의한다.
				즉, StringType의 기본생성자를 명시적으로라도 호출하도록 한다. 
				(현재는 std::string이라 누락시에도 문제는 없다.)
*/
struct Packet_Channel_Enter
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType		channelName;
	StringType		channelMaster;
};

/**
	@brief		서버 -> 클라이언트 : 채널에 존재하는 유저 리스트 전달
*/
struct Packet_Channel_Users
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType					channelName;
	std::vector<StringType>		userNames;
};

/**
	@brief		서버 -> 클라이언트 : 클라이언트가 존재하는 채널에 새 유저 접속 알림
*/
struct Packet_Newface_Enter
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType			userName;
};

/**
	@brief		서버 -> 클라이언트 : 유저가 채널을 떠났으며, 강퇴에 의한 것인지에 대한 정보 전달
*/
struct Packet_User_Leave
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	bool				isKicked;
	StringType			userName;
};

/**
	@brief		클라이언트 -> 서버 : 자신이 target을 강퇴하겠다 서버에 요청
*/
struct Packet_Kick_User
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType			kicker;
	StringType			target;
	StringType			channelName;
};

/**
	@brief		클라이언트 -> 서버 : 채팅내용 전달, 귓속말인지 여부 포함
				서버 -> 클라이언트 : 채팅내용 전달, 귓속말이 아닌 경우 채널 유저들에게 broadcast
*/
struct Packet_Chatting
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	bool				isWhisper;
	StringType			talker;
	StringType			listener;
	StringType			chat;
};

/**
	@brief		서버 -> 클라이언트 : 채널의 방장이 exit, 방장이 바뀌었음을 채널에 broadcast
*/
struct Packet_New_Master
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType			channelName;
	StringType			master;
};