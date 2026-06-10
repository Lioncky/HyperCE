#pragma warning(disable:26454)
#include "hyperce.h"
#include "vw_mem.h"

// ============================================================================
// 全局状态定义
// ============================================================================
HexViewMetrics g_Metric{ 140, 25, 12, 200, 32, 32 };

uintptr_t g_memoryMinAddress = 0x0000000000000000;
uintptr_t g_memoryMaxAddress = 0x00007FFFFFFFFFFF;
uintptr_t g_pageBaseAddress = 0x100000000;

HWND   g_hGhostEdit = NULL;
WNDPROC g_OldEditProc = NULL;
HWND   g_hBottomScroll = NULL;
int    upperHeight = 800;

static RECT rcBottomPane = { 0, 0, 800, 400 };

// ============================================================================
// 一、地址↔位置换算
// ============================================================================
int AddrToPos(uintptr_t addr) {
	double r = (double)(addr - g_memoryMinAddress) / (double)(g_memoryMaxAddress - g_memoryMinAddress);
	return (int)(r * SCROLL_MAX_RANGE);
}
uintptr_t PosToAddr(int pos) {
	double r = (double)pos / (double)SCROLL_MAX_RANGE;
	uintptr_t a = g_memoryMinAddress + (uintptr_t)(r * (double)(g_memoryMaxAddress - g_memoryMinAddress));
	return (a / 16) * 16;
}

// ============================================================================
// 二、内存读写
// ============================================================================
BOOL MockWriteProcessMemory(uintptr_t addr, void* buf, size_t size) {
	return G->WriteProcessMemoryEx((char*)addr, buf, (UINT)size) ? TRUE : FALSE;
}
BOOL MockReadProcessMemory(uintptr_t addr, void* buf, size_t size) {
	return G->ReadProcessMemoryEx((char*)addr, buf, (UINT)size) ? TRUE : FALSE;
}

// ============================================================================
// 三、状态栏文字
// ============================================================================
void SetStateText(const char* fmt, ...) {
	va_list va; va_start(va, fmt);
	nt::vsnprintf(G->tmps, -1, fmt, va); va_end(va);
	nt::tow(G->vw_mem_StateBar, G->tmps);
	SendMessageW(G->hStatusBar, SB_SETTEXT, 0 | SBT_OWNERDRAW, (LPARAM)G->vw_mem_StateBar);
}

// ============================================================================
// 四、坐标↔地址换算
// ============================================================================
uintptr_t GetAddressFromMouse(int mx, int my, uintptr_t pageBase) {
	int row = my / g_Metric.rowHeight;
	if (row < 0) return 0;

	int localX = mx - (g_Metric.addressWidth + 20);
	if (localX < 0) return 0;

	int col = 0, curX = 0;
	for (int c = 0; c < g_Metric.bytesPerRow; ++c) {
		int nextX = (c + 1) * g_Metric.byteSizeNoChar + (c / 4) * (g_Metric.charSize / 2);
		if (localX >= curX && localX < nextX) { col = c; break; }
		curX = nextX;
		if (c == g_Metric.bytesPerRow - 1) col = 15;
	}
	return pageBase + (row * g_Metric.bytesPerRow) + col;
}

RECT GetRectFromAddress(uintptr_t addr, RECT rcPane) {
	RECT rc = {};
	long long offset = (long long)(addr - g_pageBaseAddress);
	if (offset < 0) return rc;

	int maxBytes = ((rcPane.bottom - rcPane.top) / g_Metric.rowHeight) * g_Metric.bytesPerRow;
	if (offset >= maxBytes) return rc;

	int row = (int)(offset / g_Metric.bytesPerRow);
	int col = (int)(offset % g_Metric.bytesPerRow);
	int y = row * g_Metric.rowHeight;
	int x = (g_Metric.addressWidth + 20)
		+ col * g_Metric.byteSizeNoChar
		+ (col / 4) * (g_Metric.charSize / 2);

	rc = { x, y, x + g_Metric.byteSizeNoChar - g_Metric.charSize / 2, y + g_Metric.rowHeight };
	return rc;
}

// ============================================================================
// 五、下半区绘制
// ============================================================================
void DrawBottomPane(HDC hdc, RECT rcPane) {
	int w = rcPane.right - rcPane.left;
	int h = rcPane.bottom - rcPane.top;

	HDC     hMem = CreateCompatibleDC(hdc);
	HBITMAP hBmp = CreateCompatibleBitmap(hdc, w, h);
	HBITMAP hOldB = (HBITMAP)SelectObject(hMem, hBmp);

	RECT rcFull = { 0, 0, w, h };
	FillRect(hMem, &rcFull, (HBRUSH)nt::darkbrush());

	HFONT hOldF = (HFONT)SelectObject(hMem, g_Metric.hFont);
	SetBkMode(hMem, TRANSPARENT);

	int maxRows = max(1, h / g_Metric.rowHeight);
	uintptr_t minSel = emin(g_Metric.selectionStart, g_Metric.selectionEnd);
	uintptr_t maxSel = emax(g_Metric.selectionStart, g_Metric.selectionEnd);

	int hexStartX = g_Metric.addressWidth + 20;
	int asciiStartX = hexStartX + g_Metric.bytesPerRow * g_Metric.byteSizeNoChar + 50;

	for (int row = 0; row < maxRows; ++row) {
		int       y = row * g_Metric.rowHeight;
		uintptr_t rowAddr = g_pageBaseAddress + row * g_Metric.bytesPerRow;
		unsigned char data[32] = {};
		bool ok = MockReadProcessMemory(rowAddr, data, g_Metric.bytesPerRow);

		// 地址栏
		wchar_t szAddr[32];
		nt::swprintf(szAddr, L"%llX", (long long)rowAddr);
		SetTextColor(hMem, RGB(255, 255, 255));
		RECT rcA = { 10, y, g_Metric.addressWidth, y + g_Metric.rowHeight };
		DrawText(hMem, szAddr, -1, &rcA, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

		for (int col = 0; col < g_Metric.bytesPerRow; ++col) {
			uintptr_t curAddr = rowAddr + col;
			if (g_Metric.isEditing && curAddr == g_Metric.editingAddress) continue;

			unsigned char bv = data[col];
			int gs = (col / 4) * (g_Metric.charSize / 2);
			int xH = hexStartX + col * g_Metric.byteSizeNoChar + gs;
			int xA = asciiStartX + col * g_Metric.charSize;

			bool sel = g_Metric.hasSelection && curAddr >= minSel && curAddr <= maxSel;

			wchar_t szB[4], szC[2] = { bv >= 32 && bv <= 126 ? (wchar_t)bv : L'.', 0 };
			nt::swprintf(szB, ok ? L"%02X" : L"??", bv);

			RECT rcH = { xH, y, xH + g_Metric.byteSizeNoChar, y + g_Metric.rowHeight };
			RECT rcC = { xA, y, xA + g_Metric.charSize,        y + g_Metric.rowHeight };

			SetTextColor(hMem, sel ? RGB(255, 0, 0) : RGB(0, 0x7F, 0));
			DrawText(hMem, szB, -1, &rcH, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			if (!sel) SetTextColor(hMem, RGB(80, 255, 80));
			DrawText(hMem, szC, -1, &rcC, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}
	}

	BitBlt(hdc, rcPane.left, rcPane.top, w, h, hMem, 0, 0, SRCCOPY);
	SelectObject(hMem, hOldF);
	SelectObject(hMem, hOldB);
	DeleteDC(hMem);
	DeleteObject(hBmp);
}

// ============================================================================
// 六、布局
// ============================================================================
void LayoutControls(HWND hwnd) {
	RECT rc;
	GetClientRect(hwnd, &rc);
	MoveWindow(G->hListView, rc.left, rc.top, rc.right - rc.left, upperHeight, TRUE);

	int statusH = 0;
	if (G->hStatusBar && IsWindowVisible(G->hStatusBar)) {
		RECT rs; GetWindowRect(G->hStatusBar, &rs);
		statusH = rs.bottom - rs.top;
	}

	rcBottomPane = { rc.left, rc.top + upperHeight + SPLITTER_HEIGHT,
					 rc.right, rc.bottom - statusH };

	// Edit 和 Button 在 hStatusBar 客户区内定位（父窗口是 hStatusBar）
	if (G->hStatusEdit && G->hStatusBtn && G->hStatusBar) {
		RECT rsb; GetClientRect(G->hStatusBar, &rsb);
		const int btnW = G->get_dpi_mul(32);
		const int editW = G->get_dpi_mul(160);
		const int pad = G->get_dpi_mul(3);
		const int ctrlH = rsb.bottom - pad * 2;
		const int ctrlY = pad;

		MoveWindow(G->hStatusBtn,
			rsb.right - btnW - pad, ctrlY,
			btnW, ctrlH, TRUE);
		MoveWindow(G->hStatusEdit,
			rsb.right - btnW - editW - pad * 2, ctrlY,
			editW, ctrlH, TRUE);
	}

	if (g_hBottomScroll) {
		int sbW = G->get_dpi_mul(18);
		int sbH = rcBottomPane.bottom - rcBottomPane.top;

		MoveWindow(g_hBottomScroll,
			rcBottomPane.right - sbW, rcBottomPane.top,
			sbW, sbH, TRUE);

		rcBottomPane.right -= sbW;
	}
}

// ============================================================================
// 七、幽灵 Edit 激活
// ============================================================================
void ActivateGhostEdit(HWND hParent, uintptr_t addr, RECT rcPane) {
	if (!addr || !g_hGhostEdit) return;

	if (g_Metric.isEditing) {
		g_Metric.isEditing = false;
		::ShowWindow(g_hGhostEdit, SW_HIDE);
	}

	RECT rcT = GetRectFromAddress(addr, rcPane);
	if (!rcT.left && !rcT.top && !rcT.right) return;

	g_Metric.isEditing = true;
	g_Metric.editingAddress = addr;

	RECT rcE = {
		rcPane.left + rcT.left,
		rcPane.top + rcT.top,
		rcPane.left + rcT.right,
		rcPane.top + rcT.bottom
	};

	::MoveWindow(g_hGhostEdit,
		rcE.left - 2, rcE.top + 1,
		(rcE.right - rcE.left) - 4,
		(rcE.bottom - rcE.top) - 2,
		TRUE);

	unsigned char b = 0;
	MockReadProcessMemory(addr, &b, 1);

	wchar_t sz[8];
	nt::swprintf(sz, L"%02X", b);

	::SetWindowTextW(g_hGhostEdit, sz);
	::SendMessageW(g_hGhostEdit, EM_LIMITTEXT, 2, 0);
	::ShowWindow(g_hGhostEdit, SW_SHOW);
	::SetFocus(g_hGhostEdit);
	::SendMessage(g_hGhostEdit, EM_SETSEL, -1, -1);

	::InvalidateRect(hParent, &rcPane, FALSE);
}

// ============================================================================
// 八、WM_CREATE
// ============================================================================

static BOOL on_wm_drawitem(LPDRAWITEMSTRUCT di) {
	if (di->hwndItem == G->hStatusBtn) {
		HDC  hdc = di->hDC;

		RECT rect = di->rcItem;
		bool pressed = (di->itemState & ODS_SELECTED) != 0;
		bool hot = (di->itemState & ODS_HOTLIGHT) != 0;
		COLORREF bg = pressed ? RGB(0x25, 0x25, 0x25)
			: hot ? RGB(0x60, 0x60, 0x60)
			: RGB(0x32, 0x32, 0x32);

		FillRect(hdc, &rect, (HBRUSH)nt::darkbrush());

		// 边框
		COLORREF border = RGB(0x70, 0x70, 0x70);
		HPEN hPen = CreatePen(PS_SOLID, 1, border);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
		HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
		Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
		SelectObject(hdc, hOldPen); SelectObject(hdc, hOldBr); DeleteObject(hPen);
		// 文字：用 Segoe UI Symbol 保证箭头字符可见
		if (pressed) OffsetRect(&rect, 1, 1);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(220, 220, 220));
		SelectObject(hdc, g_Metric.hFont);
		DrawText(hdc, L"→", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}

	if (di->hwndItem == G->hStatusBar) {

		HDC  hdc = di->hDC;
		RECT rect = di->rcItem;
		SelectClipRgn(hdc, NULL);
		rect.left -= 2; rect.top -= 2; rect.right += 2; rect.bottom += 2;
		FillRect(hdc, &rect, (HBRUSH)nt::darkbrush());
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(255, 255, 255));
		rect.left += 5;
		DrawText(hdc, (wchar_t*)di->itemData, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}
	return FALSE;
}
static void on_wm_create(HWND hwnd) {
	Nt::EnableDarkModeDwm(hwnd);

	// 字体与度量
	HDC hdc = GetDC(hwnd);
	g_Metric.hFont = CreateFont(G->get_dpi_mul(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

	HFONT hOld = (HFONT)SelectObject(hdc, g_Metric.hFont);
	SIZE  sc, sb;
	GetTextExtentPoint32(hdc, L"X", 1, &sc);
	GetTextExtentPoint32(hdc, L"XX ", 3, &sb);
	g_Metric.charSize = sc.cx;
	g_Metric.byteSizeNoChar = sb.cx;
	g_Metric.addressWidth = sc.cx * 16;
	g_Metric.rowHeight = sc.cy + 4;
	SelectObject(hdc, hOld);
	ReleaseDC(hwnd, hdc);

	// 幽灵 Edit
	g_hGhostEdit = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, L"EDIT", L"",
		WS_CHILD | ES_UPPERCASE | ES_AUTOHSCROLL,
		0, 0, 0, 0, hwnd, (HMENU)1002, NtCurrentImageBase(), NULL);

	LONG_PTR sty = GetWindowLongPtr(g_hGhostEdit, GWL_STYLE);
	SetWindowLongPtr(g_hGhostEdit, GWL_STYLE, sty & ~WS_BORDER);
	::SendMessage(g_hGhostEdit, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);
	g_OldEditProc = (WNDPROC)::SetWindowLongPtr(g_hGhostEdit, GWLP_WNDPROC, (LONG_PTR)GhostEditSubclassProc);

	// 滚动条
	g_hBottomScroll = ::CreateWindowEx(0, L"SCROLLBAR", NULL,
		WS_CHILD | WS_VISIBLE | SBS_VERT,
		0, 0, 0, 0, hwnd, (HMENU)1003, NtCurrentImageBase(), NULL);
	Nt::UxSetWindowTheme(g_hBottomScroll, L"Explorer", NULL);

	SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS,
		0, SCROLL_MAX_RANGE, 300, AddrToPos(g_pageBaseAddress) };
	::SetScrollInfo(g_hBottomScroll, SB_CTL, &si, TRUE);

	// ListView
	RECT rcC; GetClientRect(hwnd, &rcC);
	G->hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
		WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA,
		0, 0, rcC.right, upperHeight, hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);

	Nt::UxSetWindowTheme(G->hListView, L"DarkMode_Explorer", NULL);
	G->hHeader = ListView_GetHeader(G->hListView);
	Nt::UxSetWindowTheme(G->hHeader, L"ItemsView", NULL);

	HIMAGELIST hIL = ImageList_Create(1, G->get_dpi_mul(22), ILC_COLOR32, 1, 1);
	ListView_SetImageList(G->hListView, hIL, LVSIL_SMALL);

	HFONT hHdrFont = CreateFont(G->get_dpi_mul(22), 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
	SendMessage(G->hHeader, WM_SETFONT, (WPARAM)hHdrFont, TRUE);

	COLORREF bg = RGB(0x20, 0x20, 0x20);
	ListView_SetBkColor(G->hListView, bg);
	ListView_SetTextBkColor(G->hListView, bg);
	ListView_SetTextColor(G->hListView, RGB(255, 255, 255));

	SetWindowSubclass(G->hListView,
		[](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
			UINT_PTR, DWORD_PTR) -> LRESULT {
				if (uMsg == WM_NOTIFY && LPNMHDR(lParam)->code == NM_CUSTOMDRAW) {
					auto* d = (LPNMCUSTOMDRAW)lParam;
					if (d->dwDrawStage == CDDS_PREPAINT)  return CDRF_NOTIFYITEMDRAW;
					if (d->dwDrawStage == CDDS_ITEMPREPAINT) {
						SetTextColor(d->hdc, RGB(255, 255, 255));
						SetBkColor(d->hdc, RGB(32, 32, 32));
						return CDRF_DODEFAULT;
					}
				}
				return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}, 0, 0);

	ListView_SetExtendedListViewStyle(G->hListView, LVS_EX_FULLROWSELECT);

	auto AddCol = [](int i, int cx, LPCWSTR text) {
		LVCOLUMN c = { LVCF_WIDTH | LVCF_TEXT, 0, cx, (LPWSTR)text };
		ListView_InsertColumn(G->hListView, i, &c);
	};
	AddCol(0, 580, L"地址 Address");
	AddCol(1, 380, L"字节 Bytes");
	AddCol(2, 360, L"指令 Asm");
	AddCol(3, 1020, L"注释 Note");

	ListView_SetItemCount(G->hListView, 1000000);
	ListView_SetItemState(G->hListView, -1, 0, LVIS_SELECTED);
	ListView_EnsureVisible(G->hListView, 500010, FALSE);
	ListView_SetItemState(G->hListView, 500000,
		LVNI_SELECTED | LVNI_FOCUSED, LVNI_SELECTED | LVNI_FOCUSED);
	SetFocus(G->hListView);

	// 状态栏
	G->hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL,
		WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, hwnd, NULL, NtCurrentImageBase(), NULL);
	SendMessageW(G->hStatusBar, SB_SETTEXT, 0 | SBT_OWNERDRAW, (LPARAM)L"就绪");

	// Edit 和 Button 作为状态栏的子控件，天然在状态栏内部，无 Z-order 问题
	G->hStatusEdit = CreateWindowExW(
		0, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_UPPERCASE | ES_AUTOHSCROLL,
		0, 0, 0, 0, G->hStatusBar, (HMENU)IDC_STATUS_EDIT, NtCurrentImageBase(), NULL);
	SendMessage(G->hStatusEdit, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);

	G->hStatusBtn = CreateWindowExW(
		0, L"BUTTON", L"→",
		WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
		0, 0, 0, 0, G->hStatusBar, (HMENU)IDC_STATUS_BTN, NtCurrentImageBase(), NULL);
	SendMessage(G->hStatusBtn, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);

	// 子类化状态栏：只转发 Edit 相关消息
	SetWindowSubclass(G->hStatusBar, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) -> LRESULT {
		if (uMsg == WM_COMMAND || uMsg == WM_CTLCOLOREDIT || 
			(uMsg == WM_DRAWITEM && LPDRAWITEMSTRUCT(lParam)->hwndItem == G->hStatusBtn)
			)
			return SendMessage(GetParent(hWnd), uMsg, wParam, lParam);

		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}, 1, 0);
}

// ============================================================================
// 九、主窗口过程
// ============================================================================
int ui_show_mmview(void*) {
	int w = G->get_dpi_mul(WIN_WIDTH), h = G->get_dpi_mul(WIN_HEIGHT);
	int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
	int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

	HWND hwnd = Nt::CreateWnd(MMVIEW_CAPTION, MMVIEW_CAPTION, WndProc);
	Nt::SetWindowPosU32(hwnd, 0, x, y, w, h, SWP_SHOWWINDOW);
	Nt::AddWndStyle(hwnd, WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
	Nt::AddWndIcon(hwnd, G->exeIcon);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg); DispatchMessage(&msg);
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static RECT rcClient;
	switch (msg) {

	case WM_CREATE:
		on_wm_create(hwnd);
		break;

	case WM_DRAWITEM: {
		if (on_wm_drawitem((LPDRAWITEMSTRUCT)lParam))
			return TRUE;
		break;
	}

	case WM_NOTIFY: {
		auto* hdr = (LPNMHDR)lParam;
		if (hdr->hwndFrom != G->hListView) break;

		if (hdr->code == LVN_ITEMCHANGED) {
			SetStateText("选中：第 %d 行", ((LPNMLISTVIEW)lParam)->iItem);
		}
		else if (hdr->code == LVN_GETDISPINFO) {
			auto* pdi = (NMLVDISPINFO*)lParam;
			if (pdi->item.mask & LVIF_TEXT) {
				switch (pdi->item.iSubItem) {
				case 0: wsprintf(pdi->item.pszText, L"Item %d", pdi->item.iItem);     break;
				case 1: wsprintf(pdi->item.pszText, L"Value %d", pdi->item.iItem * 2); break;
				}
			}
			return TRUE;
		}
		else if (hdr->code == NM_CUSTOMDRAW) {
			auto* lv = (LPNMLVCUSTOMDRAW)lParam;
			switch (lv->nmcd.dwDrawStage) {
			case CDDS_PREPAINT:           return CDRF_NOTIFYITEMDRAW;
			case CDDS_ITEMPREPAINT:       return CDRF_NOTIFYSUBITEMDRAW;
			case CDDS_SUBITEM | CDDS_ITEMPREPAINT: {
				WCHAR text[128];
				ListView_GetItemText(G->hListView, (int)lv->nmcd.dwItemSpec, lv->iSubItem, text, 128);
				bool isNum = text[0] != 0;
				for (int i = 0; text[i]; i++) if (!nt::iswdigit(text[i])) { isNum = false; break; }
				lv->clrText = isNum ? RGB(0, 255, 0) : RGB(255, 255, 255);
				lv->clrTextBk = RGB(0x20, 0x20, 0x20);
				if (lv->iSubItem == 2) {
					static HFONT hFontBold;
					if (!hFontBold)
						hFontBold = CreateFont(G->get_dpi_mul(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
							DEFAULT_CHARSET, 0, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
							FIXED_PITCH | FF_MODERN, L"Microsoft YaHei UI");
					SelectObject(lv->nmcd.hdc, hFontBold);
					return CDRF_NEWFONT;
				}
				return CDRF_DODEFAULT;
			}
			}
		}
		break;
	}

	case WM_KEYDOWN:
		if (wParam == 'C' && (::GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
			if (!g_Metric.hasSelection) break;
			uintptr_t lo = emin(g_Metric.selectionStart, g_Metric.selectionEnd);
			uintptr_t hi = emax(g_Metric.selectionStart, g_Metric.selectionEnd);
			size_t    cnt = hi - lo + 1;
			char* buf = (char*)::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, cnt * 3 + 1);
			if (!buf) break;
			char* p = buf;
			for (uintptr_t a = lo; a <= hi; ++a) {
				unsigned char v = 0;
				if (!MockReadProcessMemory(a, &v, 1)) { *(unsigned*)p = *(unsigned*)"?? "; p += 3; continue; }
				p += nt::sprintf(p, "%02X ", v);
			}
			if (p > buf) *(p - 1) = '\0';
			if (::OpenClipboard(hwnd)) {
				::EmptyClipboard();
				size_t len = nt::strlen(buf);
				HGLOBAL hG = ::GlobalAlloc(GMEM_MOVEABLE, len + 1);
				if (hG) {
					char* pd = (char*)::GlobalLock(hG);
					if (pd) { nt::strcpy(pd, buf); ::GlobalUnlock(hG); ::SetClipboardData(CF_TEXT, hG); }
				}
				::CloseClipboard();
			}
			::HeapFree(::GetProcessHeap(), 0, buf);
			SetStateText("复制->%d个字节", cnt);
		}
		break;

	case WM_SIZE:
		SendMessageW(G->hStatusBar, WM_SIZE, 0, 0);
		GetClientRect(hwnd, &rcClient);
		if (upperHeight > rcClient.bottom - SPLITTER_HEIGHT - 50)
			upperHeight = rcClient.bottom - SPLITTER_HEIGHT - 50;
		LayoutControls(hwnd);
		InvalidateRect(hwnd, NULL, TRUE);
		break;

	case WM_CTLCOLOREDIT:
		if ((HWND)lParam == g_hGhostEdit) {
			SetBkColor((HDC)wParam, RGB(0, 120, 215));
			SetTextColor((HDC)wParam, RGB(255, 255, 255));
			return (INT_PTR)nt::darkbrush();
		}
		if ((HWND)lParam == G->hStatusEdit) {
			SetBkColor((HDC)wParam, RGB(0x30, 0x30, 0x30));
			SetTextColor((HDC)wParam, RGB(255, 255, 255));
			return (INT_PTR)nt::darkbrush();
		}
		break;

	case WM_VSCROLL:
		if (lParam == (LPARAM)g_hBottomScroll) {
			if (g_Metric.isEditing) ::SetFocus(hwnd);
			switch (LOWORD(wParam)) {
			case SB_LINEUP:     g_pageBaseAddress -= g_Metric.bytesPerRow;      break;
			case SB_LINEDOWN:   g_pageBaseAddress += g_Metric.bytesPerRow;      break;
			case SB_PAGEUP:     g_pageBaseAddress -= g_Metric.bytesPerRow * 10; break;
			case SB_PAGEDOWN:   g_pageBaseAddress += g_Metric.bytesPerRow * 10; break;
			case SB_THUMBTRACK: g_pageBaseAddress = PosToAddr(HIWORD(wParam)); break;
			}
			g_pageBaseAddress = emin(emax(g_pageBaseAddress, g_memoryMinAddress), g_memoryMaxAddress);
			SCROLLINFO si = { sizeof(si), SIF_POS, 0, 0, 0, AddrToPos(g_pageBaseAddress) };
			::SetScrollInfo(g_hBottomScroll, SB_CTL, &si, TRUE);
			::InvalidateRect(hwnd, &rcBottomPane, FALSE);
		}
		break;

	case WM_MOUSEWHEEL: {
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		::ScreenToClient(hwnd, &pt);
		if (!::PtInRect(&rcBottomPane, pt)) break;
		if (g_Metric.isEditing) ::SetFocus(hwnd);
		short d = (short)HIWORD(wParam);
		g_pageBaseAddress += (d > 0 ? -1 : 1) * g_Metric.bytesPerRow * 3;
		g_pageBaseAddress = emin(emax(g_pageBaseAddress, g_memoryMinAddress), g_memoryMaxAddress);
		SCROLLINFO si = { sizeof(si), SIF_POS, 0, 0, 0, AddrToPos(g_pageBaseAddress) };
		::SetScrollInfo(g_hBottomScroll, SB_CTL, &si, TRUE);
		::InvalidateRect(hwnd, &rcBottomPane, FALSE);
		return 0;
	}

	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
		if (y > upperHeight && y < upperHeight + SPLITTER_HEIGHT) {
			G->draggingSplitter = true;
			SetCapture(hwnd);
			break;
		}
		if (::PtInRect(&rcBottomPane, { x, y })) {
			// 只有幽灵Edit正在编辑时才踢焦点，不影响状态栏上的输入框
			if (g_Metric.isEditing && ::GetFocus() == g_hGhostEdit)
				::SetFocus(hwnd);
			uintptr_t a = GetAddressFromMouse(x, y - rcBottomPane.top, g_pageBaseAddress);
			if (a) {
				g_Metric.hasSelection = true;
				g_Metric.selectionStart = a;
				g_Metric.selectionEnd = a;
				::SetCapture(hwnd);
				::InvalidateRect(hwnd, &rcBottomPane, FALSE);
			}
		}
		break;
	}
	case WM_RBUTTONDOWN: {
		break;
	}

	case WM_MOUSEMOVE: {
		int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
		if (g_Metric.hasSelection && (wParam & MK_LBUTTON)) {
			uintptr_t a = GetAddressFromMouse(x, y - rcBottomPane.top, g_pageBaseAddress);
			if (a && a != g_Metric.selectionEnd) {
				g_Metric.selectionEnd = a;
				::InvalidateRect(hwnd, &rcBottomPane, FALSE);
			}
		}
		if (G->draggingSplitter) {
			GetClientRect(hwnd, &rcClient);
			y = emin(emax(y, 50), rcClient.bottom - SPLITTER_HEIGHT - 50);
			upperHeight = y;
			LayoutControls(hwnd);
			InvalidateRect(hwnd, NULL, TRUE);
		}
		else if (y > upperHeight && y < upperHeight + SPLITTER_HEIGHT)
			G->bakcur = SetCursor((HCURSOR)G->crVSplit);
		else if (GetCursor() != G->crDefault)
			SetCursor((HCURSOR)G->crDefault);
		break;
	}

	case WM_LBUTTONUP:
		if (G->draggingSplitter) { G->draggingSplitter = false; ReleaseCapture(); }
		if (g_Metric.hasSelection) {
			::ReleaseCapture();
			if (g_Metric.selectionStart == g_Metric.selectionEnd) {
				g_Metric.hasSelection = false;
				ActivateGhostEdit(hwnd, g_Metric.selectionStart, rcBottomPane);

				long long addr = 0;
				if (MockReadProcessMemory(g_Metric.selectionStart, &addr, 8)) {

					SetStateText("%p %d(%02X) %d(%04X) %d(%X) %lld(%llX) f %s db %s",
						g_Metric.selectionStart,
						(int)*(char*)&addr, *(unsigned char*)&addr,
						(int)*(short*)&addr, *(unsigned short*)&addr,
						*(int*)&addr, *(unsigned*)&addr,
						addr, addr,
						nt::fss(*(float*)&addr), nt::fss(*(double*)&addr)
					);
				}
			}
			else {
				MEMORY_BASIC_INFORMATION mbi;
				if (G->QueryVirtualMemoryEx((PVOID)g_Metric.selectionStart, &mbi)) {
					auto size = (int)(intptr_t)(g_Metric.selectionStart - g_Metric.selectionEnd);
					if (size < 0) size = -size;

					SetStateText("%llX(%s%s%s-0x%X) at %p~%p(%d bytes)",
						mbi.BaseAddress,
						(mbi.Protect & 0xEE) ? "R" : "",
						(mbi.Protect & 0xCC) ? "W" : "",
						(mbi.Protect & 0xF0) ? "E" : "", mbi.RegionSize,
						g_Metric.selectionStart,
						g_Metric.selectionEnd, size
					);
				}
			}
		}
		break;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		GetClientRect(hwnd, &rcClient);
		RECT spl = { rcClient.left, upperHeight, rcClient.right, upperHeight + SPLITTER_HEIGHT };
		FillRect(hdc, &spl, (HBRUSH)GetStockObject(GRAY_BRUSH));
		DrawBottomPane(hdc, rcBottomPane);
		EndPaint(hwnd, &ps);
		break;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_STATUS_EDIT && HIWORD(wParam) == EN_CHANGE) {
			// on_status_edit_change((HWND)lParam);
		}
		if (LOWORD(wParam) == IDC_STATUS_BTN) {
			wchar_t szAddr[32] = {};
			GetWindowTextW(G->hStatusEdit, szAddr, 32);
			uintptr_t addr = 0;
			// 解析十六进制地址（兼容带 0x 前缀）
			wchar_t* p = szAddr;
			if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) p += 2;
			for (; *p; ++p) {
				wchar_t c = *p;
				int d = (c >= L'0' && c <= L'9') ? c - L'0'
					: (c >= L'A' && c <= L'F') ? c - L'A' + 10
					: (c >= L'a' && c <= L'f') ? c - L'a' + 10 : -1;
				if (d < 0) break;
				addr = addr * 16 + d;
			}
			if (addr) {
				// 16 字节对齐后设为页面基址
				g_pageBaseAddress = (addr / 16) * 16;
				g_pageBaseAddress = emin(emax(g_pageBaseAddress, g_memoryMinAddress), g_memoryMaxAddress);

				SCROLLINFO si = { sizeof(si), SIF_POS, 0, 0, 0, AddrToPos(g_pageBaseAddress) };
				::SetScrollInfo(g_hBottomScroll, SB_CTL, &si, TRUE);

				::InvalidateRect(hwnd, &rcBottomPane, FALSE);
				SetStateText("跳转到 %llX", (long long)g_pageBaseAddress);
			}
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

// ============================================================================
// 十、幽灵 Edit 子类化过程
// ============================================================================
LRESULT CALLBACK GhostEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {

	case WM_KEYDOWN:
		switch (wParam) {

		case VK_RETURN:
			::SetFocus(::GetParent(hWnd));
			return 0;

		case VK_ESCAPE:
			g_Metric.isEditing = false;
			::ShowWindow(hWnd, SW_HIDE);
			::InvalidateRect(::GetParent(hWnd), NULL, FALSE);
			return 0;

		case VK_LEFT: case VK_RIGHT: {
			int sel; ::SendMessageW(g_hGhostEdit, EM_GETSEL, (WPARAM)&sel, NULL);
			if (sel == 1) break;
			if (sel == 0 && wParam == VK_LEFT) {
				ActivateGhostEdit(hWnd, --g_Metric.selectionStart, rcBottomPane);
				::SendMessage(g_hGhostEdit, EM_SETSEL, 2, 2);
				return 0;
			}
			if (sel == 2 && wParam == VK_RIGHT) {
				ActivateGhostEdit(hWnd, ++g_Metric.selectionStart, rcBottomPane);
				::SendMessage(g_hGhostEdit, EM_SETSEL, 0, 0);
				return 0;
			}
			break;
		}

		case VK_UP: case VK_DOWN: {
			int sel;
			::SendMessageW(g_hGhostEdit, EM_GETSEL, (WPARAM)&sel, NULL);
			int off = (wParam == VK_UP ? -16 : 16) << 1;
			uintptr_t newAddr = g_Metric.editingAddress += off;
			g_Metric.selectionStart += off;
			ActivateGhostEdit(::GetParent(hWnd), newAddr, rcBottomPane);
			::SendMessageW(g_hGhostEdit, EM_SETSEL, sel, sel);
			return 0;
		}

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
		case VK_NUMPAD0: case VK_NUMPAD1: case VK_NUMPAD2:
		case VK_NUMPAD3: case VK_NUMPAD4: case VK_NUMPAD5:
		case VK_NUMPAD6: case VK_NUMPAD7: case VK_NUMPAD8: case VK_NUMPAD9: {
			if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
				wParam -= 0x30;

			int sel; ::SendMessageW(g_hGhostEdit, EM_GETSEL, (WPARAM)&sel, NULL);

			// 格子已满，跳下一格并转发按键
			if (sel == 2) {
				ActivateGhostEdit(hWnd, ++g_Metric.selectionStart, rcBottomPane);
				::SendMessageW(g_hGhostEdit, WM_KEYDOWN, wParam, lParam);
				MSG m; ::PeekMessageW(&m, hWnd, WM_CHAR, WM_CHAR, PM_REMOVE);
				return 0;
			}

			wchar_t szText[4] = {};
			::GetWindowTextW(hWnd, szText, 4);
			bool changed = false;

			if (sel == 0) {
				if (szText[0] != (wchar_t)wParam) { szText[0] = (wchar_t)wParam; changed = true; }
				if (changed) ::SetWindowTextW(hWnd, szText);
				::SendMessageW(g_hGhostEdit, EM_SETSEL, 1, 0);
			}
			else {
				if (szText[1] != (wchar_t)wParam) { szText[1] = (wchar_t)wParam; changed = true; }
				if (changed) ::SetWindowTextW(hWnd, szText);
				::SendMessage(g_hGhostEdit, EM_SETSEL, -1, 0);
			}

			if (changed) {
				unsigned char v = (unsigned char)nt::whcc(szText);
				MockWriteProcessMemory(g_Metric.editingAddress, &v, 1);
			}

			if (sel == 1) {
				ActivateGhostEdit(hWnd, ++g_Metric.selectionStart, rcBottomPane);
				MSG m; ::PeekMessageW(&m, hWnd, WM_CHAR, WM_CHAR, PM_REMOVE);
			}

			wParam = 0;
			return 1;
		}

		default: break;
		}
		break;

	case WM_KILLFOCUS: {
		// 焦点转移到状态栏的地址框或按钮时，不触发存盘隐藏
		if (g_Metric.isEditing) {
			wchar_t szText[4] = {};
			::GetWindowTextW(hWnd, szText, 4);
			unsigned char v = (unsigned char)nt::whcc(szText);
			MockWriteProcessMemory(g_Metric.editingAddress, &v, 1);
			g_Metric.isEditing = false;
			g_Metric.editingAddress = 0;
			::ShowWindow(hWnd, SW_HIDE);
			::InvalidateRect(::GetParent(hWnd), NULL, FALSE);
		}
		break;
		}
	}

	return ::CallWindowProc(g_OldEditProc, hWnd, uMsg, wParam, lParam);
}
