#include "Framework.h"
#include <iostream>
using namespace std;

void main()
{
	Framework::GetInstance();

	cout << "'u' 입력시 유저 수 확인, 'c' 입력시 커스텀 채널 리스트, 'x' 입력시 서버 종료\n";
	int ch = 0;
	while (ch != 'x')
	{
		ch = getchar();
		if (ch == 'u')
			cout << "유저 수 : " << Framework::GetInstance().DebugUserCount() << '\n';
		else if (ch == 'c')
		{
			auto channels = Framework::GetInstance().DebugCustomChannels();
			cout << "커스텀 채널 리스트 ... 총 " << channels.size() << " 개" << '\n';
			for (auto& name : channels)
			{
				cout << name << '\n';
			}
			cout << "****************************************\n";
		}
	}
	
	cout << "프로그램 종료\n";
}