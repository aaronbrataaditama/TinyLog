// Tests for WP-F2 (layered animation math). Spec 3.5.
#include "TestHarness.h"
#include "win32/LayeredAnimation.h"

#include <cmath>
#include <cstdlib>

namespace {

RECT MakeRect(int l, int t, int r, int b) {
    RECT rc{};
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

bool SameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

}  // namespace

TEST(Anim_EaseOutCubicValues) {
    CHECK(std::fabs(EaseOutCubic(0.0f) - 0.0f) < 1e-6f);
    CHECK(std::fabs(EaseOutCubic(1.0f) - 1.0f) < 1e-6f);
    CHECK(std::fabs(EaseOutCubic(0.5f) - 0.875f) < 1e-6f);
    CHECK(std::fabs(EaseOutCubic(-1.0f) - 0.0f) < 1e-6f);
    CHECK(std::fabs(EaseOutCubic(2.0f) - 1.0f) < 1e-6f);
}

TEST(Anim_ScaleFinalRectIsIdentity) {
    const RECT fin = MakeRect(1560, 810, 1920, 1040);
    const POINT anchor{1812, 1048};
    CHECK(SameRect(ScaleRectAboutAnchor(fin, anchor, 1.0f), fin));
}

// Anchor inside the final rect stays inside the scaled rect; width/height ~10%.
TEST(Anim_ScaleTenPercentContainsAnchor) {
    const RECT fin = MakeRect(1560, 810, 1920, 1040);
    const POINT anchor{1740, 925};  // centre of fin
    const RECT r = ScaleRectAboutAnchor(fin, anchor, 0.10f);
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    CHECK(std::abs(w - 36) <= 1);
    CHECK(std::abs(h - 23) <= 1);
    CHECK(r.left <= anchor.x && anchor.x <= r.right);
    CHECK(r.top <= anchor.y && anchor.y <= r.bottom);
}

TEST(Anim_ScaleKeepsMinimumOneByOne) {
    const RECT r = ScaleRectAboutAnchor(MakeRect(0, 0, 10, 10), POINT{5, 5}, 0.0f);
    CHECK(r.right - r.left >= 1);
    CHECK(r.bottom - r.top >= 1);
}

TEST(Anim_AlphaForProgressRounds) {
    CHECK_EQ(AlphaForProgress(1.0f), 255);
    CHECK_EQ(AlphaForProgress(0.0f), 0);
    CHECK_EQ(AlphaForProgress(0.5f), 128);
    CHECK_EQ(AlphaForProgress(2.0f), 255);
    CHECK_EQ(AlphaForProgress(-1.0f), 0);
}

TEST(Anim_LayeredOpenFinalFrameExact) {
    LayeredAnimation a;
    const RECT fin = MakeRect(1560, 810, 1920, 1040);
    a.Start(nullptr, LayeredAnimation::Dir::Open, fin, POINT{1812, 1048}, 1);
    CHECK(a.Running());
    Sleep(5);
    const LayeredAnimation::Frame f = a.Tick();
    CHECK(f.done);
    CHECK(!a.Running());
    CHECK(SameRect(f.rect, fin));
    CHECK_EQ(f.alpha, 255);
}

TEST(Anim_LayeredCloseFinalFrameExact) {
    LayeredAnimation a;
    const RECT fin = MakeRect(1560, 810, 1920, 1040);
    const POINT anchor{1812, 1048};
    a.Start(nullptr, LayeredAnimation::Dir::Close, fin, anchor, 1);
    Sleep(5);
    const LayeredAnimation::Frame f = a.Tick();
    CHECK(f.done);
    CHECK(!a.Running());
    CHECK(SameRect(f.rect, ScaleRectAboutAnchor(fin, anchor, 0.10f)));
    CHECK_EQ(f.alpha, 0);
}

TEST(Anim_TickBeforeDurationNotDone) {
    LayeredAnimation a;
    a.Start(nullptr, LayeredAnimation::Dir::Open, MakeRect(0, 0, 100, 100), POINT{50, 100}, 60000);
    const LayeredAnimation::Frame f = a.Tick();
    CHECK(!f.done);
    CHECK(a.Running());
    a.Cancel();
    CHECK(!a.Running());
}
