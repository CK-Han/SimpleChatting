#include "stream.h"


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

void StreamWriter::WriteRawData(const void* src, SizeType size)
{
	if (src == nullptr)
		throw InvalidStreamArgument();

	Append(src, size);
}

void StreamWriter::OverwriteRawData(SizeType begin, const void* src, SizeType size)
{
	if (src == nullptr)
		throw InvalidStreamArgument();

	if (maxsize <= (begin + size))
		throw StreamWriteOverflow();

	std::memcpy(buffer + begin, src, size);
	if ((begin + size) > cursor)
		cursor = (begin + size);
}


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


void StreamReader::ReadRawData(void* dst, SizeType size)
{
	if (dst == nullptr)
		throw InvalidStreamArgument();

	Parse(dst, size);
}

void StreamReader::Parse(void* dst, SizeType size)
{
	if (dst == nullptr)
		throw InvalidStreamArgument();

	if ((maxsize - cursor) < size)
		throw StreamReadUnderflow();

	std::memcpy(dst, buffer + cursor, size);
	cursor += size;
}