#ifndef _HYPER_CE_
# define _HYPER_CE_
# include <WinNe.h>
# include "hyperce_id.h"

struct HYPER_CE
{
	bool bOption1, bShowConsle, bMainWnd, bCapting;
	DWORD targetId; POINT pt;
	HANDLE exeIcon, cursor;
	
	HWND ConsoleWnd;
	HWND NotifyIconOverflowWindow, Shell_TrayWnd;
};
NX_G HYPER_CE* G;
#endif // 