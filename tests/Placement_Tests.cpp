// Tests for WP-F2 (bubble placement). Spec 4.
#include "TestHarness.h"
#include "win32/BubblePlacement.h"

#include <string>

namespace {

RECT MakeRect(int l, int t, int r, int b) {
    RECT rc{};
    rc.left = l;
    rc.top = t;
    rc.right = r;
    rc.bottom = b;
    return rc;
}

std::string RectStr(const RECT& rc) {
    return "{" + std::to_string(rc.left) + "," + std::to_string(rc.top) + "," + std::to_string(rc.right) +
           "," + std::to_string(rc.bottom) + "}";
}

void CheckRectAt(const char* file, int line, const RECT& got, const RECT& want) {
    if (got.left != want.left || got.top != want.top || got.right != want.right || got.bottom != want.bottom)
        th::Fail(file, line, "rect", "got " + RectStr(got) + " want " + RectStr(want));
}

}  // namespace

#define CHECK_RECT(got, l, t, r, b) CheckRectAt(__FILE__, __LINE__, (got), MakeRect((l), (t), (r), (b)))

// Bottom taskbar, icon mid-screen: bubble above, centred on icon.
TEST(Placement_BottomTaskbarAbove) {
    const RECT work = MakeRect(0, 0, 1920, 1040);
    const RECT icon = MakeRect(900, 1048, 924, 1072);
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Above);
    CHECK_RECT(p.rect, 732, 810, 1092, 1040);
    CHECK_EQ(p.anchor.x, 912);
    CHECK_EQ(p.anchor.y, 1048);
}

// Bottom taskbar, icon at right edge: rect shifted left so right <= work.right.
TEST(Placement_BottomTaskbarClampedRightEdge) {
    const RECT work = MakeRect(0, 0, 1920, 1040);
    const RECT icon = MakeRect(1800, 1048, 1824, 1072);
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Above);
    CHECK_RECT(p.rect, 1560, 810, 1920, 1040);
    CHECK(p.rect.right <= work.right);
    CHECK_EQ(p.anchor.x, 1812);
    CHECK_EQ(p.anchor.y, 1048);
}

// Top taskbar: not enough room above, Below has the most room.
TEST(Placement_TopTaskbarBelow) {
    const RECT work = MakeRect(0, 48, 1920, 1080);
    const RECT icon = MakeRect(960, 8, 984, 32);
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Below);
    // Top would be 40, shifted down to work.top = 48.
    CHECK_RECT(p.rect, 792, 48, 1152, 278);
    CHECK_EQ(p.anchor.x, 972);
    CHECK_EQ(p.anchor.y, 32);
}

// Left taskbar: icon near top-left, Right has the most room.
TEST(Placement_LeftTaskbarRightSide) {
    const RECT work = MakeRect(48, 0, 1920, 1080);
    const RECT icon = MakeRect(16, 8, 40, 32);
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Right);
    CHECK_RECT(p.rect, 48, 0, 408, 230);
    CHECK_EQ(p.anchor.x, 40);
    CHECK_EQ(p.anchor.y, 20);
}

// Right taskbar: icon near top-right, Left has the most room.
TEST(Placement_RightTaskbarLeftSide) {
    const RECT work = MakeRect(0, 0, 1872, 1080);
    const RECT icon = MakeRect(1880, 8, 1904, 32);   // icon lives in the taskbar, right of the work area
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Left);
    CHECK_RECT(p.rect, 1512, 0, 1872, 230);
    CHECK_EQ(p.anchor.x, 1880);
    CHECK_EQ(p.anchor.y, 20);
}

// Bubble larger than the work area: never resized, aligned to work.left/top.
TEST(Placement_BubbleLargerThanWorkAligns) {
    const RECT work = MakeRect(0, 0, 200, 100);
    const RECT icon = MakeRect(50, 80, 70, 90);
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Right);
    CHECK_RECT(p.rect, 0, 0, 360, 230);
    CHECK_EQ(p.anchor.x, 70);
    CHECK_EQ(p.anchor.y, 85);
}

// Secondary monitor to the left of the primary, with negative work-area coordinates.
TEST(Placement_MultiMonitorNegativeCoords) {
    const RECT work = MakeRect(-1920, 0, 0, 1040);
    const RECT icon = MakeRect(-40, 1048, -16, 1072);
    const Placement p = ComputePlacement(icon, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Above);
    CHECK_RECT(p.rect, -360, 810, 0, 1040);
    CHECK_EQ(p.anchor.x, -28);
    CHECK_EQ(p.anchor.y, 1048);
}

// Cursor fallback is treated as a 1x1 icon rect.
TEST(Placement_FallbackFromCursor) {
    const RECT work = MakeRect(0, 0, 1920, 1040);
    POINT cursor{};
    cursor.x = 900;
    cursor.y = 1040;
    const Placement p = FallbackPlacement(cursor, work, SIZE{360, 230}, 8);
    CHECK(p.side == BubbleSide::Above);
    CHECK_RECT(p.rect, 720, 802, 1080, 1032);
    CHECK_EQ(p.anchor.x, 900);
    CHECK_EQ(p.anchor.y, 1040);
}

// Taskbar-edge detection (orchestrator fix): icon outside the work area picks the side facing it.
TEST(Placement_EdgeDetect_TopTaskbarRightCorner) {
    const RECT work{0, 48, 1920, 1080};
    const RECT icon{1880, 8, 1904, 32};
    const Placement p = ComputePlacement(icon, work, SIZE{360, 223}, 6);
    CHECK(p.side == BubbleSide::Below);
    CHECK(p.rect.right <= work.right);
}

TEST(Placement_EdgeDetect_LeftTaskbarBottomIcon) {
    const RECT work{48, 0, 1920, 1080};
    const RECT icon{12, 1000, 36, 1024};
    const Placement p = ComputePlacement(icon, work, SIZE{360, 223}, 6);
    CHECK(p.side == BubbleSide::Right);
    CHECK(p.rect.bottom <= work.bottom);
    CHECK_EQ(p.rect.left, 48L);   // 36 + 6 = 42, clamped into the work area
}
