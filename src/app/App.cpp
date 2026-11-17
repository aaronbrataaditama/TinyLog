// Application integration (WP-H). Spec 3.1, 3.7, 4.2, 8.2-8.7, 9.1.
#include "app/App.h"

#include <windowsx.h>
#include <shellapi.h>
#include "export/ExportController.h"
#include "util/FileHelpers.h"
#include "util/Handles.h"
#include "util/Status.h"
#include "win32/SaveDialog.h"
#include "win32/SingleInstance.h"
#include "win32/StartupShortcut.h"
#include "win32/TrayMenu.h"

namespace {

constexpr ULONGLONG kDeactivationDebounceMs = 300;   // same tray click that collapsed the bubble
constexpr ULONGLONG kDuplicateToggleMs = 250;        // NIN_SELECT + double-click patterns
constexpr UINT kExitMsg = WM_APP + 2;                // deferred Exit() after the Save dialog closes
constexpr size_t kBalloonTitleMax = 63;              // NOTIFYICONDATAW::szInfoTitle is 64 incl. NUL
constexpr size_t kBalloonTextMax = 255;              // NOTIFYICONDATAW::szInfo is 256 incl. NUL

constexpr wchar_t kFirstRunText[] =
    L"TinyLog is running in the notification area. If you don't see the TinyLog stump icon, show it via "
    L"Settings > Personalization > Taskbar > Other system tray icons.";

// Creates and immediately deletes <dir>\TinyLog.probe (Spec 8.7: never relocate data).
Status ProbeWritable(const std::wstring& dir) {
    const std::wstring path = dir + L"\\TinyLog.probe";
    UniqueHandle h(CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!h.valid()) return Status::FromLastError(L"Create probe file");
    h.reset();   // CloseHandle deletes the file
    return Status::Ok();
}

void ShowFatal(const std::wstring& dir, const std::wstring& reason) {
    const std::wstring text = L"TinyLog cannot write next to TinyLog.exe:\n" + dir + L"\n" + reason +
                              L"\nMove TinyLog to a writable folder.";
    MessageBoxW(nullptr, text.c_str(), L"TinyLog", MB_OK | MB_ICONERROR);
}

POINT CursorPoint() {
    POINT pt{};
    if (!GetCursorPos(&pt)) pt = POINT{};
    return pt;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------------------------

int App::Run(HINSTANCE inst) {
    inst_ = inst;

    SingleInstance single;
    if (!single.Acquire()) {
        SingleInstance::ActivateExisting();
        return 0;
    }

    std::wstring dir;
    Status st = GetExeDirectory(dir);
    if (!st) {
        ShowFatal(L"(unknown location)", st.message);
        return 1;
    }
    st = ProbeWritable(dir);
    if (!st) {
        ShowFatal(dir, st.message);
        return 1;
    }

    const std::wstring logPath = dir + L"\\TinyLog.jsonl";
    const bool firstRun = !FileExists(logPath) && !FileExists(dir + L"\\TinyLog.state");

    comInit_.emplace();   // held for the whole UI-thread lifetime (startup shortcut, Save dialog)

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    activateMsg_ = RegisterWindowMessageW(kActivateMsgName);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::OwnerProc;
    wc.hInstance = inst;
    wc.lpszClassName = kOwnerClass;
    if (!RegisterClassExW(&wc)) {
        const Status e = Status::FromLastError(L"RegisterClassEx");
        MessageBoxW(nullptr, e.message.c_str(), L"TinyLog", MB_OK | MB_ICONERROR);
        return 1;
    }
    owner_ = CreateWindowExW(WS_EX_TOOLWINDOW, kOwnerClass, L"TinyLog", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                             inst, this);
    if (!owner_) {
        const Status e = Status::FromLastError(L"Create owner window");
        MessageBoxW(nullptr, e.message.c_str(), L"TinyLog", MB_OK | MB_ICONERROR);
        return 1;
    }

    stateStore_ = std::make_unique<StateStore>(dir);
    log_ = std::make_unique<JsonLinesStore>(logPath);
    session_ = std::make_unique<SessionManager>(*stateStore_, *log_, clock_);

    const SessionManager::RecoveryReport report = session_->Recover();

    // The startup balloon is kept pending until the tray icon actually exists (Add may fail, e.g. no
    // taskbar yet; the TaskbarCreated handler retries and then shows it).
    if (!report.error.empty()) {
        std::wstring text = report.error;
        if (report.quarantined && !report.quarantinePath.empty())
            text += L"\nMoved aside to: " + report.quarantinePath;
        SetPendingBalloon(L"TinyLog recovery problem", text, true);
    } else if (report.quarantined) {
        SetPendingBalloon(L"TinyLog", L"An unreadable state file was moved aside to: " + report.quarantinePath, false);
    } else if (firstRun) {
        SetPendingBalloon(L"TinyLog", kFirstRunText, false);
    }

    AddTrayIcon();   // false result: keep running, retry on TaskbarCreated

    MSG msg;
    BOOL r;
    while ((r = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (bubble_ && bubble_->PreTranslate(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // Never touch the state file here: a recording continues on the next launch.
    return r == 0 ? static_cast<int>(msg.wParam) : 1;
}

// ---------------------------------------------------------------------------------------------
// Owner window
// ---------------------------------------------------------------------------------------------

LRESULT CALLBACK App::OwnerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        if (session_) AddTrayIcon();
        return 0;
    }
    if (msg == kExitMsg) {
        Exit();
        return 0;
    }
    if (activateMsg_ != 0 && msg == activateMsg_) {
        if (session_) OpenBubble(CursorPoint());
        return 0;
    }

    switch (msg) {
        case kTrayCallbackMsg:
            OnTrayCallback(wp, lp);
            return 0;
        case WM_DPICHANGED:
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            tray_.OnDpiOrThemeChanged();
            break;   // default processing
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wp) tray_.Remove();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::OnTrayCallback(WPARAM wp, LPARAM lp) {
    if (!session_ || exiting_) return;
    // The Save dialog pumps messages: while it is up, tray clicks must neither toggle the bubble nor open
    // the menu (Exit would destroy the dialog's owner).
    if (exporting_) return;
    const POINT anchor{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)};
    switch (LOWORD(lp)) {
        // WM_LBUTTONDBLCLK is deliberately not handled: NIN_SELECT already fired for the first click, so a
        // double-click ends up "open" instead of toggling closed again.
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ToggleBubble(anchor);
            break;
        case WM_CONTEXTMENU:
            ShowContextMenu(anchor);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// Bubble
// ---------------------------------------------------------------------------------------------

BubbleModel App::Model() const {
    BubbleModel m;
    const AppState s = session_->State();
    m.recording = (s == AppState::Recording);
    m.busy = (s == AppState::Starting || s == AppState::Completing);
    const ActiveState* a = session_->Active();
    m.startUtc = a ? a->startUtc : 0;
    return m;
}

void App::ToggleBubble(POINT anchor) {
    const ULONGLONG now = GetTickCount64();
    if (hiddenByDeactTick_ != 0 && now - hiddenByDeactTick_ < kDeactivationDebounceMs) return;
    if (lastToggleTick_ != 0 && now - lastToggleTick_ < kDuplicateToggleMs) return;
    lastToggleTick_ = now;

    if (bubble_) {
        if (bubble_->IsVisible()) {
            bubble_->Hide(true);
            return;
        }
        // Still collapsing (window not yet hidden): this click is the one that deactivated it, or a
        // repeat. Show() would cancel the close and reopen it, so ignore.
        const HWND bw = bubble_->Hwnd();
        if (bw && IsWindowVisible(bw)) return;
    }
    OpenBubble(anchor);
}

void App::OpenBubble(POINT fallback) {
    if (!bubble_) bubble_ = std::make_unique<BubbleWindow>(inst_, owner_, *this);

    RECT rc{};
    const bool hasRect = tray_.GetIconRect(rc);

    if (!bubble_->IsVisible()) {
        bubble_->SetNotes(session_->Notes());
        bubble_->SetModel(Model());
    }
    bubble_->Show(hasRect ? &rc : nullptr, fallback);
}

void App::OnBubbleHidden(bool byDeactivation) {
    if (byDeactivation) hiddenByDeactTick_ = GetTickCount64();
}

// ---------------------------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------------------------

void App::ShowContextMenu(POINT anchor) {
    if (exporting_) return;
    if (bubble_ && bubble_->IsVisible()) bubble_->Hide(false);

    bool checked = false;
    if (!startup::IsEnabled(checked)) checked = false;

    const UINT cmd = ShowTrayMenu(owner_, anchor, checked);
    switch (cmd) {
        case kCmdStartup:
            ToggleStartup(checked);
            break;
        case kCmdExport:
            DoExport();
            break;
        case kCmdExit:
            Exit();
            break;
        default:
            break;
    }
}

void App::ToggleStartup(bool currentlyEnabled) {
    const Status st = currentlyEnabled ? startup::Disable() : startup::Enable();
    if (!st) Balloon(L"Start with Windows", L"Could not change the startup setting: " + st.message, true);
}

// ---------------------------------------------------------------------------------------------
// IBubbleHost
// ---------------------------------------------------------------------------------------------

void App::OnStartStopClicked() {
    if (!session_ || !bubble_) return;
    const std::wstring notes = bubble_->GetNotes();

    const AppState s = session_->State();
    if (s == AppState::Idle) {
        BubbleModel busy;
        busy.busy = true;
        bubble_->SetModel(busy);
        const Status st = session_->Start(notes);
        if (!st) Balloon(L"Could not start recording", st.message, true);
    } else if (s == AppState::Recording) {
        // A failed notes save must not prevent Stop: Stop uses the in-memory notes.
        (void)session_->UpdateNotes(notes);
        const Status st = session_->Stop();
        if (!st)
            Balloon(L"Could not stop recording", st.message, true);
        else if (!st.message.empty())
            Balloon(L"Recording stopped", st.message, false);   // saved; state cleanup failed
    } else {
        return;   // Starting/Completing: ignore
    }

    tray_.SetRecording(session_->State() == AppState::Recording);
    bubble_->SetModel(Model());
}

void App::OnNotesChanged(const std::wstring& notes) {
    if (!session_) return;
    const Status st = session_->UpdateNotes(notes);
    if (st) {
        notesFailureShown_ = false;
    } else if (!notesFailureShown_) {
        notesFailureShown_ = true;
        Balloon(L"Could not save notes", st.message, true);
    }
}

void App::OnExportClicked() {
    DoExport();
}

// ---------------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------------

void App::DoExport() {
    if (exporting_ || !log_) return;   // the dialog pumps messages: guard against re-entry
    exporting_ = true;
    if (bubble_) bubble_->SetModalGuard(true);

    HWND dlgOwner = owner_;
    if (bubble_ && bubble_->IsVisible()) {
        dlgOwner = bubble_->Hwnd();
    } else {
        SetForegroundWindow(owner_);
    }

    std::wstring path;
    Status err;
    const SaveOutcome outcome = ShowExportSaveDialog(dlgOwner, path, err);
    if (outcome == SaveOutcome::Ok) {
        size_t rows = 0;
        const Status st = ExportWorkbook(*log_, path, rows);
        if (st)
            Balloon(L"Export complete", L"Exported " + std::to_wstring(rows) + L" session(s)", false);
        else
            Balloon(L"Export failed", st.message, true);
    } else if (outcome == SaveOutcome::Failed) {
        Balloon(L"Export failed", err.message, true);
    }

    if (bubble_) bubble_->SetModalGuard(false);
    exporting_ = false;

    // An exit requested during the dialog runs now, but posted: DoExport can be running inside the
    // bubble's own window procedure (Export button), which Exit() would destroy.
    if (exitRequested_ && owner_) PostMessageW(owner_, kExitMsg, 0, 0);
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

void App::Balloon(const std::wstring& title, const std::wstring& text, bool error) {
    // TrayIcon::ShowBalloon copies with wcscpy_s: over-long text would trip the CRT invalid-parameter
    // handler, so clamp to the NOTIFYICONDATA field sizes here.
    const std::wstring t = title.substr(0, kBalloonTitleMax);
    const std::wstring x = text.substr(0, kBalloonTextMax);
    tray_.ShowBalloon(t.c_str(), x.c_str(), error);
}

void App::SetPendingBalloon(const std::wstring& title, const std::wstring& text, bool error) {
    pendingBalloon_.title = title;
    pendingBalloon_.text = text;
    pendingBalloon_.error = error;
    pendingBalloon_.active = true;
}

void App::AddTrayIcon() {
    if (!tray_.Add(owner_, session_ && session_->State() == AppState::Recording)) return;
    if (!pendingBalloon_.active) return;
    pendingBalloon_.active = false;
    Balloon(pendingBalloon_.title, pendingBalloon_.text, pendingBalloon_.error);
    pendingBalloon_ = PendingBalloon{};
}

void App::Exit() {
    if (exiting_) return;
    if (exporting_) {   // never destroy the bubble or owner while the Save dialog is up
        exitRequested_ = true;
        return;
    }
    exiting_ = true;
    tray_.Remove();
    bubble_.reset();
    if (owner_) {
        DestroyWindow(owner_);
        owner_ = nullptr;
    }
    PostQuitMessage(0);
}
