#pragma once
#include <stdexcept>

/**
	@class StreamBase
	@brief		StreamWriter와 StreamReader의 베이스 클래스
	@details	공통적으로 사용되는 내용들에 대한 정의
				스트림 크기 별칭 및 공용 예외 클래스 정의
*/
class StreamBase
{
public:
	using SizeType = size_t;

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
	@details	버퍼 쓰기의 방식을 일관적으로 하며, 예외 상황에 대응한다.
				기본적으로 operator<< 를 지원하며 raw data를 쓰기위한 인터페이스도 제공한다.
				
	@todo		추상화 방법에 대해 생각해본다. 또한 그것이 필요할지 생각해본다.
				가령, 함수들을 가상화하고 ex) virtual operator<<(std::string);
				PacketStream 클래스 제작, 오버라이딩 하는 방식으로
				
				write raw data의 사용보다는 오버로딩 타입을 늘리는 방향을 고려해야한다.
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
	@details	버퍼 읽기의 방식을 일관적으로 하며, 예외 상황에 대응한다.
				기본적으로 operator>>를 지원하며 raw data를 읽기 위한 인터페이스도 제공한다.

	@todo		StreamWriter와 같이 추상화 방법에 대해 생각해본다. 또한 그것이 필요할지 생각해본다.
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