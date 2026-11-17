#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: TrayMenu.cpp (WP-E). Spec 1.1, 3.7.
#include <windows.h>

enum : UINT { kCmdStartup = 100, kCmdExport = 101, kCmdExit = 102 };

// Menu contains exactly: "Start with Windows" (checked if startupChecked), separator, "Export to Excel", "Exit".
// SetForegroundWindow(owner) + TrackPopupMenuEx(TPM_RETURNCMD | TPM_RIGHTBUTTON | alignment for pt)
// + PostMessageW(owner, WM_NULL). Returns the command id, or 0 if dismissed.
UINT ShowTrayMenu(HWND owner, POINT screenPt, bool startupChecked);
