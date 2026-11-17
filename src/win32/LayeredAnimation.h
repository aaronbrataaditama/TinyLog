#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: LayeredAnimation.cpp (WP-F2). Spec 3.5, 13.2.
#include <windows.h>
#include <cstdint>

float EaseOutCubic(float t);                                            // 1 - (1-t)^3, t clamped to [0,1]
RECT  ScaleRectAboutAnchor(const RECT& finalRect, POINT anchor, float scale);  // anchor stays fixed; min 1x1
BYTE  AlphaForProgress(float eased);                                    // round(255 * eased), clamped

// Pure progress driver; the owning window applies frames (SetWindowPos + SetLayeredWindowAttributes).
class LayeredAnimation {
public:
    enum class Dir { Open, Close };
    struct Frame { RECT rect; BYTE alpha; bool done; };
    static constexpr UINT_PTR kTimerId = 0xA11;

    // Records QPC start and SetTimer(hwnd, kTimerId, 15). Open: scale 0.10 -> 1.00, alpha 0 -> 255.
    // Close: the reverse. Restarting while running is allowed.
    void  Start(HWND hwnd, Dir dir, RECT finalRect, POINT anchor, UINT durationMs = 200);
    // Progress from QPC elapsed time (never "+16 ms per tick"). When done: KillTimer and return the
    // exact end frame (Open: finalRect, 255; Close: smallest rect, 0).
    Frame Tick();
    void  Cancel();   // KillTimer; Running() becomes false
    bool  Running() const { return running_; }
    Dir   Direction() const { return dir_; }

private:
    HWND    hwnd_ = nullptr;
    Dir     dir_ = Dir::Open;
    RECT    final_{};
    POINT   anchor_{};
    UINT    durationMs_ = 200;
    int64_t startQpc_ = 0;
    bool    running_ = false;
};
