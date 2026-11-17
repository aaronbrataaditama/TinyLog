#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: TrayIcon.cpp (WP-E). Spec 3.7, 4.1.
#include <windows.h>
#include <shellapi.h>
#include "util/Handles.h"

// Permanent application GUID for the notification icon. Never change.
inline constexpr GUID kTrayGuid = {0xb9e28437, 0x5716, 0x4618, {0x98, 0xcd, 0xaf, 0x81, 0xb8, 0xef, 0x06, 0xfe}};
inline constexpr UINT kTrayCallbackMsg = WM_APP + 1;
inline constexpr UINT kTrayFallbackUid = 1;

class TrayIcon {
public:
    // NIM_ADD with NIF_GUID|NIF_ICON|NIF_TIP|NIF_MESSAGE|NIF_SHOWTIP, then NIM_SETVERSION (NOTIFYICON_VERSION_4).
    // If NIM_ADD with the GUID fails: NIM_DELETE(guid) and retry once; if it still fails, fall back to
    // hWnd + uID = kTrayFallbackUid (Windows binds the GUID to the EXE path; a portable EXE may move).
    // Safe to call again after TaskbarCreated. Icons: LoadIconMetric(LIM_SMALL) of IDI_IDLE / IDI_RECORDING.
    bool Add(HWND owner, bool recording);
    void SetRecording(bool recording);   // icon + tooltip "TinyLog — Idle" / "TinyLog — Recording"
    void Remove();                       // NIM_DELETE (no-op if not added)
    bool GetIconRect(RECT& rc) const;    // Shell_NotifyIconGetRect with whichever identity Add used
    void ShowBalloon(const wchar_t* title, const wchar_t* text, bool error);   // NIF_INFO, NIIF_INFO/NIIF_ERROR
    void OnDpiOrThemeChanged();          // reload icons, NIM_MODIFY

private:
    HWND       owner_ = nullptr;
    bool       added_ = false;
    bool       useGuid_ = true;
    bool       recording_ = false;
    UniqueIcon iconIdle_;
    UniqueIcon iconRecording_;
};
