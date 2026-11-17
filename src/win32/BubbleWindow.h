#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: BubbleWindow.cpp (WP-F1). Spec 3.1-3.6, 8.5, 8.6.
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

struct BubbleModel {
    bool    recording = false;
    bool    busy = false;       // Starting/Completing: Start/Stop button disabled
    int64_t startUtc = 0;       // Unix seconds; elapsed display = now - startUtc while recording
};

struct IBubbleHost {
    virtual ~IBubbleHost() = default;
    virtual void OnStartStopClicked() = 0;
    virtual void OnExportClicked() = 0;
    virtual void OnNotesChanged(const std::wstring& notes) = 0;   // every EN_CHANGE
    virtual void OnBubbleHidden(bool byDeactivation) = 0;         // after the close animation finishes
};

class BubbleWindow {
public:
    BubbleWindow(HINSTANCE inst, HWND owner, IBubbleHost& host);
    ~BubbleWindow();                                   // destroys HWND and every GDI object
    BubbleWindow(const BubbleWindow&) = delete;
    BubbleWindow& operator=(const BubbleWindow&) = delete;

    // Lazily creates HWND/fonts/controls on first call. iconRect may be nullptr (then fallbackPt is used).
    // Places via ComputePlacement/FallbackPlacement on the icon's monitor, animates open, focuses the EDIT.
    void Show(const RECT* iconRect, POINT fallbackPt);
    void Hide(bool animate);                           // animates close, SW_HIDE, then OnBubbleHidden(false)
    bool IsVisible() const;                            // true while open or opening
    void SetModel(const BubbleModel& model);           // relabels button, status dot, timer; 1 s display timer only while visible && recording
    void SetNotes(const std::wstring& notes);          // does NOT fire OnNotesChanged
    std::wstring GetNotes() const;
    void SetModalGuard(bool active);                   // while true, deactivation does not collapse
    bool PreTranslate(MSG& msg);                       // IsDialogMessage-style Tab/Space/Enter; Esc -> Hide(true). true = handled
    HWND Hwnd() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
