#include "protocol.h"
#include "stream.h"

#include <iostream>
#include <algorithm>
#include <numeric>
////////////////////////////////////
/*
	Serialize
	1. out << size << type << datas;
		데이터 순서대로 담기
	2. out.GetStreamSize() 값으로 Size 재조정

	Deserialize
	1. in >> packet.size >> packet.type;
		헤더 제거
	2. datas를 serialize에서 했던 방식대로 파싱
*/
///////////////////////////////////

/**
	@static		패킷의 type값을 해싱한 이후 충돌(혹은 중복)을 확인하기 위한 map
*/
std::map<Packet_Base::ValueType, Packet_Base::StringType> Packet_Base::registeredTypes;

/**
	@brief		입력된 패킷 클래스 이름을 해싱하여 관리구조에 등록한다.
	@return		해싱된 ValueType 정수이며 충돌로 인해 중복된 값일 수 있다.

	@todo		해시 충돌 및 다운캐스팅시의 중복을 처리해야 한다.
*/
Packet_Base::ValueType Packet_Base::RegisterType(const StringType& packetName)
{
	ValueType type = static_cast<ValueType>(HashType{}(packetName));

	if (false == registeredTypes.insert(std::make_pair(type, packetName)).second)
	{
		std::cerr << "RegisterType Error - Packet Name : " << packetName << ", hash : " << type << std::endl;
		std::cerr << "Existed Packet Name : " << registeredTypes[type] << std::endl;
	}

	return type;
}

Packet_Base::TypeAdder::TypeAdder(const Packet_Base::StringType& packetName)
	: type(Packet_Base::RegisterType(packetName))
{
}

/**
	@warning		버퍼의 널 검사는 진행하나 메모리 침범은 검사하지 않는다.
*/
Packet_Base::ValueType GetPacketSize(const void* buf)
{
	if (buf == nullptr)	return 0;

	Packet_Base::ValueType size = 0;
	StreamReader stream(buf, sizeof(size));
	stream >> size;

	return size;
}

/**
	@warning		버퍼의 널 검사는 진행하나 메모리 침범은 검사하지 않는다.
*/
Packet_Base::ValueType GetPacketType(const void* buf)
{
	if (buf == nullptr)	return 0;

	Packet_Base::ValueType size = 0, type = 0;
	StreamReader stream(buf, sizeof(size) + sizeof(type));
	stream >> size >> type;

	return type;
}


/////////Protected Functions////////////
/**
	@brief		패킷의 헤더 작성
	@warning	처음 입력하는 Size는 쓰레기 값이며, SerializeEnd에서 스트림의 크기를 설정한다.
*/
void Packet_Base::SerializeBegin(StreamWriter& out, ValueType type) const
{
	out << ValueType(0) << type;	//Size is garbage value now
}

/**
	@brief		StreamWriter의 StreamSize(cursor)값을 통해 패킷의 최종 크기를 설정한다.
*/
void Packet_Base::SerializeEnd(StreamWriter& out) const
{
	auto streamSize = out.GetStreamSize();
	if (streamSize > MAX_BUF_SIZE)
	{
		std::cerr << "SerializeEnd Warning - Invalid Stream Size" << std::endl;
	}

	ValueType size = static_cast<ValueType>(streamSize);
	out.OverwriteRawData(0, &size, sizeof(size));
}


void Packet_Base::DeserializeBegin(StreamReader& in, ValueType type)
{
	ValueType size, packetType;
	in >> size >> packetType;		//header parsing

	if (size > MAX_BUF_SIZE
		|| packetType != type)
	{
		std::cerr << "DeserializeBegin Warning - invalid header value" << std::endl;
	}
}

void Packet_Base::DeserializeEnd(StreamReader&)
{
	//nothing to do
}

/**
	@brief		문자열 Serialize, 크기를 먼저 검사하여 복사 후 문자열을 복사한다.
	@details	최대 길이를 지정, std::min()으로 복사할 길이를 얻는다.
*/
void Packet_Base::SerializeString(StreamWriter& out, const StringType& str, ValueType maxSize = Packet_Base::MAX_BUF_SIZE) const
{
	StringType::size_type strSize
		= std::min<StringType::size_type>(str.size(), maxSize);

	out << strSize;
	out.WriteRawData(str.c_str(), strSize);
}

void Packet_Base::DeserializeString(StreamReader& in, StringType& str)
{
	StringType::size_type msgSize = 0;
	in >> msgSize;

	StringType::value_type buffer[MAX_BUF_SIZE];
	in.ReadRawData(buffer, msgSize);

	str = StringType(buffer, msgSize);
}
/////////Protected Functions////////////



///////////////////////////////////////////////////////////////////////////////////////
///////////////////////BEGIN///////////////////////////////////////////////////////////

const Packet_Base::TypeAdder Packet_System::typeAdder("Packet_System");
void Packet_System::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	SerializeString(out, systemMessage, MAX_MESSAGE_SIZE);

	SerializeEnd(out);
}

void Packet_System::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	DeserializeString(in, systemMessage);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Login::typeAdder("Packet_Login");
void Packet_Login::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());
	
	out << isCreated;
	SerializeString(out, userName, MAX_USERNAME_SIZE);

	SerializeEnd(out);
}

void Packet_Login::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	in >> isCreated;
	DeserializeString(in,userName);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Channel_List::typeAdder("Packet_Channel_List");
void Packet_Channel_List::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	out << customChannelCount;
	decltype(publicChannelNames)::size_type publicChannelCount = publicChannelNames.size();
	out << publicChannelCount;

	for(const auto& name : publicChannelNames)
	{
		SerializeString(out, name, MAX_CHANNELNAME_SIZE);
	}

	SerializeEnd(out);
}

void Packet_Channel_List::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	in >> customChannelCount;
	std::vector<StringType>::size_type publicChannelCount = 0;
	in >> publicChannelCount;

	publicChannelNames.clear();
	publicChannelNames.resize(publicChannelCount);

	for (auto& name : publicChannelNames)
	{
		DeserializeString(in, name);
	}

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Channel_Enter::typeAdder("Packet_Channel_Enter");
void Packet_Channel_Enter::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	SerializeString(out, channelName, MAX_CHANNELNAME_SIZE);
	SerializeString(out, channelMaster, MAX_USERNAME_SIZE);

	SerializeEnd(out);
}

void Packet_Channel_Enter::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	DeserializeString(in, channelName);
	DeserializeString(in, channelMaster);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Channel_Users::typeAdder("Packet_Channel_Users");
void Packet_Channel_Users::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	SerializeString(out, channelName, MAX_CHANNELNAME_SIZE);

	decltype(userNames)::size_type userCount = userNames.size();
	out << userCount;

	for (const auto& name : userNames)
	{
		SerializeString(out, name, MAX_USERNAME_SIZE);
	}

	SerializeEnd(out);
}

void Packet_Channel_Users::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	DeserializeString(in, channelName);

	decltype(userNames)::size_type userCount = 0;
	in >> userCount;

	userNames.clear();
	userNames.resize(userCount);
	
	for (auto& name : userNames)
	{
		DeserializeString(in, name);
	}

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Newface_Enter::typeAdder("Packet_Newface_Enter");
void Packet_Newface_Enter::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	SerializeString(out, userName, MAX_USERNAME_SIZE);
	
	SerializeEnd(out);
}

void Packet_Newface_Enter::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	DeserializeString(in, userName);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_User_Leave::typeAdder("Packet_User_Leave");
void Packet_User_Leave::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	out << isKicked;
	SerializeString(out, userName, MAX_USERNAME_SIZE);
	
	SerializeEnd(out);
}

void Packet_User_Leave::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	in >> isKicked;
	DeserializeString(in, userName);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Kick_User::typeAdder("Packet_Kick_User");
void Packet_Kick_User::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	SerializeString(out, kicker, MAX_USERNAME_SIZE);
	SerializeString(out, target, MAX_USERNAME_SIZE);
	SerializeString(out, channelName, MAX_CHANNELNAME_SIZE);

	SerializeEnd(out);
}

void Packet_Kick_User::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	DeserializeString(in, kicker);
	DeserializeString(in, target);
	DeserializeString(in, channelName);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_Chatting::typeAdder("Packet_Chatting");
void Packet_Chatting::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	out << isWhisper;
	SerializeString(out, talker, MAX_USERNAME_SIZE);
	SerializeString(out, listener, MAX_USERNAME_SIZE);
	SerializeString(out, chat, MAX_MESSAGE_SIZE);

	SerializeEnd(out);
}

void Packet_Chatting::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());
	
	in >> isWhisper;
	DeserializeString(in, talker);
	DeserializeString(in, listener);
	DeserializeString(in, chat);

	DeserializeEnd(in);
}


/////////////////////////////////////////////////////
const Packet_Base::TypeAdder Packet_New_Master::typeAdder("Packet_New_Master");
void Packet_New_Master::Serialize(StreamWriter& out) const
{
	SerializeBegin(out, typeAdder.GetType());

	SerializeString(out, channelName, MAX_CHANNELNAME_SIZE);
	SerializeString(out, master, MAX_USERNAME_SIZE);

	SerializeEnd(out);
}

void Packet_New_Master::Deserialize(StreamReader& in)
{
	DeserializeBegin(in, typeAdder.GetType());

	DeserializeString(in, channelName);
	DeserializeString(in, master);

	DeserializeEnd(in);
}