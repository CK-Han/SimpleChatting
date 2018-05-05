#pragma once
#include <stdexcept>

/**
	@class StreamBase
	@brief		StreamWriter와 Reader의 베이스 클래스
	@details	공통적으로 사용되는 내용들에 대한 정의
	@author		cgHan
	@date		2018/05/05
	@version	0.0.1
*/
class StreamBase
{
public:
	using SizeType = unsigned int;

	class InvalidStreamArgument
		: public std::invalid_argument
	{
	public:
		InvalidStreamArgument() : std::invalid_argument("InvalidStreamArgument") {}
	};
};


/**
	@class StreamWriter
	@brief		버퍼 쓰기 지원 클래스
	@details	버퍼 쓰기의 방식을 일관적으로 하며, 예외 상황에 대해 대응한다.
	@author		cgHan
	@date		2018/05/05
	@version	0.0.1
*/
class StreamWriter
	: public StreamBase
{
public:
	class StreamWriteOverflow
		: public std::overflow_error
	{
	public:
		StreamWriteOverflow() : std::overflow_error("StreamWriteOverflow") {}
	};

public:
	StreamWriter(void* buf, SizeType maxsize);
	StreamWriter(const StreamWriter&) = delete;

	StreamWriter& operator<<(bool );
	StreamWriter& operator<<(char );
	StreamWriter& operator<<(unsigned char );
	StreamWriter& operator<<(short );
	StreamWriter& operator<<(unsigned short );
	StreamWriter& operator<<(int );
	StreamWriter& operator<<(unsigned int );
	StreamWriter& operator<<(float );
	StreamWriter& operator<<(double );
	StreamWriter& operator<<(long long );
	StreamWriter& operator<<(unsigned long long );
	
	void WriteRawData(const void* src, SizeType size);
	void OverwriteRawData(SizeType begin, const void* src, SizeType size);

	SizeType GetStreamSize() const { return cursor; }
	const void* GetBuffer() const { return buffer; }

private:
	void Append(const void* src, SizeType size);

private:
	char*				buffer;
	SizeType			maxsize;
	SizeType			cursor;
};



/**
	@class StreamReader
	@brief		버퍼 읽기 지원 클래스
	@details	버퍼 읽기의 방식을 일관적으로 하며, 예외 상황에 대해 대응한다.
	@author		cgHan
	@date		2018/05/05
	@version	0.0.1
*/
class StreamReader
	: public StreamBase
{
public:
	class StreamReadUnderflow
		: public std::underflow_error
	{
	public:
		StreamReadUnderflow() : std::underflow_error("StreamReadUnderflow") {}
	};

public:
	StreamReader(const void* buf, SizeType maxsize);
	StreamReader(const StreamReader&) = delete;

	StreamReader& operator>>(bool& );
	StreamReader& operator>>(char& );
	StreamReader& operator>>(unsigned char& );
	StreamReader& operator>>(short& );
	StreamReader& operator>>(unsigned short& );
	StreamReader& operator>>(int& );
	StreamReader& operator>>(unsigned int& );
	StreamReader& operator>>(float& );
	StreamReader& operator>>(double& );
	StreamReader& operator>>(long long& );
	StreamReader& operator>>(unsigned long long& );

	void ReadRawData(void* dst, SizeType size);

	const void* GetBuffer() const { return buffer; }

private:
	void Parse(void* dst, SizeType size);

private:
	const char*			buffer;
	SizeType			maxsize;
	SizeType			cursor;
};