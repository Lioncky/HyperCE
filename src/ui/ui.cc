#include "hyperce.h"
#include <Windows.h>
#include <CommCtrl.h>
VOID ui_ShowTrayMenu(HWND);


HMENU CreateAppMenu();
void ui_on_create(HWND hWnd)
{
	DAS("!ui_on_create");

	// init dark mode theme data for menu
	//nt::darkmenu_theme() = Nt::UxOpenThemeData(hWnd, L"Menu");

	// set current system menu
	::SetMenu(hWnd, CreateAppMenu());

}

VOID UI_MainWnd()
{
	HWND hwnd;

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

			}

			// Open Context Menu
			if (LOWORD(lParam) == WM_RBUTTONDOWN) {
				Nt::GetCursorPosU32(&G->pt); // 
				ui_ShowTrayMenu(hwnd);
			}

		}

		else if (msg == WM_CLOSE) {
			G->bMainWnd = FALSE;
			//Nt::ShowWindowU32(hwnd, FALSE);
			nt::exit();
			return 0ll;
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

			auto pid = (HANDLE)Nt::WndToPid(Nt::PosToWnd(&G->pt));
			
			Nt::QueryProcessPath(pid, nt::tmp(), 256);
			
			nt::pset(pid);

			DDS("PID: %d %X HANDLE:%d %s", pid, pid, G->hProcessHandle, nt::tmp());
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
		
		#if 1 // DARK MODE MENU
		else if (G->bDarkMode && msg == WM_UAHDRAWMENUITEM) {
			return _ewndi::ON_WM_UAHDRAWMENUITEM((UAHDRAWMENUITEM*)lParam);
		}

		else if (G->bDarkMode && msg == WM_UAHDRAWMENU) {
			return _ewndi::ON_WM_UAHDRAWMENU(hwnd, (UAHMENU*)lParam);
		}
		#endif

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
	while (::GetMessageW(&msg, nullptr, 0, 0)) {
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
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

VOID UI_MainInit() {

	nt::cpuid(0);

	// SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

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

		// Set up read memory functions
		if (!G->ReadProcessMemoryEx) {

			G->ReadProcessMemoryEx = [](LPCVOID lpBaseAddress, LPVOID lpBuffer, UINT nSize) -> INT {
				return Nt::ReadProc(nt::ph(), (char*)lpBaseAddress, lpBuffer, (SIZE_T)nSize) ? (INT)nSize : (INT)0;
			};
			G->WriteProcessMemoryEx = [](LPCVOID lpBaseAddress, LPVOID lpBuffer, UINT nSize) -> INT {
				return Nt::ProtectWrite(nt::ph(), (char*)lpBaseAddress, lpBuffer, (SIZE_T)nSize) ? (INT)nSize : (INT)0;
			};
			G->QueryVirtualMemoryEx = [](LPCVOID lpBaseAddress, MEMORY_BASIC_INFORMATION* _) -> INT {
				return Nt::NtQuery(nt::ph(), (char*)lpBaseAddress, _);
			};
		}
	}
}

HMENU CreateAppMenu()
{
	HMENU hMenuBar = CreateMenu();

	// ── 文件(F) ──────────────────────────────
	HMENU hFile = CreatePopupMenu();
	AppendMenuW(hFile, MF_STRING, IDM_FILE_NEW, L"新建(&N)\tCtrl+N");
	AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"打开(&O)...\tCtrl+O");
	AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE, L"保存(&S)\tCtrl+S");
	AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE_AS, L"另存为(&A)...");
	AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"退出(&X)\tAlt+F4");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFile, L"文件(&F)");

	// ── 编辑(E) ──────────────────────────────
	HMENU hEdit = CreatePopupMenu();
	AppendMenuW(hEdit, MF_STRING, IDM_EDIT_UNDO, L"撤销(&Z)\tCtrl+Z");
	AppendMenuW(hEdit, MF_STRING, IDM_EDIT_REDO, L"重做(&Y)\tCtrl+Y");
	AppendMenuW(hEdit, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hEdit, MF_STRING, IDM_EDIT_CUT, L"剪切(&T)\tCtrl+X");
	AppendMenuW(hEdit, MF_STRING, IDM_EDIT_COPY, L"复制(&C)\tCtrl+C");
	AppendMenuW(hEdit, MF_STRING, IDM_EDIT_PASTE, L"粘贴(&P)\tCtrl+V");
	AppendMenuW(hEdit, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hEdit, MF_STRING, IDM_EDIT_SELECT_ALL, L"全选(&A)\tCtrl+A");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hEdit, L"编辑(&E)");

	// ── 帮助(H) ──────────────────────────────
	HMENU hHelp = CreatePopupMenu();
	AppendMenuW(hHelp, MF_STRING, IDM_HELP_DOCS, L"文档(&D)\tF1");
	AppendMenuW(hHelp, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hHelp, MF_STRING, IDM_HELP_ABOUT, L"关于(&A)...");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hHelp, L"帮助(&H)");

	return hMenuBar;
}