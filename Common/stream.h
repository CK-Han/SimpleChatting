#pragma once
#include <stdexcept>

/**
	@class StreamBase
	@brief		StreamWriter�� Reader�� ���̽� Ŭ����
	@details	���������� ���Ǵ� ����鿡 ���� ����
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
	@brief		���� ���� ���� Ŭ����
	@details	���� ������ ����� �ϰ������� �ϸ�, ���� ��Ȳ�� ���� �����Ѵ�.
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
	@brief		���� �б� ���� Ŭ����
	@details	���� �б��� ����� �ϰ������� �ϸ�, ���� ��Ȳ�� ���� �����Ѵ�.
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