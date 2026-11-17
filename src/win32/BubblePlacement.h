#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: BubblePlacement.cpp (WP-F2). Spec 4. Physical pixels.
#include <windows.h>

enum class BubbleSide { Above, Below, Left, Right };   // side of the icon on which the bubble sits

struct Placement {
    RECT       rect;     // final bubble window rect (includes the pointer area)
    POINT      anchor;   // midpoint of the icon edge facing the bubble; pointer tip / animation pivot
    BubbleSide side;
};

// Prefer Above if the bubble fits between workArea.top and iconRect.top - gap; otherwise pick the side
// (Below / Left / Right) with the most room. Center on the icon along the cross axis, then clamp the
// whole rect inside workArea.
Placement ComputePlacement(const RECT& iconRect, const RECT& workArea, SIZE bubbleSize, int gap);

// Treat cursor as a 1x1 icon rect.
Placement FallbackPlacement(POINT cursor, const RECT& workArea, SIZE bubbleSize, int gap);
