#pragma warning(disable:26454) // 4309 关闭截断警告
#include "hyperce.h"  // name=MemoryViewer.cpp
#include <windows.h>
#include <commctrl.h> 
#include "vw_mem.h"

#define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))
#define WIN_WIDTH  1350
#define WIN_HEIGHT 1150
#define SPLITTER_HEIGHT 5

// ============================================================================
// 一、 核心度量衡与状态结构体 (仿 CE hexviewunit 测绘架构)
// ============================================================================
struct HexViewMetrics {
	int charSize;            // 单个字符宽度 (对应 CE: charsize)
	int byteSizeNoChar;      // 十六进制格子宽度 (对应 CE: byteSizeWithoutChar，由"XX "撑开)
	int addressWidth;        // 左侧地址栏总宽度 (对应 CE: addresswidthdefault)
	int rowHeight;           // 行高 (对应 CE: textheight)
	int bytesPerRow;    // 每行固定的字节数

	// 选区管理：直接使用绝对内存地址锚定，滚动时选区不穿帮
	uintptr_t selectionStart = 0;
	uintptr_t selectionEnd = 0;
	bool hasSelection = false;

	// 编辑器管理：幽灵输入框专属状态锁
	uintptr_t editingAddress = 0; // 当前正在编辑的绝对内存地址 (0表示未在编辑)
	bool isEditing = false;       // 是否处于激活编辑状态

	HFONT hFont = NULL;           // 等宽字体句柄
} g_Metric{

140,25,12,200,32,32,
};
static RECT rcBottomPane = { 0, 0, 800, 400 };
LRESULT CALLBACK GhostEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
uintptr_t GetAddressFromMouse(int mx, int my, uintptr_t pageBaseAddress);
void ActivateGhostEdit(HWND hWndParent, uintptr_t targetAddress, RECT rcPaneClient);
// 全局基础变量
HWND g_hGhostEdit = NULL;              // 幽灵输入框句柄
WNDPROC g_OldEditProc = NULL;          // 存储系统默认 Edit 的窗口过程
uintptr_t g_pageBaseAddress = 0x7FFF1000; // 模拟当前滚动条滚到的内存基址

int upperHeight = 800; // 可调整

const char* assemblyRows[] = {
	"00401000  mov eax, [ebp+8]",
	"00401004  add eax, 1234h",
	"00401009  mov [edx], eax",
	"0040100C  cmp eax, edx",
	"0040100E  jne 0040101A",
	"00401010  push ebx",
	"00401011  call 00402000",
	"00401016  pop ecx",
	"00401017  nop",
	"00401018  ret"
};

// ============================================================================
// 二、 游戏数据交互层 (实际开发中替换为标准驱动或 Read/WriteProcessMemory)
// ============================================================================
void MockReadProcessMemory(uintptr_t address, unsigned char* buffer, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		// 通过地址生成伪数据，方便测试观察
		buffer[i] = (unsigned char)((address + i) ^ 0x55);
	}
}

void on_selected_ListView(int sel) {

	SetStateText("选中：第 %d 行 %s", sel, "");
	//SetStateText("选中：第 %d 行 %s", sel, sel > 9 ? "" : assemblyRows[sel]);

}
int ui_show_mmview(void*) {

	int x, y, dpi = GetDpiForSystem();
	int w = WIN_WIDTH * dpi / 96;
	int h = WIN_HEIGHT * dpi / 96;
	x = (::GetSystemMetrics(SM_CXSCREEN) - w) / 2;
	y = (::GetSystemMetrics(SM_CYSCREEN) - h) / 2;

	HWND hwnd = Nt::CreateWnd(MMVIEW_CAPTION, MMVIEW_CAPTION, WndProc);
	Nt::SetWindowPosU32(hwnd, 0, x, y, w, h,SWP_SHOWWINDOW);

	Nt::AddWndStyle(hwnd, WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
	Nt::AddWndIcon(hwnd, G->exeIcon);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg); DispatchMessage(&msg);
	}
	return 0;
}


void LayoutControls(HWND hwnd);
void DrawBottomPane(HDC hdc, RECT rcClient);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static RECT rcClient;
	switch (msg) {
	case WM_CREATE: {
		Nt::EnableDarkModeDwm(hwnd);
		//nt::AddWndStyle(hwnd, WS_VSCROLL); // 添加垂直滚动条样式
		{
			// 1. 初始化度量参数 (使用 Consolas 字体强制测绘)
			HDC hdc = GetDC(hwnd);
			
			g_Metric.hFont = CreateFont(G->get_dpi_mul(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
			HFONT hOldFont = (HFONT)SelectObject(hdc, g_Metric.hFont);

			SIZE sizeChar, sizeByte;
			GetTextExtentPoint32(hdc, L"X", 1, &sizeChar);
			GetTextExtentPoint32(hdc, L"XX ", 3, &sizeByte);

			g_Metric.charSize = sizeChar.cx;
			g_Metric.byteSizeNoChar = sizeByte.cx;
			g_Metric.addressWidth = sizeChar.cx * 16; // 模拟8位地址位宽
			g_Metric.rowHeight = sizeChar.cy + 4;    // 动态行高加补空隙

			SelectObject(hdc, hOldFont);
			ReleaseDC(hwnd, hdc);

			// 2. 动态塑造幽灵 Edit 演员
			g_hGhostEdit = ::CreateWindowExW(
				WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, L"EDIT", L"",
				WS_CHILD | ES_UPPERCASE | ES_AUTOHSCROLL,
				0, 0, 0, 0, hwnd, (HMENU)1002, ((LPCREATESTRUCT)lParam)->hInstance, NULL
			);
			LONG_PTR style = GetWindowLongPtr(g_hGhostEdit, GWL_STYLE);
			style &= ~WS_BORDER;  // 移除边框
			SetWindowLongPtr(g_hGhostEdit, GWL_STYLE, style);


			::SendMessage(g_hGhostEdit, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);

			// 3. 强行子类化，接管原生 Edit 控制权
			g_OldEditProc = (WNDPROC)::SetWindowLongPtr(g_hGhostEdit, GWLP_WNDPROC, (LONG_PTR)GhostEditSubclassProc);

		}
		GetClientRect(hwnd, &rcClient);

		G->hListView = CreateWindowExW(
			WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
			WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA,
			0, 0, rcClient.right, upperHeight,
			hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);

		Nt::UxSetWindowTheme(G->hListView, L"DarkMode_Explorer", NULL);
		Nt::UxSetWindowTheme(G->hHeader, L"ItemsView", NULL);
		G->hHeader = ListView_GetHeader(G->hListView);

		{
			// 2. 设定理想行高（比如未缩放前期望是 22 像素）
			
			// 3. 创建一个空的 ImageList。宽度设为 1（尽量小），高度设为缩放后的行高
			HIMAGELIST hEmptyIL = ImageList_Create(1, G->get_dpi_mul(22), ILC_COLOR32, 1, 1);

			// 4. 将其绑定到 ListView 上
			ListView_SetImageList(G->hListView, hEmptyIL, LVSIL_SMALL);
		}

		// 1. 动态计算高度：把原本期望的 20 像素，根据当前的 DPI 进行等比例放大
	
		// 2. 创建一个加粗字体
		HFONT hHeaderFont = CreateFont(
			G->get_dpi_mul(22), 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei"
		);

		// 3. 仅把加粗字体设置给 Header 控件
		SendMessage(G->hHeader, WM_SETFONT, (WPARAM)hHeaderFont, TRUE);

		COLORREF bg = RGB(0x20, 0x20, 0x20);
		ListView_SetBkColor(G->hListView, bg);
		ListView_SetTextBkColor(G->hListView, bg);
		ListView_SetTextColor(G->hListView, RGB(255, 255, 255));

		SetWindowSubclass(G->hListView, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT {
			if (uMsg == WM_NOTIFY){

				if (LPNMHDR(lParam)->code == NM_CUSTOMDRAW) {
					auto* nmcd = (LPNMCUSTOMDRAW)(lParam);
					//SetTextColor(nmcd->hdc, RGB(255, 255, 255));
					//SetBkColor(nmcd->hdc, RGB(32, 32, 32));
					switch (nmcd->dwDrawStage) {
					case CDDS_PREPAINT:
						return CDRF_NOTIFYITEMDRAW;

					case CDDS_ITEMPREPAINT: 
						SetTextColor(nmcd->hdc, RGB(255, 255, 255));
						SetBkColor(nmcd->hdc, RGB(32, 32, 32));
						//SetBkMode(nmcd->hdc, TRANSPARENT);
						return CDRF_DODEFAULT;
					}
					return CDRF_DODEFAULT;
				}
			}
			return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}, 0, 0);

		// 列表美化
		ListView_SetExtendedListViewStyle(G->hListView, LVS_EX_FULLROWSELECT);

		//Nt::UxSetWindowTheme(hListView, L"", L"");

		// 两列: 地址、指令
		LVCOLUMN lvc = { 0 };
		lvc.mask = LVCF_WIDTH | LVCF_TEXT;
		lvc.cx = 580;
		lvc.pszText = (LPWSTR)L"地址 Address";
		ListView_InsertColumn(G->hListView, 0, &lvc);
		lvc.cx = 380;
		lvc.pszText = (LPWSTR)L"字节 Bytes";
		ListView_InsertColumn(G->hListView, 1, &lvc);
		lvc.cx = 360;
		lvc.pszText = (LPWSTR)L"指令 Asm";
		ListView_InsertColumn(G->hListView, 2, &lvc);
		lvc.cx = 1020;
		lvc.pszText = (LPWSTR)L"注释 Note";
		ListView_InsertColumn(G->hListView, 3, &lvc);

		ListView_SetItemCount(G->hListView, 1000000);
		ListView_SetItemState(G->hListView, -1, 0, LVIS_SELECTED);  // 清除所有选中
		ListView_EnsureVisible(G->hListView, 500010, FALSE);  // 滚动到目标项
		ListView_SetItemState(G->hListView, 500000, LVNI_SELECTED | LVNI_FOCUSED, LVNI_SELECTED | LVNI_FOCUSED);

		SetFocus(G->hListView);


		// 加10行示例
		if (0) for (int i = 0; i < (int)10; ++i) {
			LVITEMW lvi = {};
			lvi.mask = LVIF_TEXT;
			static wchar_t addr[32], asmcode[68];
			nt::tow(asmcode, assemblyRows[i]);

			lvi.iItem = i;

			nt::tow(addr, assemblyRows[i]); addr[8] = 0;
			lvi.pszText = (LPWSTR)addr;
			ListView_InsertItem(G->hListView, &lvi);
			ListView_SetItemText(G->hListView, i, 2, asmcode + 9); // skip address+2空格
		}

		// 创建标准状态栏 - 它会自动贴在底部，从左下角开始
		G->hStatusBar = CreateWindowEx(
			0, STATUSCLASSNAME, NULL,
			WS_CHILD | WS_VISIBLE, // | SBARS_SIZEGRIP
			0, 0, 0, 0,
			hwnd, NULL, NtCurrentImageBase(), NULL);

			
		//nt::UxClearWindowTheme(G->hStatusBar);
		//SetWindowPos(G->hStatusBar, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
		//int borders[3];
		//SendMessage(G->hStatusBar, SB_GETBORDERS, 0, (LPARAM)&borders);
		//borders[1] = 1;
		//borders[2] = 1;
		//SendMessage(G->hStatusBar, WM_USER + 5, 0, (LPARAM)&borders);

		//nt::GetWndStyle(G->hStatusBar);
		//nt::RemoveWndStyle(G->hStatusBar, WS_BORDER | WS_THICKFRAME);  // 移除边框)

		//Nt::UxSetWindowDarkMode(G->hStatusBar);
		//Nt::ThemeChangedWnd(G->hStatusBar);
		//InvalidateRect(G->hStatusBar, NULL, TRUE);
		//UpdateWindow(G->hStatusBar);

		// 设置状态栏分区
		//int parts[] = { 350, -1 };
		//SendMessage(G->hStatusBar, SB_SETPARTS, 2 | SBT_OWNERDRAW, (LPARAM)parts);
		
		SendMessageW(G->hStatusBar, SB_SETTEXT, 0 | SBT_OWNERDRAW, (LPARAM)L"就绪");
		break;
	}
	case WM_DRAWITEM:
	{
		auto* lpDrawItem = (LPDRAWITEMSTRUCT)lParam;

		// Verify the message is coming from your status bar
		if (lpDrawItem->hwndItem == G->hStatusBar)
		{
			HDC hdc = lpDrawItem->hDC;
			RECT rect = lpDrawItem->rcItem;

			// 0. 解除 DC 的默认剪裁区域限制，允许向窗格外部绘制
			SelectClipRgn(hdc, NULL);

			rect.left -= 2;
			rect.top -= 2;
			rect.right += 2;
			rect.bottom += 2;

			// 1. Draw your dark background
			FillRect(hdc, &rect, (HBRUSH)nt::darkbrush());

			// 2. Configure text styling
			SetBkMode(hdc, TRANSPARENT);
			SetBkColor(hdc, RGB(32, 32, 32));
			SetTextColor(hdc, RGB(255, 255, 255)); // Light text

			// Adjust text margins slightly so it isn't glued to the left edge
			rect.left += 5;
			DrawText(hdc, (wchar_t*)lpDrawItem->itemData, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

			return TRUE;
		}
		break;
	}
	case WM_NOTIFY:
	{
		auto* hdr = (LPNMHDR)lParam;

		if (hdr->hwndFrom == G->hListView)
		{

			// 监听列表视图的通知消息

			if (hdr->code == LVN_ITEMCHANGED) {

				// 列表框选项被改变
				on_selected_ListView(((LPNMLISTVIEW)lParam)->iItem);

				InvalidateRect(hwnd, NULL, TRUE);
			}

			else if (hdr->code == LVN_GETDISPINFO) {
				NMLVDISPINFO* pdi = (NMLVDISPINFO*)lParam;

				if (pdi->item.mask & LVIF_TEXT)
				{
					// 根据索引返回文本
					switch (pdi->item.iSubItem)
					{
					case 0:
						pdi->item.cchTextMax;
						wsprintf(pdi->item.pszText, L"Item %d", pdi->item.iItem);
						break;
					case 1:
						wsprintf(pdi->item.pszText, L"Value %d", pdi->item.iItem * 2);
						break;
					}
				}

				if (pdi->item.mask & LVIF_IMAGE)
				{
					pdi->item.iImage = pdi->item.iItem % 10;  // 图片索引
				}

				return TRUE;
			}
			else if (hdr->code == NM_CUSTOMDRAW)
			{
				auto* lv = (LPNMLVCUSTOMDRAW)lParam;

				switch (lv->nmcd.dwDrawStage)
				{
				case CDDS_PREPAINT:
					return CDRF_NOTIFYITEMDRAW;

				case CDDS_ITEMPREPAINT:
					return CDRF_NOTIFYSUBITEMDRAW;

				case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
				{
					int row = (int)lv->nmcd.dwItemSpec;
					int col = lv->iSubItem;

					WCHAR text[128];

					ListView_GetItemText(
						G->hListView,
						row,
						col,
						text,
						128
					);

					// 判断是不是数字
					bool isNumber = true;

					for (int i = 0; text[i]; i++)
					{
						if (!nt::iswdigit(text[i]))
						{
							isNumber = false;
							break;
						}
					}

					if (isNumber)
					{
						lv->clrText = RGB(0, 255, 0); // 绿色
					}
					else
					{
						lv->clrText = RGB(255, 255, 255);
					}

					lv->clrTextBk = RGB(0x20, 0x20, 0x20);

					int subItem = lv->iSubItem;

					// 判断如果是第 2 列（注意：你的代码里插入的索引是 2）
					if (subItem == 2)
					{
						static HFONT hFontBold;

						if (!hFontBold) {

							hFontBold = CreateFont(
								G->get_dpi_mul(18),                       // 字体高度
								0,                        // 字体宽度
								0,                        // 逸入角
								0,                        // 取向角
								FW_BOLD,                // 字体粗细 (0 或 FW_NORMAL)
								FALSE,                    // 斜体
								FALSE,                    // 下划线
								FALSE,                    // 删除线
								DEFAULT_CHARSET,          // 字符集
								0,          // 10. 输出精度 (更精确的文本匹配)
								CLIP_DEFAULT_PRECIS,      // 11. 剪裁精度
								CLEARTYPE_QUALITY,        // 12. 渲染质量 (强制开启 ClearType 平滑抗锯齿)
								FIXED_PITCH | FF_MODERN,  // 13. 等宽字体族 (更契合 Consolas 的定位)
								L"Microsoft YaHei UI"               // 14. 字体名称
								//L"Consolas"               // 14. 字体名称
							);
						}
							//hFontBold = CreateFont(18, 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");

						SelectObject(lv->nmcd.hdc, hFontBold);

						// 顺便还能改这一列的颜色（比如改成红色或蓝色提示）
						// lplvcd->clrText = RGB(0, 102, 204); 

						return CDRF_NEWFONT; // 告诉系统：我换了新字体，请用它来画
					}

					return CDRF_DODEFAULT;
				}
				}
			}
		}

		break;
	}
	case WM_KEYDOWN:

		// 完美支持键盘 Ctrl + C 快捷键拦截，将蓝色框选的多选字节提取为标准 Hex 文本导出
		if (wParam == 'C' && (::GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
			if (!g_Metric.hasSelection) break;

			// 1. 计算选中的内存边界与总字节数
			uintptr_t minSel = emin(g_Metric.selectionStart, g_Metric.selectionEnd);
			uintptr_t maxSel = emax(g_Metric.selectionStart, g_Metric.selectionEnd);
			size_t byteCount = maxSel - minSel + 1;

			// 2. 算好文本缓冲区所需的绝对大小：
			// 每个字节需要 3 个字符（2个十六进制字符 + 1个空格，如 "FF "）
			// 末尾多加 1 个字节用于存放字符串结束符 '\0'
			size_t stringBufferSize = (byteCount * 3) + 1;

			// 3. 使用 Win32 原生堆内存分配缓冲区
			char* pLocalBuffer = (char*)::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, stringBufferSize);
			if (pLocalBuffer == NULL) break;

			char* pCurrent = pLocalBuffer;

			// 4. 循环读取内存并格式化拼接
			for (uintptr_t addr = minSel; addr <= maxSel; ++addr) {
				unsigned char bVal = 0;
				MockReadProcessMemory(addr, &bVal, 1);

				// 指针式安全格式化：sprintf_s 会返回成功写入的字符数（通常是 3）
				// 每次写入后，指针向后移动 3 位，紧随其后继续拼接
				int written = nt::sprintf(pCurrent, "%02X ", bVal);
				if (written > 0) {
					pCurrent += written;
				}
			}

			// 5. 抹除最后一个字节多出来的空格
			if (pCurrent > pLocalBuffer) {
				*(pCurrent - 1) = '\0';
			}
			
			// 6. 标准 Win32 全局剪贴板注入逻辑
			// 剪贴板由于是跨进程传输，不能直接用上面的本地堆指针，必须用 GlobalAlloc
			if (::OpenClipboard(hwnd)) {
				::EmptyClipboard();

				size_t finalLen = nt::strlen(pLocalBuffer);
				HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, finalLen + 1);
				if (hMem) {
					char* pClipboardData = (char*)::GlobalLock(hMem);
					if (pClipboardData) {
						// 纯 C 风格的字符串拷贝
						nt::strcpy(pClipboardData, pLocalBuffer);
						::GlobalUnlock(hMem);

						// 将数据推送至系统剪贴板 (CF_TEXT 代表 ANSI 文本)
						::SetClipboardData(CF_TEXT, hMem);
					}
				}
				::CloseClipboard();
			}

			// 7. 善后处理：必须释放本地堆内存，防止内存泄漏
			::HeapFree(::GetProcessHeap(), 0, pLocalBuffer);

			SetStateText("复制->%d个字节", byteCount);
		}
		break;
	case WM_SIZE:

		// 
		SendMessageW(G->hStatusBar, WM_SIZE, 0, 0);

		GetClientRect(hwnd, &rcClient);
		if (upperHeight > rcClient.bottom - SPLITTER_HEIGHT - 50)
			upperHeight = rcClient.bottom - SPLITTER_HEIGHT - 50;
		LayoutControls(hwnd);
		InvalidateRect(hwnd, NULL, TRUE);
		break;
	case WM_LBUTTONDOWN: {
		int x = lParam & 0xffff; // GET_X_LPARAM(lParam);
		int y = (lParam >> 16) & 0xffff; // GET_Y_LPARAM(lParam);

		if (y > upperHeight && y < upperHeight + SPLITTER_HEIGHT) {
			G->draggingSplitter = true;
			SetCapture(hwnd);
		}

		int mx = x;
		int my = y;
		if (::PtInRect(&rcBottomPane, { mx, my })) {
			// 如果目前正在编辑，鼠标点下时必须无条件先踢掉 Edit 焦点让其安全存盘
			if (g_Metric.isEditing) {
				::SetFocus(hwnd);
			}
			// 计算多选绝对内存地址起点
			uintptr_t clickAddr = GetAddressFromMouse(mx, my - rcBottomPane.top, g_pageBaseAddress);
			if (clickAddr != 0) {
				g_Metric.hasSelection = true;
				g_Metric.selectionStart = clickAddr;
				g_Metric.selectionEnd = clickAddr; // 暂未产生位移

				::SetCapture(hwnd); // 锁定鼠标流
				::InvalidateRect(hwnd, &rcBottomPane, FALSE);
			}
		}

		break;
	}
	case WM_MOUSEMOVE: {
		int x = lParam & 0xffff; // GET_X_LPARAM(lParam);
		int y = (lParam >> 16) & 0xffff; // GET_Y_LPARAM(lParam);

		// 如果鼠标正在拉动多选
		if (g_Metric.hasSelection && (wParam & MK_LBUTTON)) {
			int mx = GET_X_LPARAM(lParam);
			int my = GET_Y_LPARAM(lParam);

			uintptr_t currentMoveAddr = GetAddressFromMouse(mx, my - rcBottomPane.top, g_pageBaseAddress);
			if (currentMoveAddr != 0 && currentMoveAddr != g_Metric.selectionEnd) {
				g_Metric.selectionEnd = currentMoveAddr; // 实时修正多选终点
				::InvalidateRect(hwnd, &rcBottomPane, FALSE); // 高频刷新高亮蓝色块
			}
		}

		if (G->draggingSplitter) {
			GetClientRect(hwnd, &rcClient);
			int minHeight = 50;
			int maxHeight = rcClient.bottom - SPLITTER_HEIGHT - minHeight;
			if (y < minHeight) y = minHeight;
			if (y > maxHeight) y = maxHeight;
			upperHeight = y;
			LayoutControls(hwnd);
			InvalidateRect(hwnd, NULL, TRUE);
		}

		else if (y > upperHeight && y < upperHeight + SPLITTER_HEIGHT) {
			G->bakcur = SetCursor((HCURSOR)G->crVSplit);
		}
		else if (GetCursor() != G->crDefault) {
			SetCursor((HCURSOR)G->crDefault);
		}
		break;
	}
	case WM_LBUTTONUP:
		if (G->draggingSplitter) {
			G->draggingSplitter = false;
			//SetCursor((HCURSOR)G->bakcur);

			ReleaseCapture();
		}

		if (g_Metric.hasSelection) {
			::ReleaseCapture();

			// 如果用户原地点击，没有拉出多选区间，退化为激活单字节编辑
			if (g_Metric.selectionStart == g_Metric.selectionEnd) {
				g_Metric.hasSelection = false; // 清空多选状态
				ActivateGhostEdit(hwnd, g_Metric.selectionStart, rcBottomPane);
			}
		}
		break;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		GetClientRect(hwnd, &rcClient);
		// 绘制分隔条
		RECT splitter = rcClient;
		splitter.top = upperHeight;
		splitter.bottom = upperHeight + SPLITTER_HEIGHT;
		FillRect(hdc, &splitter, (HBRUSH)GetStockObject(GRAY_BRUSH));
		int cx = (rcClient.left + rcClient.right) / 2;

		//for (int i = -1; i <= 1; ++i) {
		//	MoveToEx(hdc, cx - 12, upperHeight + (SPLITTER_HEIGHT / 2) + i * 2, NULL);
		//	LineTo(hdc, cx + 12, upperHeight + (SPLITTER_HEIGHT / 2) + i * 2);
		//}
		
		// 下半内容区演示与选中行联动
		DrawBottomPane(hdc, rcBottomPane);
		EndPaint(hwnd, &ps);
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0); break;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

// 重布局ListView和分割条
void LayoutControls(HWND hwnd) {
	RECT rc;
	GetClientRect(hwnd, &rc);
	MoveWindow(G->hListView, rc.left, rc.top, rc.right - rc.left, upperHeight, TRUE);
	// 2. 【直接兼容】让 rcBottomPane 严丝合缝地填满下面
	// 左边界、右边界、下边界完全和整个主视口对齐
	rcBottomPane.left = rc.left;
	// 顶边界刚好处于：ListView 的底部 + 分割条的厚度
	rcBottomPane.top = rc.top + upperHeight + SPLITTER_HEIGHT;
	rcBottomPane.right = rc.right;
	rcBottomPane.bottom = rc.bottom;
}


void SetStateText(const char* fmt, ...) {
	va_list va; va_start(va, fmt);
	nt::vsnprintf(G->tmps, -1, fmt, va); va_end(va);
	nt::tow(G->vw_mem_StateBar, G->tmps);
	SendMessageW(G->hStatusBar, SB_SETTEXT, 0 | SBT_OWNERDRAW, (LPARAM)G->vw_mem_StateBar);
}


// ============================================================================
// 三、 数学公式计算层：像素坐标 与 绝对地址的互相换算
// ============================================================================

// 1. 核心公式：根据鼠标相对视口(mx, my)算出对应的绝对内存地址
uintptr_t GetAddressFromMouse(int mx, int my, uintptr_t pageBaseAddress) {
	int row = my / g_Metric.rowHeight;
	if (row < 0) return 0;

	int hexStartX = g_Metric.addressWidth + 20;
	int localX = mx - hexStartX;
	if (localX < 0) return 0; // 点到了左侧地址栏

	// 扣除每 4 个字节加上的 groupSpacing 空隙进行精确推算
	// 为了防止除零或边缘越界，进行安全过滤
	int col = 0;
	int currentX = 0;
	for (int c = 0; c < g_Metric.bytesPerRow; ++c) {
		int groupSpacing = (c / 4) * (g_Metric.charSize / 2);
		int nextX = (c + 1) * g_Metric.byteSizeNoChar + groupSpacing;
		if (localX >= currentX && localX < nextX) {
			col = c;
			break;
		}
		currentX = nextX;
		if (c == g_Metric.bytesPerRow - 1) col = 15; // 越界兜底
	}

	return pageBaseAddress + (row * g_Metric.bytesPerRow) + col;
}

// 2. 逆向公式：根据绝对内存地址反推其在屏幕上的精确 RECT 范围 (供 MoveWindow 覆盖使用)
RECT GetRectFromAddress1(uintptr_t targetAddress, RECT rcPaneClient) {
	RECT rcGrid = { 0, 0, 0, 0 };
	long long offset = targetAddress - g_pageBaseAddress;

	// 如果地址不在当前可见视口内，返回空矩形
	int maxVisibleRows = (rcPaneClient.bottom - rcPaneClient.top) / g_Metric.rowHeight;
	if (offset < 0 || offset >= (g_Metric.bytesPerRow * maxVisibleRows)) return rcGrid;

	int row = (int)(offset / g_Metric.bytesPerRow);
	int col = (int)(offset % g_Metric.bytesPerRow);

	int y = row * g_Metric.rowHeight;
	int hexStartX = g_Metric.addressWidth + 20;
	int groupSpacing = (col / 4) * (g_Metric.charSize / 2);
	int x = hexStartX + (col * g_Metric.byteSizeNoChar) + groupSpacing;

	rcGrid.left = x;
	rcGrid.top = y;
	rcGrid.right = x + g_Metric.byteSizeNoChar - (g_Metric.charSize / 2); // 扣除末尾空格
	rcGrid.bottom = y + g_Metric.rowHeight;
	return rcGrid;
}
RECT GetRectFromAddress(uintptr_t targetAddress, RECT rcPaneClient) {
	RECT rcGrid = { 0, 0, 0, 0 };

	// ✅ 计算相对于当前页面的偏移
	long long offset = (long long)(targetAddress - g_pageBaseAddress);
	if (offset < 0) return rcGrid;

	int maxVisibleRows = (rcPaneClient.bottom - rcPaneClient.top) / g_Metric.rowHeight;
	int maxVisibleBytes = maxVisibleRows * g_Metric.bytesPerRow;

	if (offset >= maxVisibleBytes) return rcGrid;

	int row = (int)(offset / g_Metric.bytesPerRow);
	int col = (int)(offset % g_Metric.bytesPerRow);

	int y = row * g_Metric.rowHeight;
	int hexStartX = g_Metric.addressWidth + 20;
	int groupSpacing = (col / 4) * (g_Metric.charSize / 2);
	int x = hexStartX + (col * g_Metric.byteSizeNoChar) + groupSpacing;

	rcGrid.left = x;
	rcGrid.top = y;
	rcGrid.right = x + g_Metric.byteSizeNoChar - (g_Metric.charSize / 2);
	rcGrid.bottom = y + g_Metric.rowHeight;

	return rcGrid;
}
// ============================================================================
// 四、 核心手绘图层：升级版高性能双缓冲自绘 (精细控权，为 Edit 开天窗)
// ============================================================================
void DrawBottomPane(HDC hdc, RECT rcClient) {
	int width = rcClient.right - rcClient.left;
	int height = rcClient.bottom - rcClient.top;

	// 1. 开启标准离屏双缓冲，防高频重绘闪烁
	HDC hMemDC = CreateCompatibleDC(hdc);
	HBITMAP hMemBitmap = CreateCompatibleBitmap(hdc, width, height);
	HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hMemBitmap);

	// 2. 刷白背景
	//HBRUSH hBgBrush = CreateSolidBrush(RGB(255, 255, 255));
	RECT rcMemClient = { 0, 0, width, height };
	FillRect(hMemDC, &rcMemClient, (HBRUSH)nt::darkbrush());
	//DeleteObject(hBgBrush);

	// 3. 注入等宽字体
	HFONT hOldFont = (HFONT)SelectObject(hMemDC, g_Metric.hFont);
	SetBkMode(hMemDC, TRANSPARENT);

	int maxRows = height / g_Metric.rowHeight;
	if (maxRows <= 0) maxRows = 1;

	// 提取全局绝对地址选区的绝对极值
	uintptr_t minSel = emin(g_Metric.selectionStart, g_Metric.selectionEnd);
	uintptr_t maxSel = emax(g_Metric.selectionStart, g_Metric.selectionEnd);

	// 4. 行级渲染大循环
	for (int row = 0; row < maxRows; ++row) {
		int y = row * g_Metric.rowHeight;
		uintptr_t rowAddress = g_pageBaseAddress + (row * g_Metric.bytesPerRow);

		// 批量读取整行，优化IO降低跨进程损耗
		unsigned char rowData[32] = { 0 };
		MockReadProcessMemory(rowAddress, rowData, g_Metric.bytesPerRow);

		// A. 绘制左侧地址栏
		wchar_t szAddress[32];
		nt::swprintf(szAddress, L"%08I64X", (long long)rowAddress);
		SetTextColor(hMemDC, RGB(255, 255, 255));
		RECT rcAddress = { 10, y, g_Metric.addressWidth, y + g_Metric.rowHeight };
		DrawText(hMemDC, szAddress, -1, &rcAddress, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

		// B. 内部列元素（Hex码 与 ASCII 文本并列测画）
		int hexStartX = g_Metric.addressWidth + 20;
		int asciiStartX = hexStartX + (g_Metric.bytesPerRow * g_Metric.byteSizeNoChar) + 50;

		for (int col = 0; col < g_Metric.bytesPerRow; ++col) {
			uintptr_t currentByteAddress = rowAddress + col;
			unsigned char byteValue = rowData[col];

			int groupSpacing = (col / 4) * (g_Metric.charSize / 2);
			int xHex = hexStartX + (col * g_Metric.byteSizeNoChar) + groupSpacing;
			int xAscii = asciiStartX + (col * g_Metric.charSize);

			// 【重写核按钮】：如果此地址格正在被幽灵 Edit 接管编辑，手写层立刻在此“开天窗”留白，防止重叠
			if (g_Metric.isEditing && currentByteAddress == g_Metric.editingAddress) {
				continue;
			}

			// 判断当前字节地址是否落在多选蓝色高亮区间
			bool isSelected = g_Metric.hasSelection && (currentByteAddress >= minSel && currentByteAddress <= maxSel);

			wchar_t szByte[4];
			nt::swprintf(szByte, L"%02X", byteValue);
			RECT rcByte = { xHex, y, xHex + g_Metric.byteSizeNoChar, y + g_Metric.rowHeight };

			wchar_t szChar[2] = { L'.', L'\0' };
			if (byteValue >= 32 && byteValue <= 126) szChar[0] = (wchar_t)byteValue;
			RECT rcChar = { xAscii, y, xAscii + g_Metric.charSize, y + g_Metric.rowHeight };

			if (isSelected) {
				// 如果处于被框选范围，双轨刷涂经典高亮蓝
				//HBRUSH hSelBrush = CreateSolidBrush(RGB(51, 153, 255));
				//RECT rcHexBg = { rcByte.left, rcByte.top + 1, rcByte.right, rcByte.bottom - 1 };
				//RECT rcAsciiBg = { rcChar.left, rcChar.top + 1, rcChar.right, rcChar.bottom - 1 };
				//FillRect(hMemDC, &rcHexBg, hSelBrush);
				//FillRect(hMemDC, &rcAsciiBg, hSelBrush);
				//DeleteObject(hSelBrush);
				SetTextColor(hMemDC, RGB(255, 0, 0)); // 被选中字刷红
			}
			else {
				SetTextColor(hMemDC, RGB(0, 0x7F, 0)); // 仿CE：地址线绿色渲染
			}

			// 执行物理文字上屏
			DrawText(hMemDC, szByte, -1, &rcByte, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			if (!isSelected) SetTextColor(hMemDC, RGB(80, 255, 80)); // 文本恢复灰色
			DrawText(hMemDC, szChar, -1, &rcChar, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
	}

	// 5. 闪击位块传输
	BitBlt(hdc, rcClient.left, rcClient.top, width, height, hMemDC, 0, 0, SRCCOPY);

	// 6. 垃圾资源善后
	SelectObject(hMemDC, hOldFont);
	SelectObject(hMemDC, hOldBitmap);
	DeleteDC(hMemDC);
	DeleteObject(hMemBitmap);
}

// ============================================================================
// 五、 唤醒控制层：调度幽灵输入框进行精确覆盖
// ============================================================================
void ActivateGhostEdit(HWND hWndParent, uintptr_t targetAddress, RECT rcPaneClient) {
	if (targetAddress == 0 || !g_hGhostEdit) return;

	// ✅ 如果已经在编辑，先保存并退出
	if (g_Metric.isEditing) {
		// 可以在这里保存当前编辑的值
		g_Metric.isEditing = false;
		::ShowWindow(g_hGhostEdit, SW_HIDE);
	}

	g_Metric.isEditing = true;
	g_Metric.editingAddress = targetAddress;

	// 获取格子位置（相对于 pane 客户区）
	RECT rcTarget = GetRectFromAddress(targetAddress, rcPaneClient);
	if (rcTarget.left == 0 && rcTarget.top == 0 && rcTarget.right == 0) {
		// 地址不在可见区域
		g_Metric.isEditing = false;
		return;
	}

	// ✅ 转换为相对于父窗口的坐标
	RECT rcEdit = {
		rcPaneClient.left + rcTarget.left,
		rcPaneClient.top + rcTarget.top,
		rcPaneClient.left + rcTarget.right,
		rcPaneClient.top + rcTarget.bottom
	};

	// 移动并显示 Edit 控件
	::MoveWindow(g_hGhostEdit,
		rcEdit.left - 2,
		rcEdit.top + 1,
		(rcEdit.right - rcEdit.left) + 4,
		(rcEdit.bottom - rcEdit.top) - 2,
		TRUE);

	// 读取当前字节值
	unsigned char currentByte = 0;
	MockReadProcessMemory(targetAddress, &currentByte, 1);

	wchar_t szByte[8];
	nt::swprintf(szByte, L"%02X", currentByte);

	::SetWindowTextW(g_hGhostEdit, szByte);
	::SendMessage(g_hGhostEdit, EM_LIMITTEXT, 2, 0);
	::ShowWindow(g_hGhostEdit, SW_SHOW);
	::SetFocus(g_hGhostEdit);

	// 选中所有文本方便直接覆盖输入
	::SendMessage(g_hGhostEdit, EM_SETSEL, 0, -1);

	// 刷新区域
	::InvalidateRect(hWndParent, &rcPaneClient, FALSE);
}

// ============================================================================
// 六、 契约退场层：幽灵输入框的 C++ 子类化回调 (处理回车存盘、失去焦点隐身)
// ============================================================================
LRESULT CALLBACK GhostEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_KEYDOWN:
		if (wParam == VK_RETURN) { // 敲击回车
			HWND hParent = ::GetParent(hWnd);
			::SetFocus(hParent); // 强踢焦点，由下方的 KILLFOCUS 统一触发存盘逻辑
			return 0;
		}
		if (wParam == VK_ESCAPE) { // 敲击 ESC 放弃修改
			g_Metric.isEditing = false;
			::ShowWindow(hWnd, SW_HIDE);
			::InvalidateRect(::GetParent(hWnd), NULL, FALSE);
			return 0;
		}
		break;

	case WM_KILLFOCUS: // 核心锁：一旦失去焦点 (鼠标点击别处或敲回车)
		if (g_Metric.isEditing) {
			wchar_t szText[4] = { 0 };
			::GetWindowText(hWnd, szText, 4);

			// 字符串安全转回十六进制字节
			
			unsigned char newValue = (unsigned char)nt::hcc((const char *)szText);

			// 实际开发中在此处写回游戏内存
			// MockWriteProcessMemory(g_Metric.editingAddress, &newValue, 1);

			// 彻底注销并重归黑暗
			g_Metric.isEditing = false;
			g_Metric.editingAddress = 0;
			::ShowWindow(hWnd, SW_HIDE);

			// 唤醒全局重绘
			::InvalidateRect(::GetParent(hWnd), NULL, FALSE);
		}
		break;
	}
	return ::CallWindowProc(g_OldEditProc, hWnd, uMsg, wParam, lParam);
}
