#include "hyperce.h"
#include <Windows.h>

void ShowTrayMenu(HWND hwnd)
{
	HMENU hMenu = CreatePopupMenu();
	//AppendMenu(hMenu, MF_STRING | (G->bOption1 ?MF_CHECKED : MF_UNCHECKED), ID_MENU_CK1, L"主页"); // 默认勾选
	AppendMenu(hMenu, MF_STRING | (G->bShowConsle ? MF_CHECKED : MF_UNCHECKED), ID_MENU_CK2, L"命令"); // 默认未勾选
	//AppendMenu(hMenu, MF_STRING, ID_MENU_HELLO, L"信息");
	AppendMenu(hMenu, MF_STRING, ID_MENU_CANCEL, L"取消");
	AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenu(hMenu, MF_STRING, ID_MENU_EXIT, L"退出");

	// SetForegroundWindow(hwnd); // 必须调用，否则菜单可能不消失
	TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, G->pt.x, G->pt.y, 0, hwnd, nullptr);
	DestroyMenu(hMenu);
}

int xTray(PVOID)
{
	
	HWND hwnd = Nt::CurrentConsole();
	if (hwnd) 
		Nt::RemoveWndStyle(hwnd, WS_SYSMENU);

	hwnd = Nt::CreateWnd(TRAY_CAPTION, TRAY_CAPTION,
	+[](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_TRAYICON) {

			// L BUTTON
			if (LOWORD(lParam) == WM_LBUTTONDOWN) {
				G->bMainWnd ^= 1;
				Nt::ShowWindowU32(hwnd, G->bMainWnd);
				return 0ll;

				//if (!G->bMainWnd) {
				//	Nt::ShowWindowU32(hwnd, FALSE);
				//}
				//else {
				//	Nt::GetCursorPosU32(&G->pt); // 
				//	Nt::SetWindowPosU32(hwnd, HWND_TOPMOST, G->pt.x-20-800, G->pt.y-50 - 600, 80+800, 20+600, SWP_SHOWWINDOW);
				//}
			}

			// Open Context Menu
			if (LOWORD(lParam) == WM_RBUTTONDOWN) {
				Nt::GetCursorPosU32(&G->pt); // 
				ShowTrayMenu(hwnd);
			}

		}

		else if (msg == WM_CLOSE) {
			G->bMainWnd = FALSE;
			Nt::ShowWindowU32(hwnd, FALSE);
		}

		else if (msg == WM_DROPFILES)
		{
			UINT fileCount = Nt::DragQueryFileS32((HANDLE)wParam, 0xFFFFFFFF, nullptr, 0); // 获取拖入文件数量
			for (UINT i = 0; i < fileCount; ++i)
			{
				wchar_t filePath[MAX_PATH];
				if (Nt::DragQueryFileS32((HANDLE)wParam, i, filePath, MAX_PATH))
				{
					nt::lg("%S", filePath);

				}
			}
				
			Nt::DragFinish((HGLOBAL)wParam);
			return 0ll;
		}
		else if (G->bCapting && msg == WM_LBUTTONUP)
		{
			G->bCapting = FALSE;

			::ReleaseCapture();

			Nt::GetCursorPosU32(&G->pt);

			G->targetId = Nt::WndToPid(Nt::PosToWnd(&G->pt));

			Nt::QueryProcessPath((HANDLE)G->targetId, nt::tmp(), 222);

			nt::lg("%d: %s", G->targetId, nt::tmp());
			//DbgPrint("%s mouse released", nt::ltime());
		}
		else if (msg == WM_NCLBUTTONDOWN) {

			POINT pt;
			pt.y = ((LONG)(lParam) >> 16)+25;
			pt.x = (LONG)(lParam) & 0xFFFF;
			//DbgPrint("%d %d", pt.x, pt.y);

			::ScreenToClient(hwnd, &pt);
			int titleHeight = ::GetSystemMetrics(SM_CYCAPTION);

			// check top-left icon area
			if (pt.x >= 0 && pt.x < 25 && pt.y >= 0 && pt.y < titleHeight)
			{
				::SetCapture(hwnd);
				G->bCapting = TRUE;

				if (!G->cursor) {
					//G->cursora = (LoadCursorW(0, IDC_ARROW));
					G->cursor = ::LoadCursorW(NtCurrentImageBase(), (PWSTR)(11));
				}

				::SetCursor((HCURSOR)G->cursor);
			}
			else 
				goto _DEF;
		}

		//	// 阻止任务栏拖动窗口
		//else if (msg == WM_SYSCOMMAND) {
		//	if ((wParam & 0xFFF0) == SC_MOVE)
		//		return 0ll;
		//}

		else if (msg == WM_COMMAND)
		{
			switch (LOWORD(wParam))
			{
			case ID_MENU_CANCEL:
				break;
			case ID_MENU_HELLO:
				Nt::MsgBoxW(TRAY_CAPTION, TRAY_VERSION, MB_OK | MB_ICONINFORMATION);
				break;
			case ID_MENU_EXIT:
				::PostQuitMessage(0);
				break;
			case ID_MENU_CK1:
				G->bOption1 ^= 1;
				ShowTrayMenu(hwnd);
				break;

			case ID_MENU_CK2:
				G->bShowConsle ^= 1;
				if (G->bShowConsle)
					Nt::ShowConsole(TRUE);
				else 
					Nt::ShowConsole(FALSE);
				ShowTrayMenu(hwnd);
				break;
			}
		}
		else 
		_DEF:
			return ::DefWindowProcW(hwnd, msg, wParam, lParam);

		return 0ll;
	});

	#if 1
	if (nt::darkmode())
		Nt::EnableDarkModeDwm(hwnd);
	
	HANDLE hIco = Nt::LoadIconW(NtCurrentImageBase(), MAKEINTRESOURCEW(1));
	Nt::AddWndStyle(hwnd, WS_SYSMENU);
	Nt::AddWndExStyle(hwnd, WS_EX_ACCEPTFILES);
	Nt::AddWndIcon(hwnd, hIco);
	G->exeIcon = hIco;

	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN); // 
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN); // 

	int x = (screenWidth - WND_W) >> 1;
	int y = (screenHeight - WND_H) >> 1;
	Nt::SetWindowPosU32(hwnd, HWND_TOP, x, y, WND_W, WND_H, SWP_HIDEWINDOW);

	Nt::AddTary(hwnd, *(DWORD*)&hIco | 0x80000000, WM_TRAYICON, TRAY_CAPTION);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
		ntsleep(10);
	}

	// 清理图标
	//if (nid.hIcon) DestroyIcon(nid.hIcon);

	x = 10, y = 2;                   // 距左边 10 像素
	HWND hWnd = G->Shell_TrayWnd ? G->Shell_TrayWnd : G->NotifyIconOverflowWindow;
	RECT rect = { x, y, x + 1200, y + 200 };
	InvalidateRect(hWnd, &rect, TRUE);
	UpdateWindow(hWnd);
	nt::ntTerminateProcess(HPROC, 0);
	return 0;
}

//Nt::UxSetWindowTheme(hwnd, L"DarkMode_Explorer", NULL); // 主窗口
//Nt::UxAllowDarkModeForWindow(hwnd, TRUE);
//Nt::ThemeChangedWnd(hwnd);
#else	
	HMODULE hUthTheme = nt::load("uxtheme");
	void(*fnSetWindowTheme)(HWND, LPCWSTR, HANDLE);
	bool(*fnShouldAppsUseDarkMode)(); // 132
	void(*fnAllowDarkModeForWindow)(HWND, BOOL); // 133
	void(*fnSetPreferredAppMode)(BOOL); // 135
	*(PVOID*)&fnSetWindowTheme = nt::func(hUthTheme, "SetWindowTheme");
	*(PVOID*)&fnShouldAppsUseDarkMode = nt::func(hUthTheme, (PSTR)132);
	*(PVOID*)&fnAllowDarkModeForWindow = nt::func(hUthTheme, (PSTR)133);
	*(PVOID*)&fnSetPreferredAppMode = nt::func(hUthTheme, (PSTR)135);

	fnShouldAppsUseDarkMode();
	fnSetPreferredAppMode(2); // PreferredAppMode::AllowDark ForceDark = 2
	fnSetWindowTheme(hwnd, L"DarkMode_Explorer", NULL); // 主窗口
	fnAllowDarkModeForWindow(hwnd, TRUE);
	Nt::ThemeChangedWnd(hwnd);
#endif