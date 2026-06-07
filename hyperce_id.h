#ifndef _HYPERCE_ID_
# define _HYPERCE_ID_
# define TRAY_VERSION L"v1" //  [5/15/2026]
# define TRAY_CAPTION L"HyperCE"
# define MMVIEW_CAPTION L"MmView"
# define FONT_HEIGHT 36

# define WND_W 760
# define WND_H	850

# define ID_TRAY_ICON			0x1001
# define ID_MENU_HELLO		0x1002
# define ID_MENU_CANCEL	0x1003
# define ID_MENU_EXIT			0x1005

# define ID_MENU_CK0			0x1010
# define ID_MENU_CK1			0x1011
# define ID_MENU_CK2			0x1012

// ─────────────────────────────────────────────
//  菜单 ID 定义
// ─────────────────────────────────────────────

// 文件(F)
#define IDM_FILE_NEW				0x1021
#define IDM_FILE_OPEN				0x1022
#define IDM_FILE_SAVE				0x1023
#define IDM_FILE_SAVE_AS			0x1024
#define IDM_FILE_EXIT				0x1025

// 编辑(E)
#define IDM_EDIT_UNDO			0x1101
#define IDM_EDIT_REDO				0x1102
#define IDM_EDIT_CUT				0x1103
#define IDM_EDIT_COPY				0x1104
#define IDM_EDIT_PASTE			0x1105
#define IDM_EDIT_SELECT_ALL	0x1106

// 帮助(H)
#define IDM_HELP_ABOUT			0x1201
#define IDM_HELP_DOCS			0x1202

#define IDC_STATUS_EDIT 6000
#define IDC_STATUS_BTN 6001

# define WM_TRAYICON (/*WM_USER*/0x400 + 1)
#endif // 