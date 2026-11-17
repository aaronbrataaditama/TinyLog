// WP-F1: test-only viewer for the bubble; never shipped.
// Ctrl+Alt+T opens the bubble at the cursor, Ctrl+Alt+Q (or closing the owner) quits.
#include <windows.h>

#include <cstdint>
#include <string>

#include "util/TimeHelpers.h"
#include "win32/BubbleWindow.h"

namespace {

constexpr int kHotkeyOpen = 1;
constexpr int kHotkeyQuit = 2;
constexpr wchar_t kOwnerClass[] = L"TinyLogBubbleDemoOwner";

int64_t NowUtc() {
    const int64_t t = CaptureNow().utc;
    if (t != 0) return t;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

class DemoHost final : public IBubbleHost {
public:
    BubbleWindow* bubble = nullptr;
    HWND owner = nullptr;
    BubbleModel model;

    void OnStartStopClicked() override {
        model.recording = !model.recording;
        if (model.recording) model.startUtc = NowUtc();
        if (bubble) bubble->SetModel(model);
    }
    void OnExportClicked() override {
        if (!bubble) return;
        bubble->SetModalGuard(true);
        MessageBoxW(bubble->Hwnd(), L"Export to Excel (demo only).", L"BubbleDemo", MB_OK | MB_ICONINFORMATION);
        bubble->SetModalGuard(false);
    }
    void OnNotesChanged(const std::wstring&) override {}
    void OnBubbleHidden(bool) override {}
};

LRESULT CALLBACK OwnerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OwnerProc;
    wc.hInstance = inst;
    wc.lpszClassName = kOwnerClass;
    if (!RegisterClassExW(&wc)) return 1;

    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, kOwnerClass, L"BubbleDemoOwner", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                 inst, nullptr);
    if (!owner) return 1;

    RegisterHotKey(owner, kHotkeyOpen, MOD_CONTROL | MOD_ALT, 'T');
    RegisterHotKey(owner, kHotkeyQuit, MOD_CONTROL | MOD_ALT, 'Q');

    DemoHost host;
    host.owner = owner;
    int exitCode = 0;
    {
        BubbleWindow bubble(inst, owner, host);
        host.bubble = &bubble;
        bubble.SetModel(host.model);

        POINT pt{};
        GetCursorPos(&pt);
        bubble.Show(nullptr, pt);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_HOTKEY && msg.hwnd == owner) {
                if (msg.wParam == kHotkeyOpen) {
                    GetCursorPos(&pt);
                    bubble.Show(nullptr, pt);
                } else if (msg.wParam == kHotkeyQuit) {
                    DestroyWindow(owner);
                }
                continue;
            }
            if (bubble.PreTranslate(msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exitCode = static_cast<int>(msg.wParam);
        host.bubble = nullptr;
    }
    UnregisterHotKey(owner, kHotkeyOpen);
    UnregisterHotKey(owner, kHotkeyQuit);
    return exitCode;
}
