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
			HANDLE hProcessHandle;
			INT (*ReadProcessMemoryEx)(LPCVOID lpBaseAddress, LPVOID lpBuffer, UINT nSize);
			INT (*WriteProcessMemoryEx)(LPCVOID lpBaseAddress, LPVOID lpBuffer, UINT nSize);
			INT (*QueryVirtualMemoryEx)(LPCVOID lpBaseAddress, MEMORY_BASIC_INFORMATION* lpBuffer);
			
			int dpi;
			bool bDarkMode, bOption1, bShowConsle, bMainWnd;
			bool bCovering, bCapting;
			DWORD targetId; POINT pt;
			HANDLE exeIcon, cursor, bakcur;
			HANDLE crHSplit, crVSplit, crSizeAll, crDefault;
			PVOID lpLowPtr;

			HWND ConsoleWnd;
			HWND NotifyIconOverflowWindow, Shell_TrayWnd;


			// 
			// mem_view
			// 
			bool draggingSplitter;
			HWND hHeader;
			HWND hListView;
			HWND hStatusBar;
			HWND hStatusEdit;
			HWND hStatusBtn;
		};
		BYTE ___[0x1000];
	};
	BYTE page2[0x1000];
	WCHAR vw_mem_StateBar[0x180]; CHAR tmps[0x1D00];
	BYTE unused[0x2000];
	BYTE unused2[0x2000];

	inline int get_dpi_mul(int _) { return _ * dpi / 96; }
	inline PHYPER_WMSR as_wmsr() { *(int*)this = 'WMSR'; return (PHYPER_WMSR)this; }
	inline PHYPER_VMRD as_vmrd() { *(int*)this = 'VMRD'; return (PHYPER_VMRD)this; }
}; static_assert(sizeof(HYPER_CE) == 0x8000, "! HYPER_CE");

NX_G HYPER_CE* G; VOID debug_test(); VOID UI_MainWnd(); VOID UI_MainInit();
#endif // 