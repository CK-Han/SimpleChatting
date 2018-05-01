#include "Framework.h"
#include <iostream>
using namespace std;

void main()
{
	//initialize
	Framework::GetInstance();

	string comandExplain = "input 'x' to close server, 'c' -> user count\n"
							"'h' -> custom channel count, 'H' -> custom channel list\n";
	cout << comandExplain;
	int ch = 0;
	while ((ch = getchar()) != 'x')
	{
		switch (ch)
		{
		case 'c':
			cout << "user count : " << Framework::GetInstance().DebugUserCount(false) << endl;
			break;
		case 'h':
		{
			auto vec = Framework::GetInstance().DebugCustomChannels(false);
			cout << "custom channel count : " << vec.size() << endl;
		}	
			break;
		case 'H':
		{
			auto vec = Framework::GetInstance().DebugCustomChannels(false);
			cout << "custom channel list... " << endl;
			for (const auto& name : vec)
			{
				cout << name << '\t' << '\t';
			}

			cout << "..................................\n";
			cout << comandExplain;
		}
			break;
		}
	}
	
	cout << "server ended\n";
}