// vm_mem.h
#ifndef _VM_MEM_H_
# define _VM_MEM_H_

#include <windows.h>
#include <commctrl.h>

// ── 窗口尺寸常量 ────────────────────────────────────────────────
#define WIN_WIDTH        1320
#define WIN_HEIGHT       960
#define SPLITTER_HEIGHT  5
#define SCROLL_MAX_RANGE 10000

// ── 坐标辅助宏 ──────────────────────────────────────────────────
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

// ── 核心度量衡结构体 ────────────────────────────────────────────
struct HexViewMetrics {
	int      charSize;       // 单字符宽度
	int      byteSizeNoChar; // "XX " 格宽
	int      addressWidth;   // 左侧地址栏总宽
	int      rowHeight;      // 行高
	int      bytesPerRow;    // 每行字节数
	int      upperHeight;    // 每行字节数
	uintptr_t memoryMinAddress		,// = 0x0000000000000000;
	 memoryMaxAddress	,// = 0x00007FFFFFFFFFFF;
	 pageBaseAddress			,// = 0x100000000;
	selectionStart,
	selectionEnd,
	editingAddress;

	bool      hasSelection = false;
	bool      isEditing = false;
	bool      clickedAscii = false;
	bool      isUpAera = false;



	WNDPROC OldEditProc = NULL;
	HWND   hGhostEdit = NULL;
	HWND   hHexView = NULL;
	HFONT hFont = NULL;

	bool draggingSplitter;
	HWND
		hHeader,
		hListView,
		hStatusBar,
		hStatusEdit,
		hStatusBtn;

	WCHAR vw_mem_StateBar[0x180];
	CHAR tmps[1];
};

// ── 地址 滚动条位置换算 ─────────────────────────────────────────
int       AddrToPos(uintptr_t addr);
uintptr_t PosToAddr(int pos);

// ── 坐标 地址换算 ───────────────────────────────────────────────
uintptr_t GetAddressFromMouse(int mx, int my, uintptr_t pageBase);
RECT      GetRectFromAddress(uintptr_t addr, RECT rcPane);

// ── 内存读写（由外部实现替换） ───────────────────────────────────
BOOL MockReadProcessMemory(uintptr_t addr, void* buf, size_t size);
BOOL MockWriteProcessMemory(uintptr_t addr, void* buf, size_t size);

// ── UI 入口与布局 ────────────────────────────────────────────────
int  ui_show_mmview(void*);
void LayoutControls(HWND hwnd);
void DrawBottomPane(HDC hdc, RECT rcPane);
void ActivateGhostEdit(HWND hParent, uintptr_t addr, RECT rcPane);
void SetStateText(const char* fmt, ...);

// ── 窗口过程 ─────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK GhostEditSubclassProc(HWND, UINT, WPARAM, LPARAM);


#endif // _VM_MEM_H_