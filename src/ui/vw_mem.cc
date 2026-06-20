#pragma warning(disable:26454)
#include "hyperce.h"
#include "vw_mem.h"

// ============================================================================
// 全局状态定义
// ============================================================================
HexViewMetrics g_Metric{ 120, 25, 12, 200, 32, 800, 0, 0x00007FFFFFFFFFFF, 0x100000000 };

// ============================================================================
// 一、地址↔位置换算
// ============================================================================
int AddrToPos(uintptr_t addr) {
	double r = (double)(addr - g_Metric.memoryMinAddress) / (double)(g_Metric.memoryMaxAddress - g_Metric.memoryMinAddress);
	return (int)(r * SCROLL_MAX_RANGE);
}
uintptr_t PosToAddr(int pos) {
	double r = (double)pos / (double)SCROLL_MAX_RANGE;
	uintptr_t a = g_Metric.memoryMinAddress + (uintptr_t)(r * (double)(g_Metric.memoryMaxAddress - g_Metric.memoryMinAddress));
	return (a / 16) * 16;
}

// ============================================================================
// UTF-8 解码辅助
// ============================================================================
static int Utf8SeqLen(unsigned char ch) {
	if ((ch & 0x80) == 0x00) return 1;
	if ((ch & 0xE0) == 0xC0) return 2;
	if ((ch & 0xF0) == 0xE0) return 3;
	if ((ch & 0xF8) == 0xF0) return 4;
	return 0;
}

static unsigned DecodeUtf8(const unsigned char* p, int len) {
	if (len == 1) return p[0];
	if (len == 2) {
		if ((p[1] & 0xC0) != 0x80) return 0;
		unsigned cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
		return cp >= 0x80 ? cp : 0;
	}
	if (len == 3) {
		if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
		unsigned cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
		if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
		return cp >= 0x800 ? cp : 0;
	}
	if (len == 4) {
		if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return 0;
		unsigned cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
			((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
		return (cp >= 0x10000 && cp <= 0x10FFFF) ? cp : 0;
	}
	return 0;
}

static int CodePointToWchar(unsigned cp, wchar_t* out) {
	if (cp <= 0xFFFF) {
		out[0] = (wchar_t)cp;
		out[1] = 0;
		return 1;
	}
	cp -= 0x10000;
	out[0] = (wchar_t)(0xD800 + (cp >> 10));
	out[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
	out[2] = 0;
	return 2;
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
	nt::vsnprintf(g_Metric.tmps, -1, fmt, va); va_end(va);
	nt::tow(g_Metric.vw_mem_StateBar, g_Metric.tmps);
	SendMessageW(g_Metric.hStatusBar, SB_SETTEXT, 0 | SBT_OWNERDRAW, (LPARAM)g_Metric.vw_mem_StateBar);
}

// ============================================================================
// 四、坐标↔地址换算
// ============================================================================
uintptr_t GetAddressFromMouse(int mx, int my, uintptr_t pageBase) {
	int row = my / g_Metric.rowHeight;
	if (row < 0) return 0;

	int hexStartX = g_Metric.addressWidth + 20;
	int asciiStartX = hexStartX + g_Metric.bytesPerRow * g_Metric.byteSizeNoChar + 50;
	int asciiEndX = asciiStartX + g_Metric.bytesPerRow * g_Metric.charSize;

	if (mx >= asciiStartX && mx < asciiEndX) {
		int col = (mx - asciiStartX) / g_Metric.charSize;
		col = emin(emax(col, 0), g_Metric.bytesPerRow - 1);
		g_Metric.clickedAscii = true;
		return pageBase + (row * g_Metric.bytesPerRow) + col;
	}

	int localX = mx - hexStartX;
	if (localX < 0) return 0;

	g_Metric.clickedAscii = false;

	int col = 0, curX = 0;
	for (int c = 0; c < g_Metric.bytesPerRow; ++c) {
		int nextX = (c + 1) * g_Metric.byteSizeNoChar + (c / 4) * (g_Metric.charSize / 2);
		if (localX >= curX && localX < nextX) { col = c; break; }
		curX = nextX;
		if (c == g_Metric.bytesPerRow - 1) col = g_Metric.bytesPerRow - 1;
	}
	return pageBase + (row * g_Metric.bytesPerRow) + col;
}

RECT GetRectFromAddress(uintptr_t addr, RECT rcPane) {
	RECT rc = {};
	auto offset = (long long)(addr - g_Metric.pageBaseAddress);
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
void DrawHexViewContent(HDC hdc, RECT rcPane) {
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
		uintptr_t rowAddr = g_Metric.pageBaseAddress + row * g_Metric.bytesPerRow;
		unsigned char data[32] = {};
		bool ok = MockReadProcessMemory(rowAddr, data, g_Metric.bytesPerRow);

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

			bool sel = g_Metric.hasSelection && curAddr >= minSel && curAddr <= maxSel;

			wchar_t szB[4];
			nt::swprintf(szB, ok ? L"%02X" : L"??", bv);

			RECT rcH = { xH, y, xH + g_Metric.byteSizeNoChar, y + g_Metric.rowHeight };
			SetTextColor(hMem, sel ? RGB(255, 0, 0) : RGB(0, 0x7F, 0));
			DrawText(hMem, szB, -1, &rcH, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}

		if (ok) {
			int col = 0;
			while (col < g_Metric.bytesPerRow) {
				unsigned char ch = data[col];
				int seqLen = Utf8SeqLen(ch);

				bool valid = false;
				unsigned cp = 0;
				wchar_t szC[4] = { L'.', 0, 0, 0 };
				int displayCols = 1;

				if (seqLen >= 1 && seqLen <= 4 && (col + seqLen) <= g_Metric.bytesPerRow) {
					cp = DecodeUtf8(&data[col], seqLen);
					if (cp > 0 && cp != 0xFFFD) {
						valid = true;
						displayCols = seqLen;
						if (seqLen == 1) {
							szC[0] = (ch >= 32 && ch <= 126) ? (wchar_t)ch : L'.';
							szC[1] = 0;
						}
						else {
							CodePointToWchar(cp, szC);
						}
					}
				}

				if (!valid) {
					szC[0] = L'.';
					szC[1] = 0;
					displayCols = 1;
				}

				int xA = asciiStartX + col * g_Metric.charSize;
				int xAEnd = asciiStartX + (col + displayCols) * g_Metric.charSize;
				RECT rcC = { xA, y, xAEnd, y + g_Metric.rowHeight };

				bool anySel = false;
				if (g_Metric.hasSelection) {
					for (int k = 0; k < displayCols; ++k) {
						uintptr_t addr = rowAddr + col + k;
						if (addr >= minSel && addr <= maxSel) { anySel = true; break; }
					}
				}

				SetTextColor(hMem, anySel ? RGB(255, 0, 0) :
					(valid && displayCols > 1) ? RGB(100, 200, 255) : RGB(80, 255, 80));
				DrawText(hMem, szC, -1, &rcC, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

				col += displayCols;
			}
		}
		else {
			for (int col = 0; col < g_Metric.bytesPerRow; ++col) {
				int xA = asciiStartX + col * g_Metric.charSize;
				RECT rcC = { xA, y, xA + g_Metric.charSize, y + g_Metric.rowHeight };
				SetTextColor(hMem, RGB(80, 80, 80));
				DrawText(hMem, L"?", -1, &rcC, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
		}
	}

	BitBlt(hdc, rcPane.left, rcPane.top, w, h, hMem, 0, 0, SRCCOPY);
	SelectObject(hMem, hOldF);
	SelectObject(hMem, hOldB);
	DeleteDC(hMem);
	DeleteObject(hBmp);
}

// ============================================================================
// 六、HexView 子窗口过程
// ============================================================================
static LRESULT CALLBACK HexViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {

	case WM_CREATE: {
		SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS,
			0, SCROLL_MAX_RANGE, 300, AddrToPos(g_Metric.pageBaseAddress) };
		::SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
		return 0;
	}

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc; GetClientRect(hwnd, &rc);
		DrawHexViewContent(hdc, rc);
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_ERASEBKGND:
		return 1;

	case WM_VSCROLL: {
		if (g_Metric.isEditing) ::SetFocus(hwnd);
		switch (LOWORD(wParam)) {
		case SB_LINEUP:     g_Metric.pageBaseAddress -= g_Metric.bytesPerRow;      break;
		case SB_LINEDOWN:   g_Metric.pageBaseAddress += g_Metric.bytesPerRow;      break;
		case SB_PAGEUP:     g_Metric.pageBaseAddress -= g_Metric.bytesPerRow * 10; break;
		case SB_PAGEDOWN:   g_Metric.pageBaseAddress += g_Metric.bytesPerRow * 10; break;
		case SB_THUMBTRACK: g_Metric.pageBaseAddress = PosToAddr(HIWORD(wParam));  break;
		}
		g_Metric.pageBaseAddress = emin(emax(g_Metric.pageBaseAddress, g_Metric.memoryMinAddress), g_Metric.memoryMaxAddress);
		SCROLLINFO si = { sizeof(si), SIF_POS, 0, 0, 0, AddrToPos(g_Metric.pageBaseAddress) };
		::SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
		::InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}

	case WM_MOUSEWHEEL: {
		if (g_Metric.isEditing) ::SetFocus(hwnd);
		short d = (short)HIWORD(wParam);
		g_Metric.pageBaseAddress += (d > 0 ? -1 : 1) * g_Metric.bytesPerRow * 3;
		g_Metric.pageBaseAddress = emin(emax(g_Metric.pageBaseAddress, g_Metric.memoryMinAddress), g_Metric.memoryMaxAddress);
		SCROLLINFO si = { sizeof(si), SIF_POS, 0, 0, 0, AddrToPos(g_Metric.pageBaseAddress) };
		::SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
		::InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}

	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
		if (g_Metric.isEditing && ::GetFocus() == g_Metric.hGhostEdit)
			::SetFocus(hwnd);
		uintptr_t a = GetAddressFromMouse(x, y, g_Metric.pageBaseAddress);
		if (a) {
			g_Metric.hasSelection = true;
			g_Metric.selectionStart = a;
			g_Metric.selectionEnd = a;
			::SetCapture(hwnd);
			::InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	}

	case WM_MOUSEMOVE: {
		int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
		if (g_Metric.hasSelection && (wParam & MK_LBUTTON)) {
			uintptr_t a = GetAddressFromMouse(x, y, g_Metric.pageBaseAddress);
			if (a && a != g_Metric.selectionEnd) {
				g_Metric.selectionEnd = a;
				::InvalidateRect(hwnd, NULL, FALSE);
			}
		}
		return 0;
	}

	case WM_LBUTTONUP: {
		if (g_Metric.hasSelection) {
			::ReleaseCapture();
			::SetFocus(hwnd);
			g_Metric.isUpAera = false;

			RECT rcClient; GetClientRect(hwnd, &rcClient);

			if (g_Metric.selectionStart == g_Metric.selectionEnd) {
				g_Metric.hasSelection = false;

				long long addr = 0;
				if (MockReadProcessMemory(g_Metric.selectionStart, &addr, 8)) {
					ActivateGhostEdit(hwnd, g_Metric.selectionStart, rcClient);

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
		return 0;
	}

	case WM_KEYDOWN:
		return SendMessage(GetParent(hwnd), msg, wParam, lParam);

	case WM_KEYUP:
		return SendMessage(GetParent(hwnd), msg, wParam, lParam);

	case WM_CTLCOLOREDIT:
		if ((HWND)lParam == g_Metric.hGhostEdit) {
			SetBkColor((HDC)wParam, RGB(0, 120, 215));
			SetTextColor((HDC)wParam, RGB(255, 255, 255));
			return (INT_PTR)nt::darkbrush();
		}
		break;

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// 七、布局（保留分割条，无闪烁）
// ============================================================================
void LayoutControls(HWND hwnd) {
	RECT rc;
	GetClientRect(hwnd, &rc);

	int statusH = 0;
	if (g_Metric.hStatusBar && IsWindowVisible(g_Metric.hStatusBar)) {
		RECT rs; GetWindowRect(g_Metric.hStatusBar, &rs);
		statusH = rs.bottom - rs.top;
	}

	// 使用 DeferWindowPos 批量移动，减少重绘次数
	HDWP hdwp = BeginDeferWindowPos(2);

	if (g_Metric.hListView) {
		hdwp = DeferWindowPos(hdwp, g_Metric.hListView, NULL,
			rc.left, rc.top, rc.right - rc.left, g_Metric.upperHeight,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	if (g_Metric.hHexView) {
		int top = g_Metric.upperHeight + SPLITTER_HEIGHT;
		int h = rc.bottom - top - statusH;
		if (h < 0) h = 0;
		hdwp = DeferWindowPos(hdwp, g_Metric.hHexView, NULL,
			rc.left, top, rc.right - rc.left, h,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	EndDeferWindowPos(hdwp);

	// Edit 和 Button 在 hStatusBar 客户区内定位
	if (g_Metric.hStatusEdit && g_Metric.hStatusBtn && g_Metric.hStatusBar) {
		RECT rsb; GetClientRect(g_Metric.hStatusBar, &rsb);
		const int btnW = nt::cdpi(32);
		const int editW = nt::cdpi(160);
		const int pad = nt::cdpi(3);
		const int ctrlH = rsb.bottom - pad * 2;
		const int ctrlY = pad;

		MoveWindow(g_Metric.hStatusBtn,
			rsb.right - btnW - pad, ctrlY,
			btnW, ctrlH, TRUE);
		MoveWindow(g_Metric.hStatusEdit,
			rsb.right - btnW - editW - pad * 2, ctrlY,
			editW, ctrlH, TRUE);
	}
}

// ============================================================================
// 八、幽灵 Edit 激活
// ============================================================================
void ActivateGhostEdit(HWND hParent, uintptr_t addr, RECT rcPane) {
	if (!addr || !g_Metric.hGhostEdit) return;

	if (g_Metric.isEditing) {
		g_Metric.isEditing = false;
		::ShowWindow(g_Metric.hGhostEdit, SW_HIDE);
	}

	RECT rcT = GetRectFromAddress(addr, rcPane);
	if (!rcT.left && !rcT.top && !rcT.right) return;

	g_Metric.isEditing = true;
	g_Metric.editingAddress = addr;

	if (::GetParent(g_Metric.hGhostEdit) != hParent)
		::SetParent(g_Metric.hGhostEdit, hParent);

	::MoveWindow(g_Metric.hGhostEdit,
		rcT.left - 2, rcT.top + 1,
		(rcT.right - rcT.left) - 4,
		(rcT.bottom - rcT.top) - 2,
		TRUE);

	unsigned char b = 0;
	MockReadProcessMemory(addr, &b, 1);

	wchar_t sz[8];
	nt::swprintf(sz, L"%02X", b);

	::SetWindowTextW(g_Metric.hGhostEdit, sz);
	::SendMessageW(g_Metric.hGhostEdit, EM_LIMITTEXT, 2, 0);
	::ShowWindow(g_Metric.hGhostEdit, SW_SHOW);
	::SetFocus(g_Metric.hGhostEdit);
	::SendMessage(g_Metric.hGhostEdit, EM_SETSEL, -1, -1);

	::InvalidateRect(hParent, NULL, FALSE);
}

// ============================================================================
// 九、菜单与辅助
// ============================================================================

static BOOL on_wm_drawitem(LPDRAWITEMSTRUCT di) {
	if (di->hwndItem == g_Metric.hStatusBtn) {
		HDC  hdc = di->hDC;
		RECT rect = di->rcItem;
		bool pressed = (di->itemState & ODS_SELECTED) != 0;

		FillRect(hdc, &rect, (HBRUSH)nt::darkbrush());

		HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0x70, 0x70, 0x70));
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
		HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
		Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
		SelectObject(hdc, hOldPen); SelectObject(hdc, hOldBr); DeleteObject(hPen);

		if (pressed) OffsetRect(&rect, 1, 1);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(220, 220, 220));
		SelectObject(hdc, g_Metric.hFont);
		DrawText(hdc, L"\u2192", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}

	if (di->hwndItem == g_Metric.hStatusBar) {
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

void on_select_process(HANDLE pid) {
	DAS("SEL %u D", pid);
	nt::pset(pid);
}

void on_show_process_list_menu(HMENU hPopup) {
	static HMENU hPop;

	while (GetMenuItemCount(hPopup) > 0)
		RemoveMenu(hPopup, 0, MF_BYPOSITION);

	hPop = hPopup;
	Nt::EnumProcETS(+[](PSYSTEM_PROCESS_INFORMATION_NS p) {
		if (nt::wcsstr(p->ImageName.Buffer, L"svchost")) return true;
		if (nt::wcsstr(p->ImageName.Buffer, L"msedge")) return true;
		if (nt::wcsstr(p->ImageName.Buffer, L"chrome")) return true;
		if (nt::wcsstr(p->ImageName.Buffer, L"vcpkg")) return true;

		HANDLE hp = Nt::OpenProc(p->UniqueProcessId, PROCESS_QUERY_INFORMATION);
		AppendMenuW(hPop,
			MF_STRING | (hp ? (p->UniqueProcessId == nt::pidh() ? MF_CHECKED : 0) : MF_DISABLED),
			0x88000000 | (UINT)(INT_PTR)p->UniqueProcessId,
			nt::wsprint(G->bHexMode ? L"%X %s" : L"%d %s", p->UniqueProcessId,
				p->ImageName.Buffer ? p->ImageName.Buffer : L"")
		);
		if (hp) nt::ntClose(hp);
		return true;
		});
}

void on_show_module_list_menu(HMENU hPopup) {
	static HMENU hPopModule;
	static unsigned uPop;
	if (!nt::ph()) return;

	while (GetMenuItemCount(hPopup) > 0)
		RemoveMenu(hPopup, 0, MF_BYPOSITION);

	hPopModule = hPopup; uPop = 0;
	nt::pmodules(+[](LDR_DATA_TABLE_ENTRY_NS* p) {
		AppendMenuW(hPopModule,
			MF_STRING, 0x89000000 | uPop++,
			nt::wsprint(L"[%llX~%llX] %s", p->DllBase, (char*)p->DllBase + p->SizeOfImage, p->BaseDllName.Buffer)
		);
		return true;
		});
}

HMENU MmviewMenu() {
	HMENU hMenuBar = CreateMenu();

	HMENU hList = CreatePopupMenu();
	HMENU hModules = CreatePopupMenu();
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hList, L"进程(&L)");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hModules, L"模块(&M)");

	HMENU hFunc = CreatePopupMenu();
	AppendMenuW(hFunc, MF_STRING, IDM_PROC_HEXPID, L"Hex (&H)");
	AppendMenuW(hFunc, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFunc, MF_STRING, IDM_PROC_SUPEND, L"挂起+1(&S)");
	AppendMenuW(hFunc, MF_STRING, IDM_PROC_RESUME, L"恢复-1(&R)");
	AppendMenuW(hFunc, MF_STRING, IDM_PROC_PAUSE, L"暂停(PS)");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFunc, L"功能(&F)");

	AppendMenuW(hMenuBar, MF_STRING, IDM_STEP_BREAK, L"断点(F9)");
	AppendMenuW(hMenuBar, MF_STRING, IDM_STEP_SINGLE, L"单步(F10)");
	AppendMenuW(hMenuBar, MF_STRING, IDM_STEP_ENTER, L"进入(F11)");
	AppendMenuW(hMenuBar, MF_STRING, IDM_STEP_OUT, L"跳出 >");

	return hMenuBar;
}

static void RegisterHexViewClass() {
	WNDCLASSEX wc = { sizeof(wc) };
	wc.lpfnWndProc = HexViewWndProc;
	wc.hInstance = NtCurrentImageBase();
	wc.lpszClassName = L"HexViewChild";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.style = CS_DBLCLKS;
	RegisterClassExW(&wc);
}

// ============================================================================
// 十、WM_CREATE
// ============================================================================
static void on_wm_create(HWND hwnd) {
	Nt::EnableDarkModeDwm(hwnd);
	::SetMenu(hwnd, MmviewMenu());

	HDC hdc = GetDC(hwnd);
	g_Metric.hFont = CreateFont(nt::cdpi(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

	// 注册并创建 HexView 子窗口
	RegisterHexViewClass();

	RECT rcC; GetClientRect(hwnd, &rcC);
	g_Metric.hHexView = CreateWindowExW(
		0, L"HexViewChild", NULL,
		WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		0, g_Metric.upperHeight + SPLITTER_HEIGHT,
		rcC.right, rcC.bottom - g_Metric.upperHeight - SPLITTER_HEIGHT,
		hwnd, (HMENU)1010, NtCurrentImageBase(), NULL);
	if (nt::darkmode())
		Nt::UxSetWindowDarkMode(g_Metric.hHexView);

	// 幽灵 Edit
	g_Metric.hGhostEdit = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT, L"EDIT", L"",
		WS_CHILD | ES_UPPERCASE | ES_AUTOHSCROLL,
		0, 0, 0, 0, g_Metric.hHexView, (HMENU)1002, NtCurrentImageBase(), NULL);

	LONG_PTR sty = GetWindowLongPtr(g_Metric.hGhostEdit, GWL_STYLE);
	SetWindowLongPtr(g_Metric.hGhostEdit, GWL_STYLE, sty & ~WS_BORDER);
	::SendMessage(g_Metric.hGhostEdit, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);
	g_Metric.OldEditProc = (WNDPROC)::SetWindowLongPtr(g_Metric.hGhostEdit, GWLP_WNDPROC, (LONG_PTR)GhostEditSubclassProc);

	// ListView
	g_Metric.hListView = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
		WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA,
		0, 0, rcC.right, g_Metric.upperHeight, hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);

	if (nt::darkmode())
		Nt::UxSetWindowDarkMode(g_Metric.hListView);
	g_Metric.hHeader = ListView_GetHeader(g_Metric.hListView);

	HIMAGELIST hIL = ImageList_Create(1, nt::cdpi(22), ILC_COLOR32, 1, 1);
	ListView_SetImageList(g_Metric.hListView, hIL, LVSIL_SMALL);

	HFONT hHdrFont = CreateFont(nt::cdpi(22), 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
	SendMessage(g_Metric.hHeader, WM_SETFONT, (WPARAM)hHdrFont, TRUE);

	COLORREF bg = RGB(0x20, 0x20, 0x20);
	ListView_SetBkColor(g_Metric.hListView, bg);
	ListView_SetTextBkColor(g_Metric.hListView, bg);
	ListView_SetTextColor(g_Metric.hListView, RGB(255, 255, 255));

	SetWindowSubclass(g_Metric.hListView,
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

	ListView_SetExtendedListViewStyle(g_Metric.hListView, LVS_EX_FULLROWSELECT);

	auto AddCol = [](int i, int cx, LPCWSTR text) {
		LVCOLUMN c = { LVCF_WIDTH | LVCF_TEXT, 0, cx, (LPWSTR)text };
		ListView_InsertColumn(g_Metric.hListView, i, &c);
		};
	AddCol(0, 580, L"地址 Address");
	AddCol(1, 380, L"字节 Bytes");
	AddCol(2, 360, L"指令 Asm");
	AddCol(3, 1020, L"注释 Note");

	ListView_SetItemCount(g_Metric.hListView, 1000000);
	ListView_SetItemState(g_Metric.hListView, -1, 0, LVIS_SELECTED);
	ListView_EnsureVisible(g_Metric.hListView, 500010, FALSE);
	ListView_SetItemState(g_Metric.hListView, 500000,
		LVNI_SELECTED | LVNI_FOCUSED, LVNI_SELECTED | LVNI_FOCUSED);
	SetFocus(g_Metric.hListView);

	// 状态栏
	g_Metric.hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL,
		WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, hwnd, NULL, NtCurrentImageBase(), NULL);
	SendMessageW(g_Metric.hStatusBar, SB_SETTEXT, 0 | SBT_OWNERDRAW, (LPARAM)L"就绪");

	g_Metric.hStatusEdit = CreateWindowExW(
		0, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_UPPERCASE | ES_AUTOHSCROLL,
		0, 0, 0, 0, g_Metric.hStatusBar, (HMENU)IDC_STATUS_EDIT, NtCurrentImageBase(), NULL);
	SendMessage(g_Metric.hStatusEdit, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);

	g_Metric.hStatusBtn = CreateWindowExW(
		0, L"BUTTON", L"\u2192",
		WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
		0, 0, 0, 0, g_Metric.hStatusBar, (HMENU)IDC_STATUS_BTN, NtCurrentImageBase(), NULL);
	SendMessage(g_Metric.hStatusBtn, WM_SETFONT, (WPARAM)g_Metric.hFont, TRUE);

	SetWindowSubclass(g_Metric.hStatusBar, [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) -> LRESULT {
		if (uMsg == WM_COMMAND || uMsg == WM_CTLCOLOREDIT ||
			(uMsg == WM_DRAWITEM && LPDRAWITEMSTRUCT(lParam)->hwndItem == g_Metric.hStatusBtn))
			return SendMessage(GetParent(hWnd), uMsg, wParam, lParam);
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
		}, 1, 0);
}

// ============================================================================
// 十一、主窗口过程
// ============================================================================
int ui_mmview(void*) {
	int w = nt::cdpi(WIN_WIDTH), h = nt::cdpi(WIN_HEIGHT);
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

	case WM_ERASEBKGND:
		return 1;

	case WM_DRAWITEM:
		if (on_wm_drawitem((LPDRAWITEMSTRUCT)lParam))
			return TRUE;
		break;

	case WM_NOTIFY: {
		auto* hdr = (LPNMHDR)lParam;
		if (hdr->hwndFrom != g_Metric.hListView) break;

		if (hdr->code == LVN_ITEMCHANGED) {
			SetStateText("选中：第 %d 行", ((LPNMLISTVIEW)lParam)->iItem);
			g_Metric.isUpAera = true;
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
				ListView_GetItemText(g_Metric.hListView, (int)lv->nmcd.dwItemSpec, lv->iSubItem, text, 128);
				bool isNum = text[0] != 0;
				for (int i = 0; text[i]; i++) if (!nt::iswdigit(text[i])) { isNum = false; break; }
				lv->clrText = isNum ? RGB(0, 255, 0) : RGB(255, 255, 255);
				lv->clrTextBk = RGB(0x20, 0x20, 0x20);
				if (lv->iSubItem == 2) {
					static HFONT hFontBold;
					if (!hFontBold)
						hFontBold = CreateFont(nt::cdpi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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
		//DDS("KEYDOWN: %c", wParam);
		break;

	case WM_KEYUP:
		if (wParam == 'C' && (::GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
			if (!g_Metric.hasSelection) break;
			uintptr_t lo = emin(g_Metric.selectionStart, g_Metric.selectionEnd);
			uintptr_t hi = emax(g_Metric.selectionStart, g_Metric.selectionEnd);
			size_t    cnt = hi - lo + 1;
			char* p, * buf = (char*)nt::valloc(cnt << 2);
			if (!buf) break; p = buf;
			for (uintptr_t a = lo; a <= hi; ++a) {
				unsigned char v = 0;
				if (!MockReadProcessMemory(a, &v, 1)) { *(unsigned*)p = *(unsigned*)"?? "; p += 3; continue; }
				p += nt::sprintf(p, "%02X ", v);
			}
			if (p > buf) *(p - 1) = '\0';

			if (nt::setclip(buf, hwnd))
				SetStateText("复制->%d个字节", cnt);

			nt::vfree(buf);
		}
		break;

	case WM_SIZE:
		SendMessageW(g_Metric.hStatusBar, WM_SIZE, 0, 0);
		GetClientRect(hwnd, &rcClient);
		if (g_Metric.upperHeight > rcClient.bottom - SPLITTER_HEIGHT - 50)
			g_Metric.upperHeight = rcClient.bottom - SPLITTER_HEIGHT - 50;
		LayoutControls(hwnd);
		{
			RECT spl = { rcClient.left, g_Metric.upperHeight, rcClient.right, g_Metric.upperHeight + SPLITTER_HEIGHT };
			InvalidateRect(hwnd, &spl, FALSE);
		}
		break;

	case WM_CTLCOLOREDIT:
		if ((HWND)lParam == g_Metric.hStatusEdit) {
			SetBkColor((HDC)wParam, RGB(0x30, 0x30, 0x30));
			SetTextColor((HDC)wParam, RGB(255, 255, 255));
			return (INT_PTR)nt::darkbrush();
		}
		break;

	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
		if (y >= g_Metric.upperHeight && y < g_Metric.upperHeight + SPLITTER_HEIGHT) {
			g_Metric.draggingSplitter = true;
			SetCapture(hwnd);
		}
		break;
	}

	case WM_MOUSEMOVE: {
		int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
		if (g_Metric.draggingSplitter) {
			GetClientRect(hwnd, &rcClient);
			y = emin(emax(y, 50), rcClient.bottom - SPLITTER_HEIGHT - 50);
			if (y == g_Metric.upperHeight) break;

			// 失效旧分割条
			RECT splOld = { rcClient.left, g_Metric.upperHeight, rcClient.right, g_Metric.upperHeight + SPLITTER_HEIGHT };
			InvalidateRect(hwnd, &splOld, FALSE);

			g_Metric.upperHeight = y;
			LayoutControls(hwnd);

			// 失效新分割条
			RECT splNew = { rcClient.left, g_Metric.upperHeight, rcClient.right, g_Metric.upperHeight + SPLITTER_HEIGHT };
			InvalidateRect(hwnd, &splNew, FALSE);
			UpdateWindow(hwnd);

			// HexView 子窗口也需要重绘 
			if (g_Metric.hHexView)
				InvalidateRect(g_Metric.hHexView, NULL, FALSE);
		}
		else if (y >= g_Metric.upperHeight && y < g_Metric.upperHeight + SPLITTER_HEIGHT)
			G->bakcur = SetCursor((HCURSOR)G->crVSplit);
		else if (GetCursor() != G->crDefault)
			SetCursor((HCURSOR)G->crDefault);
		break;
	}

	case WM_LBUTTONUP:
		if (g_Metric.draggingSplitter) {
			g_Metric.draggingSplitter = false;
			ReleaseCapture();

		}
		break;

	case WM_INITMENUPOPUP: {
		if (LOWORD(lParam) == 0) {
			on_show_process_list_menu((HMENU)wParam);
			return 0;
		}
		if (LOWORD(lParam) == 1) {
			on_show_module_list_menu((HMENU)wParam);
			return 0;
		}
		break;
	}

	case WM_COMMAND:
		if (wParam >> 24 == 0x88) {
			wParam &= 0xFFFFFF;
			on_select_process((HANDLE)wParam);
			return 0;
		}
		switch (LOWORD(wParam)) {

		case IDM_PROC_SUPEND:
		case IDM_PROC_RESUME: {
			if (nt::pidh() == ntpid()) return 0;
			if (LOWORD(wParam) == IDM_PROC_SUPEND)
				nt::ntSuspendProcess(nt::ph());
			else
				nt::ntResumeProcess(nt::ph());
			return 0;
		}
		case IDC_STATUS_BTN: {
			wchar_t szAddr[32] = { 0 };
			GetWindowTextW(g_Metric.hStatusEdit, szAddr, 32);
			uintptr_t addr = 0;
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
				g_Metric.pageBaseAddress = (addr / 16) * 16;
				g_Metric.pageBaseAddress = emin(emax(g_Metric.pageBaseAddress, g_Metric.memoryMinAddress), g_Metric.memoryMaxAddress);
				SCROLLINFO si = { sizeof(si), SIF_POS, 0, 0, 0, AddrToPos(g_Metric.pageBaseAddress) };
				::SetScrollInfo(g_Metric.hHexView, SB_VERT, &si, TRUE);
				::InvalidateRect(g_Metric.hHexView, NULL, FALSE);
				SetStateText("跳转到 %llX", (long long)g_Metric.pageBaseAddress);
			}
			return 0;
		}
		case IDM_PROC_PAUSE: {
			HMENU hMenu = GetMenu(hwnd);
			UINT state = GetMenuState(hMenu, IDM_PROC_PAUSE, MF_BYCOMMAND);
			CheckMenuItem(hMenu, IDM_PROC_PAUSE, MF_BYCOMMAND | ((state & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
			return 0;
		}
		case IDM_PROC_HEXPID: {
			HMENU hMenu = GetMenu(hwnd);
			UINT state = GetMenuState(hMenu, IDM_PROC_HEXPID, MF_BYCOMMAND);
			if (state & MF_CHECKED) {
				CheckMenuItem(hMenu, IDM_PROC_HEXPID, MF_BYCOMMAND | MF_UNCHECKED);
				G->bHexMode = 0;
			}
			else {
				CheckMenuItem(hMenu, IDM_PROC_HEXPID, MF_BYCOMMAND | MF_CHECKED);
				G->bHexMode = 1;
			}
			return 0;
		}
		case IDC_STATUS_EDIT:
			if (HIWORD(wParam) == EN_CHANGE) {}
			break;
		default: break;
		}
		break;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		GetClientRect(hwnd, &rcClient);
		// 只绘制分割条
		RECT spl = { rcClient.left, g_Metric.upperHeight, rcClient.right, g_Metric.upperHeight + SPLITTER_HEIGHT };
		if (RectVisible(hdc, &spl))
			FillRect(hdc, &spl, (HBRUSH)GetStockObject(GRAY_BRUSH));
		EndPaint(hwnd, &ps);
		break;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_UAHDRAWMENU:
		if (!G->bDarkMode) break;
		return _ewndi::ON_WM_UAHDRAWMENU(hwnd, (UAHMENU*)lParam);

	case WM_UAHDRAWMENUITEM:
		if (!G->bDarkMode) break;
		return _ewndi::ON_WM_UAHDRAWMENUITEM((UAHDRAWMENUITEM*)lParam);

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

// ============================================================================
// 十二、幽灵 Edit 子类化过程
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
			int sel; ::SendMessageW(g_Metric.hGhostEdit, EM_GETSEL, (WPARAM)&sel, NULL);
			if (sel == 1) break;
			RECT rcClient; GetClientRect(::GetParent(hWnd), &rcClient);
			if (sel == 0 && wParam == VK_LEFT) {
				ActivateGhostEdit(::GetParent(hWnd), --g_Metric.selectionStart, rcClient);
				::SendMessage(g_Metric.hGhostEdit, EM_SETSEL, 2, 2);
				return 0;
			}
			if (sel == 2 && wParam == VK_RIGHT) {
				ActivateGhostEdit(::GetParent(hWnd), ++g_Metric.selectionStart, rcClient);
				::SendMessage(g_Metric.hGhostEdit, EM_SETSEL, 0, 0);
				return 0;
			}
			break;
		}

		case VK_UP: case VK_DOWN: {
			int sel;
			::SendMessageW(g_Metric.hGhostEdit, EM_GETSEL, (WPARAM)&sel, NULL);
			int off = (wParam == VK_UP ? -16 : 16) << 1;
			uintptr_t newAddr = g_Metric.editingAddress += off;
			g_Metric.selectionStart += off;
			RECT rcClient; GetClientRect(::GetParent(hWnd), &rcClient);
			ActivateGhostEdit(::GetParent(hWnd), newAddr, rcClient);
			::SendMessageW(g_Metric.hGhostEdit, EM_SETSEL, sel, sel);
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

			int sel; ::SendMessageW(g_Metric.hGhostEdit, EM_GETSEL, (WPARAM)&sel, NULL);

			if (sel == 2) {
				RECT rcClient; GetClientRect(::GetParent(hWnd), &rcClient);
				ActivateGhostEdit(::GetParent(hWnd), ++g_Metric.selectionStart, rcClient);
				::SendMessageW(g_Metric.hGhostEdit, WM_KEYDOWN, wParam, lParam);
				nt::remove_char_msg(hWnd);
				return 0;
			}

			wchar_t szText[4] = {};
			::GetWindowTextW(hWnd, szText, 4);
			bool changed = false;

			if (sel == 0) {
				if (szText[0] != (wchar_t)wParam) { szText[0] = (wchar_t)wParam; changed = true; }
				if (changed) ::SetWindowTextW(hWnd, szText);
				::SendMessageW(g_Metric.hGhostEdit, EM_SETSEL, 1, 1);
				nt::remove_char_msg(hWnd);
			}
			else if (sel == 1) {
				if (szText[1] != (wchar_t)wParam) { szText[1] = (wchar_t)wParam; changed = true; }
				if (changed) ::SetWindowTextW(hWnd, szText);
				::SendMessage(g_Metric.hGhostEdit, EM_SETSEL, -1, 0);
			}

			if (changed) {
				auto& v = *szText;
				v = (unsigned char)nt::whcc(szText);
				MockWriteProcessMemory(g_Metric.editingAddress, &v, 1);
			}

			if (sel == 1) {
				RECT rcClient; GetClientRect(::GetParent(hWnd), &rcClient);
				ActivateGhostEdit(::GetParent(hWnd), ++g_Metric.selectionStart, rcClient);
				nt::remove_char_msg(hWnd);
			}

			wParam = 0;
			return 1;
		}

		default: break;
		}
		break;

	case WM_KILLFOCUS: {
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

	return ::CallWindowProc(g_Metric.OldEditProc, hWnd, uMsg, wParam, lParam);
}

int ui_show_mmview(void*) {
	nt::newthr(ui_mmview);
	return 0;
}