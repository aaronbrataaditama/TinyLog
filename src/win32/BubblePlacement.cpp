// Implementation: WP-F2. Spec 4 (bubble positioning and monitor behaviour).
#include "win32/BubblePlacement.h"

namespace {

// Returns the start coordinate that keeps [pos, pos + size) inside [lo, hi).
// A bubble larger than the work area is aligned to lo (never resized).
int ClampAxis(int pos, int size, int lo, int hi) {
    if (size >= hi - lo) return lo;
    if (pos < lo) return lo;
    if (pos + size > hi) return hi - size;
    return pos;
}

}  // namespace

Placement ComputePlacement(const RECT& icon, const RECT& work, SIZE size, int gap) {
    const int spaceAbove = icon.top - gap - work.top;
    const int spaceBelow = work.bottom - (icon.bottom + gap);
    const int spaceLeft = icon.left - gap - work.left;
    const int spaceRight = work.right - (icon.right + gap);

    BubbleSide side = BubbleSide::Above;
    if (icon.top >= work.bottom) {
        side = BubbleSide::Above;            // icon in a bottom taskbar
    } else if (icon.bottom <= work.top) {
        side = BubbleSide::Below;            // top taskbar
    } else if (icon.right <= work.left) {
        side = BubbleSide::Right;            // left taskbar
    } else if (icon.left >= work.right) {
        side = BubbleSide::Left;             // right taskbar
    } else if (spaceAbove >= size.cy) {
        side = BubbleSide::Above;            // icon inside the work area (overflow flyout, auto-hide, fallback)
    } else if (spaceBelow >= size.cy) {
        side = BubbleSide::Below;
    } else if (spaceLeft >= size.cx || spaceRight >= size.cx) {
        side = spaceRight > spaceLeft ? BubbleSide::Right : BubbleSide::Left;
    } else {
        // Nothing fits: take the side with the most room; clamping keeps it on screen.
        int best = spaceAbove;
        if (spaceBelow > best) { best = spaceBelow; side = BubbleSide::Below; }
        if (spaceLeft > best) { best = spaceLeft; side = BubbleSide::Left; }
        if (spaceRight > best) { side = BubbleSide::Right; }
    }

    const int iconCx = (icon.left + icon.right) / 2;
    const int iconCy = (icon.top + icon.bottom) / 2;

    RECT r{};
    POINT anchor{};
    switch (side) {
    case BubbleSide::Above:
        r.bottom = icon.top - gap;
        r.top = r.bottom - size.cy;
        r.left = iconCx - size.cx / 2;
        anchor = POINT{iconCx, icon.top};
        break;
    case BubbleSide::Below:
        r.top = icon.bottom + gap;
        r.bottom = r.top + size.cy;
        r.left = iconCx - size.cx / 2;
        anchor = POINT{iconCx, icon.bottom};
        break;
    case BubbleSide::Left:
        r.right = icon.left - gap;
        r.left = r.right - size.cx;
        r.top = iconCy - size.cy / 2;
        anchor = POINT{icon.left, iconCy};
        break;
    case BubbleSide::Right:
        r.left = icon.right + gap;
        r.right = r.left + size.cx;
        r.top = iconCy - size.cy / 2;
        anchor = POINT{icon.right, iconCy};
        break;
    }

    // Shift (never resize) the whole rect inside the work area.
    r.left = ClampAxis(r.left, size.cx, work.left, work.right);
    r.top = ClampAxis(r.top, size.cy, work.top, work.bottom);
    r.right = r.left + size.cx;
    r.bottom = r.top + size.cy;

    return Placement{r, anchor, side};
}

Placement FallbackPlacement(POINT cursor, const RECT& workArea, SIZE bubbleSize, int gap) {
    const RECT icon{cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    return ComputePlacement(icon, workArea, bubbleSize, gap);
}
