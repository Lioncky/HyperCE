#include "hyperce.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <ShellScalingApi.h>
VOID ui_MainInit();
VOID ui_ShowTrayMenu(HWND);
VOID ui_on_create(HWND hWnd) {
	DAS("!ui_on_create");

}

VOID UI_MainWnd()
{
	//SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	HWND hwnd;
	ui_MainInit();

	hwnd = Nt::CurrentConsole();
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
				ui_ShowTrayMenu(hwnd);
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
			::SetCursor((HCURSOR)G->crDefault);

			Nt::GetCursorPosU32(&G->pt);

			G->targetId = Nt::WndToPid(Nt::PosToWnd(&G->pt));

			Nt::QueryProcessPath((HANDLE)G->targetId, nt::tmp(), 256);

			nt::lg("%d: %s", G->targetId, nt::tmp());
			//DbgPrint("%s mouse released", nt::ltime());
		}
		else if (msg == WM_NCLBUTTONDOWN) {

			// check top-left icon area
			if (G->bCovering) {
				G->bCovering = FALSE;

				::SetCapture(hwnd);
				G->bCapting = TRUE;
				
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

		else if (msg == WM_MOUSEMOVE && G->bCovering) {
			G->bCovering = FALSE;
			::SetCursor((HCURSOR)G->crDefault);
		}
		else if (msg == WM_NCMOUSEMOVE) {

			//POINT pt {
			//	(LONG)(lParam) & 0xFFFF,
			//	((LONG)(lParam) >> 16) + 25
			//};
			
			//DAS("%d %d %u", pt.x, pt.y, (UINT)wParam);

			if (wParam == HTSYSMENU) {
				::SetCursor((HCURSOR)G->cursor);
				G->bCovering = 1;
			}
			else if (G->bCovering) {
				G->bCovering = 0;
				::SetCursor((HCURSOR)G->crDefault);
			}
		}

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
				ui_ShowTrayMenu(hwnd);
				break;

			case ID_MENU_CK2:
				G->bShowConsle ^= 1;
				if (G->bShowConsle)
					Nt::ShowConsole(TRUE);
				else 
					Nt::ShowConsole(FALSE);
				ui_ShowTrayMenu(hwnd);
				break;
			}
		}
		else if (msg == WM_CREATE) {
			ui_on_create(hwnd);
		}

		else 
		_DEF:
			return ::DefWindowProcW(hwnd, msg, wParam, lParam);

		return 0ll;
	});

	if (nt::darkmode())
		Nt::EnableDarkModeDwm(hwnd);
	
	Nt::AddWndStyle(hwnd, WS_SYSMENU);
	Nt::AddWndExStyle(hwnd, WS_EX_ACCEPTFILES);
	Nt::AddWndIcon(hwnd, G->exeIcon);

	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN); // 
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN); // 

	int x = (screenWidth - WND_W) >> 1;
	int y = (screenHeight - WND_H) >> 1;
	Nt::SetWindowPosU32(hwnd, HWND_TOP, x, y, WND_W, WND_H, SWP_SHOWWINDOW);

	//Nt::AddTary(hwnd, *(DWORD*)&hIco | 0x80000000, WM_TRAYICON, TRAY_CAPTION);

	MSG msg;
	while (::GetMessageW(&msg, nullptr, 0, 0))
	{
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
		ntsleep(10);
	}

}

VOID ui_MainInit() {

	// Very Thanks to ResourceHacker
	// https://docwiki.embarcadero.com/Libraries/Alexandria/en/Vcl.Controls.TCursor?v=23.1
	if (!G->exeIcon) {
		G->exeIcon = ::LoadIconW(NtCurrentImageBase(), MAKEINTRESOURCEW(1));
		G->cursor = ::LoadCursorW(NtCurrentImageBase(), MAKEINTRESOURCEW(5));
		G->crSizeAll = ::LoadCursorW(NtCurrentImageBase(), MAKEINTRESOURCEW(22));
		G->crHSplit = ::LoadCursorW(NtCurrentImageBase(), MAKEINTRESOURCEW(14));
		G->crVSplit = ::LoadCursorW(NtCurrentImageBase(), MAKEINTRESOURCEW(15));
		G->crDefault = ::LoadCursorW(NULL, IDC_ARROW);
		G->dpi = ::GetDpiForSystem();
	}
}

void ui_ShowTrayMenu(HWND hwnd)
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
