#pragma once

#include <map>
#include <vector>
#include <string>
#include <functional>

class StreamWriter;
class StreamReader;

class Serializable
{
public:
	virtual void Serialize(StreamWriter&) const = 0;
	virtual void Deserialize(StreamReader&) = 0;

	virtual ~Serializable() {}
};


class Packet_Base
	: public Serializable
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

struct Packet_System
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType		systemMessage;
};

struct Packet_Login
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	bool			isCreated; //only server uses. sends to client whether successfully created or not
	StringType		userName;
};

//Custom channel은 개수만 알려준다.
struct Packet_Channel_List
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	ValueType					customChannelCount;
	std::vector<StringType>		publicChannelNames;
};

struct Packet_Channel_Enter
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType		channelName;
	StringType		channelMaster;
};

struct Packet_Channel_Users
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType					channelName;
	std::vector<StringType>		userNames;
};

struct Packet_Newface_Enter
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType			userName;
};

struct Packet_User_Leave
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	bool				isKicked;
	StringType			userName;
};

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

struct Packet_New_Master
	: public Packet_Base
{
	static const TypeAdder	typeAdder;

	void Serialize(StreamWriter&) const;
	void Deserialize(StreamReader&);

	StringType			channelName;
	StringType			master;
};