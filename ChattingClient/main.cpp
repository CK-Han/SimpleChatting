#include "Socket.h"
#include "Framework.h"

HINSTANCE gInstance;
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/
	, LPSTR /*lpszCmdParam*/, int nCmdShow)
{
	HWND hWnd;
	MSG msg;
	WNDCLASS WndClass;
	LPCTSTR lpszClass = TEXT("ChattingClient");
	gInstance = hInstance;

	WndClass.cbClsExtra = 0;
	WndClass.cbWndExtra = 0;
	WndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	WndClass.hInstance = hInstance;
	WndClass.lpfnWndProc = WndProc;
	WndClass.lpszClassName = lpszClass;
	WndClass.lpszMenuName = NULL;
	WndClass.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClass(&WndClass);

	hWnd = CreateWindow(lpszClass, lpszClass, WS_CAPTION | WS_BORDER | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, (HMENU)NULL, hInstance, NULL);
	ShowWindow(hWnd, nCmdShow);

	while (GetMessage(&msg, NULL, 0, 0)) 
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static HWND editIP, buttonIP;
	static const LONG_PTR ID_SUBMITIP = 1000;
	static char ipAddress[16];

	switch (msg) 
	{
	case WM_CREATE:
		editIP = ::CreateWindow("edit", "127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER |
			ES_AUTOHSCROLL, 10, 10, 200, 25, hWnd, 0, gInstance, NULL);
		buttonIP = ::CreateWindow("button", "Submit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			220, 10, 50, 25, hWnd, (HMENU)ID_SUBMITIP, gInstance, NULL);
		break;
		
	case Socket::WM_SOCKET:
		Framework::GetInstance()->ProcessWindowMessage(msg, wParam, lParam);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_SUBMITIP:
			::GetWindowText(editIP, ipAddress, sizeof(ipAddress));
			bool isConnect = Framework::GetInstance()->GetUserSocket().Initialize(hWnd, ipAddress);
			if(isConnect)
			{
				bool isStarted = Framework::GetInstance()->Run(hWnd, gInstance);
				if (isStarted == false)
					PostQuitMessage(0);

				::DestroyWindow(editIP);
				::DestroyWindow(buttonIP);
			}
			break;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	
	return(DefWindowProc(hWnd, msg, wParam, lParam));
}
