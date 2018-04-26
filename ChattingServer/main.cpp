#include "Framework.h"
#include <iostream>
using namespace std;

void main()
{
	//initialize
	Framework::GetInstance();

	cout << "input 'x' to close server\n";
	int ch = 0;
	while ((ch = getchar()) != 'x')
	{
	}
	
	cout << "server ended\n";
}