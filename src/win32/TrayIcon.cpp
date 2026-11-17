// Spec 3.7. Notification icon with GUID identity and hWnd+uID fallback.
#include "win32/TrayIcon.h"

#include <commctrl.h>
#include <cwchar>

#include "../../resources/resource.h"

namespace {

constexpr wchar_t kTipIdle[]      = L"TinyLog — Idle";
constexpr wchar_t kTipRecording[] = L"TinyLog — Recording";

// The single NOTIFYICONDATAW builder: fills identity (GUID or hWnd+uID) and the version-4 struct size.
NOTIFYICONDATAW MakeData(HWND owner, bool useGuid) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    if (useGuid) {
        nid.uFlags = NIF_GUID;
        nid.guidItem = kTrayGuid;
    } else {
        nid.uID = kTrayFallbackUid;
    }
    return nid;
}

void SetTip(NOTIFYICONDATAW& nid, bool recording) {
    wcscpy_s(nid.szTip, recording ? kTipRecording : kTipIdle);
}

bool LoadIcon16(int resId, UniqueIcon& out) {
    HICON h = nullptr;
    const HRESULT hr = LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resId), LIM_SMALL, &h);
    if (FAILED(hr) || !h) return false;
    out.reset(h);
    return true;
}

}  // namespace

bool TrayIcon::Add(HWND owner, bool recording) {
    owner_ = owner;
    recording_ = recording;

    if (!LoadIcon16(IDI_IDLE, iconIdle_) || !LoadIcon16(IDI_RECORDING, iconRecording_)) {
        added_ = false;
        return false;
    }

    auto build = [&](bool useGuid) {
        NOTIFYICONDATAW nid = MakeData(owner_, useGuid);
        nid.uFlags |= NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
        nid.uCallbackMessage = kTrayCallbackMsg;
        nid.hIcon = (recording_ ? iconRecording_ : iconIdle_).get();
        SetTip(nid, recording_);
        return nid;
    };

    bool ok = false;
    useGuid_ = true;
    {
        NOTIFYICONDATAW nid = build(true);
        ok = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        if (!ok) {
            // The GUID may be bound to a stale registration (e.g. EXE moved): drop it and retry once.
            NOTIFYICONDATAW del = MakeData(owner_, true);
            Shell_NotifyIconW(NIM_DELETE, &del);
            nid = build(true);
            ok = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        }
    }
    if (!ok) {
        useGuid_ = false;
        NOTIFYICONDATAW nid = build(false);
        ok = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    }
    if (!ok) {
        added_ = false;
        return false;
    }

    NOTIFYICONDATAW ver = MakeData(owner_, useGuid_);
    ver.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &ver)) {
        NOTIFYICONDATAW del = MakeData(owner_, useGuid_);
        Shell_NotifyIconW(NIM_DELETE, &del);
        added_ = false;
        return false;
    }

    added_ = true;
    return true;
}

void TrayIcon::SetRecording(bool recording) {
    recording_ = recording;
    if (!added_) return;
    NOTIFYICONDATAW nid = MakeData(owner_, useGuid_);
    nid.uFlags |= NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = (recording_ ? iconRecording_ : iconIdle_).get();
    SetTip(nid, recording_);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::Remove() {
    if (!added_) return;
    NOTIFYICONDATAW nid = MakeData(owner_, useGuid_);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

bool TrayIcon::GetIconRect(RECT& rc) const {
    if (!added_) return false;
    NOTIFYICONIDENTIFIER id{};
    id.cbSize = sizeof(id);
    id.hWnd = owner_;
    if (useGuid_)
        id.guidItem = kTrayGuid;
    else
        id.uID = kTrayFallbackUid;
    RECT r{};
    if (FAILED(Shell_NotifyIconGetRect(&id, &r))) return false;
    rc = r;
    return true;
}

void TrayIcon::ShowBalloon(const wchar_t* title, const wchar_t* text, bool error) {
    if (!added_) return;
    NOTIFYICONDATAW nid = MakeData(owner_, useGuid_);
    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title ? title : L"", _TRUNCATE);   // long messages are cut, never crash
    wcsncpy_s(nid.szInfo, text ? text : L"", _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::OnDpiOrThemeChanged() {
    UniqueIcon idle, rec;
    if (!LoadIcon16(IDI_IDLE, idle) || !LoadIcon16(IDI_RECORDING, rec)) return;   // keep the old icons
    if (!added_) {
        iconIdle_ = std::move(idle);
        iconRecording_ = std::move(rec);
        return;
    }
    // Point the shell at the new icon first, then release the old handles.
    NOTIFYICONDATAW nid = MakeData(owner_, useGuid_);
    nid.uFlags |= NIF_ICON;
    nid.hIcon = (recording_ ? rec : idle).get();
    if (Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        iconIdle_ = std::move(idle);
        iconRecording_ = std::move(rec);
    }
}
