#include "DummyHandler.h"
#include <iostream>

using namespace std;

void main()
{
	bool isStarted = false;
	string ip("127.0.0.1");

	while(isStarted == false)
	{
		cout << "������ �Է� ex : 127.0.0.1 \n-> ";
		cin >> ip;
		isStarted = DummyHandler::GetInstance()->Start(ip);
	}


	cout << "'+' �Է½� 100�� �߰�, '-'�Է½� 50�� �α׾ƿ�, 'x' �Է½� ���α׷� ����\n";
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

	cout << "���α׷� ����\n";
}