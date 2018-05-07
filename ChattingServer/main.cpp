#include "Framework.h"
#include <iostream>
#include <iomanip>

using namespace std;

void main()
{
	//initialize
	if (Framework::GetInstance().IsShutDown())
		return;

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
			int newLineCheck = 0;
			for (const auto& name : vec)
			{
				cout << std::setw(Packet_Base::MAX_CHANNELNAME_SIZE + 2) << name;
				++newLineCheck;
				if (newLineCheck == 3)
				{
					newLineCheck = 0;
					cout << endl;
				}
			}

			cout << "\n..................................\n\n";
			cout << comandExplain;
		}
			break;

		} //switch end
	}
	
	cout << "server ended\n";
}