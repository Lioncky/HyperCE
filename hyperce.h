#ifndef _HYPER_CE_
# define _HYPER_CE_
# include <WinNe.h>
# include <nt/ntzwdef.h>
# include "hyperce_id.h"

struct HYPER_CE
{
	HYPER_CALLER door;
	BYTE _buffer[0x1000 - 4];
	BYTE _1000[0x1000];

	bool bOption1, bShowConsle, bMainWnd, bCapting;
	DWORD targetId; POINT pt;
	HANDLE exeIcon, cursor;
	PVOID lpLowPtr;

	HWND ConsoleWnd;
	HWND NotifyIconOverflowWindow, Shell_TrayWnd;

	inline PHYPER_WMSR as_wmsr() { return (PHYPER_WMSR)this; }
};
NX_G HYPER_CE* G;
#endif // 