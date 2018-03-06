#include "Framework.h"
#include <iostream>
using namespace std;

void main()
{
	Framework::GetInstance();

	cout << "'u' �Է½� ���� �� Ȯ��, 'c' �Է½� Ŀ���� ä�� ����Ʈ, 'x' �Է½� ���� ����\n";
	int ch = 0;
	while (ch != 'x')
	{
		ch = getchar();
		if (ch == 'u')
			cout << "���� �� : " << Framework::GetInstance().DebugUserCount() << '\n';
		else if (ch == 'c')
		{
			auto channels = Framework::GetInstance().DebugCustomChannels();
			cout << "Ŀ���� ä�� ����Ʈ ... �� " << channels.size() << " ��" << '\n';
			for (auto& name : channels)
			{
				cout << name << '\n';
			}
			cout << "****************************************\n";
		}
	}
	
	cout << "���α׷� ����\n";
}