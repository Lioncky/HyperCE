#ifndef _HYPER_CE_
# define _HYPER_CE_
# include <WinNe.h>
# include <nt/ntzwdef.h>
# include "hyperce_id.h"

struct HYPER_CE
{
	union {
		struct {
			HYPER_CALLER door;
			bool bOption1, bShowConsle, bMainWnd, bCapting;
			DWORD targetId; POINT pt;
			HANDLE exeIcon, cursor, bakcur;
			HANDLE crHSplit, crVSplit, crSizeAll, crDefault;
			PVOID lpLowPtr;

			HWND ConsoleWnd;
			HWND NotifyIconOverflowWindow, Shell_TrayWnd;
		};
		BYTE ___[0x1000];
	};
	BYTE page[0x1000];

	inline PHYPER_WMSR as_wmsr() { *(int*)this = 'WMSR'; return (PHYPER_WMSR)this; }
	inline PHYPER_VMRD as_vmrd() { *(int*)this = 'VMRD'; return (PHYPER_VMRD)this; }
};
NX_G HYPER_CE* G; void debug_test();
#endif // 