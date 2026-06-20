#ifndef _HYPER_CE_
# define _HYPER_CE_
# include <WinNe.h>
# include <nt/ntzwdef.h>
# include "hyperce_id.h"

struct HYPER_CE
{
	union { BYTE ___[0x1000]; struct {
		HYPER_CALLER door; 
		HANDLE hProcessHandle;
		INT (*ReadProcessMemoryEx)(LPCVOID lpBaseAddress, LPVOID lpBuffer, UINT nSize);
		INT (*WriteProcessMemoryEx)(LPCVOID lpBaseAddress, LPVOID lpBuffer, UINT nSize);
		INT (*QueryVirtualMemoryEx)(LPCVOID lpBaseAddress, MEMORY_BASIC_INFORMATION* lpBuffer);
			
		int dpi;
		bool bDarkMode, bOption1, bShowConsle, bMainWnd;
		bool bCovering, bCapting, bHexMode;
		POINT pt;
		HANDLE exeIcon, cursor, bakcur;
		HANDLE crHSplit, crVSplit, crSizeAll, crDefault;
		PVOID lpLowPtr;

		HWND ConsoleWnd;
		HWND NotifyIconOverflowWindow, Shell_TrayWnd;

		// 
		// mem_view
		// 
		bool draggingSplitter;
		HWND 
			hHeader,
			hListView,
			hStatusBar,
			hStatusEdit,
			hStatusBtn;

		//////////////////////////////////////////////////////////////////////////
		//																										//
		// Vm WalkThrough																		//
		//																										//
		//////////////////////////////////////////////////////////////////////////

		// 
		// eprocess struct members
		// 
		ULONG OsDirectoryTableBase, OsUniqueProcessId, OsImageFileNameOffset, OsActiveProcessLinks, OsVadRoot;
			
		ULONGLONG SystemProcessEprocess, MemoryKernelBase; PVOID PsLoadedModuleListPtr;
		ULONGLONG MemoryKernelDirbase, MemoryKernelEntry;
		UINT64 TargetVadRoot, TargetPid, TargetCR3;
	};};
	union { BYTE ____[0x1000]; struct {
		WCHAR vw_mem_StateBar[0x180];
		CHAR tmps[1];
	};};

	inline PHYPER_WMSR as_wmsr() { *(int*)this = 'WMSR'; return (PHYPER_WMSR)this; }
	inline PHYPER_VMRD as_vmrd() { *(int*)this = 'VMRD'; return (PHYPER_VMRD)this; }
}; static_assert(sizeof(HYPER_CE) == 0x2000, "! HYPER_CE");

NX_G HYPER_CE* G; VOID debug_test(); VOID UI_MainWnd(); VOID UI_MainInit();
#endif // 