#include "stream.h"


/**
	@brief	StreamWriter를 초기화한다 buf의 nullptr 검사를 진행한다.

	@throw	StreamBase::InvalidStreamArgument : buf가 nullptr일 시
*/
StreamWriter::StreamWriter(void* buf, SizeType size)
	: buffer(reinterpret_cast<char*>(buf))
	, maxsize(size)
	, cursor(0)
{
	if (buf == nullptr)
		throw InvalidStreamArgument();
}

StreamWriter& StreamWriter::operator<<(bool src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(char src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(unsigned char src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(short src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(unsigned short src)
{
	Append(&src, sizeof(src));	return *this;

}
StreamWriter& StreamWriter::operator<<(int src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(unsigned int src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(float src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(double src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(long long src)
{
	Append(&src, sizeof(src));	return *this;
}

StreamWriter& StreamWriter::operator<<(unsigned long long src)
{
	Append(&src, sizeof(src));	return *this;
}


/**
	@brief		스트림에 raw data를 size만큼 복사한다.
	@throw		InvalidStreamArgument : src가 nullptr
				StreamWriteOverflow : 복사시 오버플로우 발생한 경우
*/
void StreamWriter::WriteRawData(const void* src, SizeType size)
{
	if (src == nullptr)
		throw InvalidStreamArgument();

	Append(src, size);
}

/**
	@brief		스트림에 지정한 위치로부터 덮어쓰기
	@throw		InvalidStreamArgument : src가 nullptr
				StreamWriteOverflow : 복사시 오버플로우 발생한 경우
*/
void StreamWriter::OverwriteRawData(SizeType begin, const void* src, SizeType size)
{
	if (src == nullptr)
		throw InvalidStreamArgument();

	if (maxsize < (begin + size))
		throw StreamWriteOverflow();

	std::memcpy(buffer + begin, src, size);
	if ((begin + size) > cursor)
		cursor = (begin + size);
}

/**
	@brief		버퍼에 데이터 추가
	@details	cursor를 통해 버퍼의 끝을 지정하여 붙여나간다.

	@throw		InvalidStreamArgument : src가 nullptr
				StreamWriteOverflow : 복사시 오버플로우 발생한 경우
*/
void StreamWriter::Append(const void* src, SizeType size)
{
	if (src == nullptr)
		throw InvalidStreamArgument();

	if ((cursor + size) >= maxsize)
		throw StreamWriteOverflow();

	std::memcpy(buffer + cursor, src, size);
	cursor += size;
}


/////////////////////////////////////////////////////////////////////////////////////////////

/**
	@brief StreamReader를 초기화한다. buf의 nullptr 검사 진행

	@throw	StreamBase::InvalidStreamArgument : buf가 nullptr일 시
*/
StreamReader::StreamReader(const void* buf, SizeType size)
	: buffer(reinterpret_cast<const char*>(buf))
	, maxsize(size)
	, cursor(0)
{
	if (buf == nullptr)
		throw InvalidStreamArgument();
}

StreamReader& StreamReader::operator>>(bool& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(char& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(unsigned char& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(short& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(unsigned short& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(int& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(unsigned int& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(float& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(double& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(long long& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}

StreamReader& StreamReader::operator>>(unsigned long long& dst)
{
	Parse(&dst, sizeof(dst)); return *this;
}


/**
	@brief		지정된 크기만큼 버퍼로부터 데이터를 읽어 dst에 복사한다.

	@throw		InvalidStreamArgument : dst가 nullptr
				StreamReadUnderflow : 읽을때 언더플로우 발생한 경우
*/
void StreamReader::ReadRawData(void* dst, SizeType size)
{
	if (dst == nullptr)
		throw InvalidStreamArgument();

	Parse(dst, size);
}

/**
	@brief		버퍼로부터 사이즈만큼 데이터 파싱
	@details	cursor를 통해 지금까지 읽은 위치의 끝을 지정한다.

	@throw		InvalidStreamArgument : dst가 nullptr
				StreamReadUnderflow : 읽을때 언더플로우 발생한 경우
*/
void StreamReader::Parse(void* dst, SizeType size)
{
	if (dst == nullptr)
		throw InvalidStreamArgument();

	if ((maxsize - cursor) < size)
		throw StreamReadUnderflow();

	std::memcpy(dst, buffer + cursor, size);
	cursor += size;
}