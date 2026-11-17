// Implementation: WP-F2. Spec 3.5 (bubble expansion and collapse).
#include "win32/LayeredAnimation.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr UINT kFrameIntervalMs = 15;
constexpr float kMinScale = 0.10f;

// Cached QueryPerformanceFrequency. Never zero on Windows XP and later.
int64_t QpcFrequency() {
    static const int64_t freq = [] {
        LARGE_INTEGER f{};
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return int64_t{1};
        return static_cast<int64_t>(f.QuadPart);
    }();
    return freq;
}

int64_t QpcNow() {
    LARGE_INTEGER n{};
    QueryPerformanceCounter(&n);  // cannot fail on Windows XP and later
    return static_cast<int64_t>(n.QuadPart);
}

LONG ScaleEdge(LONG edge, LONG anchor, float scale) {
    const float d = static_cast<float>(edge - anchor) * scale;
    return anchor + static_cast<LONG>(std::lround(d));
}

}  // namespace

float EaseOutCubic(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - c;
    return 1.0f - u * u * u;
}

RECT ScaleRectAboutAnchor(const RECT& finalRect, POINT anchor, float scale) {
    RECT r{};
    r.left = ScaleEdge(finalRect.left, anchor.x, scale);
    r.right = ScaleEdge(finalRect.right, anchor.x, scale);
    r.top = ScaleEdge(finalRect.top, anchor.y, scale);
    r.bottom = ScaleEdge(finalRect.bottom, anchor.y, scale);
    if (r.right <= r.left) r.right = r.left + 1;
    if (r.bottom <= r.top) r.bottom = r.top + 1;
    return r;
}

BYTE AlphaForProgress(float eased) {
    const float p = std::clamp(eased, 0.0f, 1.0f);
    return static_cast<BYTE>(std::lround(255.0f * p));
}

void LayeredAnimation::Start(HWND hwnd, Dir dir, RECT finalRect, POINT anchor, UINT durationMs) {
    hwnd_ = hwnd;
    dir_ = dir;
    final_ = finalRect;
    anchor_ = anchor;
    durationMs_ = durationMs;
    startQpc_ = QpcNow();
    running_ = true;
    if (hwnd_ != nullptr && SetTimer(hwnd_, kTimerId, kFrameIntervalMs, nullptr) == 0) {
        // No timer means no frames: report not running so the owner snaps to the final frame.
        running_ = false;
    }
}

LayeredAnimation::Frame LayeredAnimation::Tick() {
    const bool open = dir_ == Dir::Open;

    double t = 1.0;
    if (running_ && durationMs_ > 0) {
        const double elapsedMs = static_cast<double>(QpcNow() - startQpc_) * 1000.0 /
                                 static_cast<double>(QpcFrequency());
        t = elapsedMs / static_cast<double>(durationMs_);
    }

    if (!running_ || t >= 1.0) {
        if (running_) {
            running_ = false;
            if (hwnd_ != nullptr) KillTimer(hwnd_, kTimerId);
        }
        if (open) return Frame{final_, 255, true};
        return Frame{ScaleRectAboutAnchor(final_, anchor_, kMinScale), 0, true};
    }

    const float e = EaseOutCubic(static_cast<float>(t));
    const float progress = open ? e : 1.0f - e;
    const float scale = kMinScale + (1.0f - kMinScale) * progress;
    return Frame{ScaleRectAboutAnchor(final_, anchor_, scale), AlphaForProgress(progress), false};
}

void LayeredAnimation::Cancel() {
    if (running_ && hwnd_ != nullptr) KillTimer(hwnd_, kTimerId);
    running_ = false;
}
