#include "DummyHandler.h"
#include <iostream>

using namespace std;

void main()
{
	bool isStarted = false;
	string ip("127.0.0.1");

	while(isStarted == false)
	{
		cout << "아이피 입력 ex : 127.0.0.1 \n-> ";
		cin >> ip;
		isStarted = DummyHandler::GetInstance()->Start(ip);
	}


	cout << "'+' 입력시 100명 추가, '-'입력시 50명 로그아웃, 'x' 입력시 프로그램 종료\n";
	int ch = 0;
	while(ch != 'x')
	{
		ch = getchar();
		if (ch == '+')
			DummyHandler::GetInstance()->AddDummy(DummyHandler::GetInstance()->GetValidSerial(), 100, ip);
		else if(ch == '-')
			DummyHandler::GetInstance()->DummyHandler::GetInstance()->CloseDummy(50);
	}
	 //DummyHandler::GetInstance()->Close();

	cout << "프로그램 종료\n";
}