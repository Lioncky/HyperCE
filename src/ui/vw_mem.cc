#include "hyperce.h"
// name=MemoryViewer.cpp
#include <windows.h>

#include <commctrl.h> 
#pragma comment(lib, "comctl32.lib")

#define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))

#define WIN_WIDTH  800
#define WIN_HEIGHT 600
#define IDC_LIST   1001

const int SPLITTER_HEIGHT = 6;
int upperHeight = 200; // 可调整
bool draggingSplitter = false;
POINT dragStart = { 0 };

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int ui_show_mmview(void*) {

	HWND hwnd = Nt::CreateWnd(MMVIEW_CAPTION, MMVIEW_CAPTION, WndProc);
	int x = (GetSystemMetrics(SM_CXSCREEN) - WIN_WIDTH) / 2;
	int y = (GetSystemMetrics(SM_CYSCREEN) - WIN_HEIGHT) / 2;

	SetWindowPos(hwnd, 0, x, y,
		WIN_WIDTH, WIN_HEIGHT,
		SWP_SHOWWINDOW);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg); DispatchMessage(&msg);
	}
	return 0;
}


HWND hListView = NULL;
const char * assemblyRows[] = {
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

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void LayoutControls(HWND hwnd);
void DrawBottomPane(HDC hdc, RECT rcClient);
HWND g_hHeader;
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static RECT rcClient;
	switch (msg) {
	case WM_CREATE: {
		Nt::EnableDarkModeDwm(hwnd);

		GetClientRect(hwnd, &rcClient);

		hListView = CreateWindowEx(
			WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL,
			WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
			0, 0, rcClient.right, upperHeight,
			hwnd, (HMENU)0, GetModuleHandle(NULL), NULL);

		g_hHeader = ListView_GetHeader(hListView);

		COLORREF bg = RGB(0x20, 0x20, 0x20);
		ListView_SetBkColor(hListView, bg);
		ListView_SetTextBkColor(hListView, bg);
		ListView_SetTextColor(hListView, RGB(255, 255, 255));

		Nt::UxSetWindowTheme(hListView, L"DarkMode_Explorer", NULL);
		//Nt::UxSetWindowTheme(hListView, L"", L"");

		// 两列: 地址、指令
		LVCOLUMN lvc = { 0 };
		lvc.mask = LVCF_WIDTH | LVCF_TEXT;
		lvc.cx = 100;
		lvc.pszText = (LPWSTR)L"地址";
		ListView_InsertColumn(hListView, 0, &lvc);
		lvc.cx = 200;
		lvc.pszText = (LPWSTR)L"字节";
		ListView_InsertColumn(hListView, 1, &lvc);
		lvc.cx = 150;
		lvc.pszText = (LPWSTR)L"指令";
		ListView_InsertColumn(hListView, 2, &lvc);
		lvc.cx = 200;
		lvc.pszText = (LPWSTR)L"注释";
		ListView_InsertColumn(hListView, 3, &lvc);

		// 加10行示例
		for (int i = 0; i < (int)10; ++i) {
			LVITEMW lvi = {};
			lvi.mask = LVIF_TEXT;
			static wchar_t addr[32], asmcode[68];
			nt::tow(asmcode, assemblyRows[i]);

			lvi.iItem = i;

			nt::tow(addr, assemblyRows[i]); addr[8] = 0;
			lvi.pszText = (LPWSTR)addr;
			ListView_InsertItem(hListView, &lvi);
			ListView_SetItemText(hListView, i, 2, asmcode + 9); // skip address+2空格
		}
		// 列表美化
		ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT);
		break;
	}
	case WM_NOTIFY:
	{
		LPNMHDR hdr = (LPNMHDR)lParam;

		if (hdr->hwndFrom == g_hHeader) {

			switch (hdr->code)
			{
			case NM_CUSTOMDRAW:
			{
				LPNMCUSTOMDRAW lpDraw = (LPNMCUSTOMDRAW)lParam;

				switch (lpDraw->dwDrawStage)
				{
				case CDDS_PREPAINT:
					return CDRF_NOTIFYITEMDRAW;

				case CDDS_ITEMPREPAINT:
				{
					HDC hdc = lpDraw->hdc;
					RECT rc = lpDraw->rc;

					// 背景
					HBRUSH hBrush = CreateSolidBrush(RGB(0x20, 0x20, 0x20));
					FillRect(hdc, &rc, hBrush);
					DeleteObject(hBrush);

					// 白字
					SetTextColor(hdc, RGB(255, 255, 255));
					SetBkMode(hdc, TRANSPARENT);

					WCHAR text[128];

					HDITEMW item = { 0 };
					item.mask = HDI_TEXT;
					item.pszText = text;
					item.cchTextMax = 128;

					Header_GetItem(g_hHeader,
						(int)lpDraw->dwItemSpec,
						&item);

					DrawTextW(
						hdc,
						text,
						-1,
						&rc,
						DT_CENTER |
						DT_VCENTER |
						DT_SINGLELINE
					);

					return CDRF_SKIPDEFAULT;
				}
				}
			}
			}
		}
		if (hdr->hwndFrom == hListView)
		{
			if (hdr->code == LVN_ITEMCHANGED)
				InvalidateRect(hwnd, NULL, TRUE);

			else if (hdr->code == NM_CUSTOMDRAW)
			{
				LPNMLVCUSTOMDRAW lv =
					(LPNMLVCUSTOMDRAW)lParam;

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
						hListView,
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

					return CDRF_DODEFAULT;
				}
				}
			}
		}

		break;
	}
	case WM_SIZE:
		GetClientRect(hwnd, &rcClient);
		if (upperHeight > rcClient.bottom - SPLITTER_HEIGHT - 50)
			upperHeight = rcClient.bottom - SPLITTER_HEIGHT - 50;
		LayoutControls(hwnd);
		InvalidateRect(hwnd, NULL, TRUE);
		break;
	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam);
		int y = GET_Y_LPARAM(lParam);
		if (y > upperHeight && y < upperHeight + SPLITTER_HEIGHT) {
			draggingSplitter = true;
			SetCapture(hwnd);
		}
		break;
	}
	case WM_MOUSEMOVE:
		if (draggingSplitter) {
			int y = GET_Y_LPARAM(lParam);
			GetClientRect(hwnd, &rcClient);
			int minHeight = 50;
			int maxHeight = rcClient.bottom - SPLITTER_HEIGHT - minHeight;
			if (y < minHeight) y = minHeight;
			if (y > maxHeight) y = maxHeight;
			upperHeight = y;
			LayoutControls(hwnd);
			InvalidateRect(hwnd, NULL, TRUE);
		}
		break;
	case WM_LBUTTONUP:
		if (draggingSplitter) {
			draggingSplitter = false;
			ReleaseCapture();
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
		for (int i = -1; i <= 1; ++i) {
			MoveToEx(hdc, cx - 12, upperHeight + (SPLITTER_HEIGHT / 2) + i * 2, NULL);
			LineTo(hdc, cx + 12, upperHeight + (SPLITTER_HEIGHT / 2) + i * 2);
		}
		// 下半内容区演示与选中行联动
		DrawBottomPane(hdc, rcClient);
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
	MoveWindow(hListView, rc.left, rc.top, rc.right - rc.left, upperHeight, TRUE);
}

// 下区：显示选中行的详情
void DrawBottomPane(HDC hdc, RECT rcClient) {
	RECT botRect = rcClient;
	botRect.top = upperHeight + SPLITTER_HEIGHT;
	//FillRect(hdc, &botRect, (HBRUSH)(COLOR_WINDOW + 1));
	HFONT hFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
	HFONT hOld = (HFONT)SelectObject(hdc, hFont);

	int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
	//std::string text = (sel != -1 && sel < (int)assemblyRows.size()) ?
	//	std::string("选中：") + assemblyRows[sel] : "请从上方选择一行";
	TextOutA(hdc, botRect.left + 20, botRect.top + 20, assemblyRows[sel], 6);
	SelectObject(hdc, hOld);
}
