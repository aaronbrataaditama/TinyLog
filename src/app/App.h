#pragma once
// Application integration (WP-H): owner window, tray wiring, session manager, export.
// UI-thread only; no background threads, no idle timers.
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "app/Clock.h"
#include "app/SessionManager.h"
#include "storage/JsonLinesStore.h"
#include "storage/StateStore.h"
#include "util/ComHelpers.h"
#include "win32/BubbleWindow.h"
#include "win32/TrayIcon.h"

class App final : public IBubbleHost {
public:
    int Run(HINSTANCE inst);   // returns process exit code

    // IBubbleHost
    void OnStartStopClicked() override;
    void OnExportClicked() override;
    void OnNotesChanged(const std::wstring& notes) override;
    void OnBubbleHidden(bool byDeactivation) override;

private:
    static LRESULT CALLBACK OwnerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnTrayCallback(WPARAM wp, LPARAM lp);

    BubbleModel Model() const;
    void ToggleBubble(POINT anchor);
    void OpenBubble(POINT fallback);
    void ShowContextMenu(POINT anchor);
    void DoExport();
    void ToggleStartup(bool currentlyEnabled);
    void Balloon(const std::wstring& title, const std::wstring& text, bool error);
    void SetPendingBalloon(const std::wstring& title, const std::wstring& text, bool error);
    void AddTrayIcon();   // tray_.Add, then shows the pending balloon once the icon exists
    void Exit();

    HINSTANCE inst_ = nullptr;
    HWND owner_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    UINT activateMsg_ = 0;

    // Declaration order = construction order; members are destroyed in reverse (bubble first).
    std::optional<ComInit> comInit_;   // emplaced in Run; destroyed last
    std::unique_ptr<StateStore> stateStore_;
    std::unique_ptr<JsonLinesStore> log_;
    SystemClock clock_;
    std::unique_ptr<SessionManager> session_;
    TrayIcon tray_;
    std::unique_ptr<BubbleWindow> bubble_;

    bool exporting_ = false;
    bool notesFailureShown_ = false;
    bool exiting_ = false;
    bool exitRequested_ = false;         // Exit() arrived while the Save dialog was up: run it afterwards

    struct PendingBalloon {
        std::wstring title;
        std::wstring text;
        bool error = false;
        bool active = false;
    };
    PendingBalloon pendingBalloon_;      // startup balloon waiting for the first successful tray_.Add
    ULONGLONG lastToggleTick_ = 0;       // 0 = never
    ULONGLONG hiddenByDeactTick_ = 0;    // 0 = never
};
