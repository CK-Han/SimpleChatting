#pragma once

#include <map>
#include <vector>
#include <string>
#include <functional>

class StreamWriter;
class StreamReader;

/**
	@class Packet_Base
	@brief		����� ���� �⺻���� ������ ���ǵ� ��Ŷ ���̽� Ŭ����
	@details	��� ���� - ��ſ� �ʿ��� ������� �����Ѵ�. 
				��Ī - ��Ŷ���� ���������� ����� �⺻ Ÿ�Լ���
				��Ŷ Ÿ�� ���� - ���ǵ� Hash�� ���, �ؽð����� Ÿ���� �����ϵ��� �Ѵ�.
				�������� ����ȭ ���� ���� ���� - Begin, End �� Stream�� ���ǵ��� ���� ���� (�����̳ʵ�)

	@warning	StreamWriter �� Reader�� �ٷ�� ��ŭ, ���ܿ� �Ű�Ἥ �۾��ؾ��Ѵ�.
				�ؽ� �浹�� �����صξ�� �Ѵ�.

	@todo		���� ��Ȳ ó���� ���� �����ؾ��Ѵ�. ����� �ܼ��� std::cerr�� ����Ѵ�.
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


///////////////////��� ����//////////////////////
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
	@brief		���� -> Ŭ���̾�Ʈ �ý��� �޼���
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
	@brief		Ŭ���̾�Ʈ -> ���� : �� �̸����� �����ϰ�ʹ�
				���� -> Ŭ���̾�Ʈ : isCreated�� ���� �Ǿ����� �˸�
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
	@brief		Ŭ���̾�Ʈ -> ���� : ����Ʈ ��û, �����͸� ���� ����
				���� -> Ŭ���̾�Ʈ : ����Ʈ ����, Ŀ����ä���� ������ Ȯ��

	@warning	�����͸� ���� �ʴ���, ������� �ʱ�ȭ�� �������� �ʾƾ� �Ѵ�.
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
	@brief		Ŭ���̾�Ʈ -> ���� : ä�� ���� ��û
				���� -> Ŭ���̾�Ʈ : ä�� ���� Ȯ��, ������ �������� �˸�

	@warning	Ŭ�� -> ������ ������ ������� �ʱ�ȭ�� �������� �ʵ��� �����Ѵ�.
				��, StringType�� �⺻�����ڸ� ��������ζ� ȣ���ϵ��� �Ѵ�. 
				(����� std::string�̶� �����ÿ��� ������ ����.)
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
	@brief		���� -> Ŭ���̾�Ʈ : ä�ο� �����ϴ� ���� ����Ʈ ����
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
	@brief		���� -> Ŭ���̾�Ʈ : Ŭ���̾�Ʈ�� �����ϴ� ä�ο� �� ���� ���� �˸�
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
	@brief		���� -> Ŭ���̾�Ʈ : ������ ä���� ��������, ���� ���� �������� ���� ���� ����
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
	@brief		Ŭ���̾�Ʈ -> ���� : �ڽ��� target�� �����ϰڴ� ������ ��û
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
	@brief		Ŭ���̾�Ʈ -> ���� : ä�ó��� ����, �ӼӸ����� ���� ����
				���� -> Ŭ���̾�Ʈ : ä�ó��� ����, �ӼӸ��� �ƴ� ��� ä�� �����鿡�� broadcast
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
	@brief		���� -> Ŭ���̾�Ʈ : ä���� ������ exit, ������ �ٲ������ ä�ο� broadcast
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