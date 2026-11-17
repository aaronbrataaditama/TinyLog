// Spec 3.7. The menu is built per call and destroyed afterwards.
#include "win32/TrayMenu.h"

UINT ShowTrayMenu(HWND owner, POINT screenPt, bool startupChecked) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;

    UINT result = 0;
    const bool built =
        AppendMenuW(menu, MF_STRING | (startupChecked ? MF_CHECKED : MF_UNCHECKED), kCmdStartup,
                    L"Start with Windows") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) &&
        AppendMenuW(menu, MF_STRING, kCmdExport, L"Export to Excel") &&
        AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");

    if (built) {
        // Required so the menu dismisses when the user clicks elsewhere.
        SetForegroundWindow(owner);
        const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        const BOOL cmd = TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | align,
                                          screenPt.x, screenPt.y, owner, nullptr);
        result = static_cast<UINT>(cmd);
        PostMessageW(owner, WM_NULL, 0, 0);
    }

    DestroyMenu(menu);
    return result;
}
