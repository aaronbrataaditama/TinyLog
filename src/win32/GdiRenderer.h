#pragma once
// Owned by WP-F1 (not a frozen contract). Painting of the bubble card, header, timer pill and buttons.
// Everything here works in physical pixels; Geometry is computed for one DPI + one pointer side.
#include <windows.h>

#include <string>

#include "util/Handles.h"
#include "win32/BubblePlacement.h"

namespace gdi {

struct Geometry {
    UINT       dpi = 96;
    BubbleSide side = BubbleSide::Above;
    SIZE       size{};      // window client size, includes the pointer area
    RECT       body{};      // the rounded card inside the window
    int        tip = 0;     // pointer tip offset along the pointer edge (window coords), already clamped
    int        radius = 0;  // scaled corner radius
    int        ptrW = 0;
    int        ptrH = 0;
    RECT       headerRow{};
    RECT       dot{};       // status dot bounding box
    RECT       pill{};      // timer pill
    RECT       notesBox{};  // rounded notes frame painted by the parent
    RECT       edit{};      // EDIT control rect (inset inside notesBox)
    RECT       startBtn{};
    RECT       exportBtn{};
};

// Window size for a given pointer side (pointer area included).
SIZE     BubbleSizeFor(UINT dpi, BubbleSide side);
// tipAlongEdge: pointer tip position along the pointer edge, in window coordinates (clamped inside).
Geometry MakeGeometry(UINT dpi, BubbleSide side, int tipAlongEdge);

// Rounded card + pointer region. curW/curH scale the geometry (animation frames); pass g.size for 1:1.
UniqueGdi<HRGN> CreateShapeRegion(const Geometry& g, int curW, int curH);

struct Fonts {
    UniqueGdi<HFONT> title;
    UniqueGdi<HFONT> timer;
    UniqueGdi<HFONT> notes;
    UniqueGdi<HFONT> button;
    bool Create(UINT dpi);   // replaces all four fonts; false (and nothing changed) if any failed
};

struct ViewState {
    bool                recording = false;
    bool                busy = false;
    bool                notesFocused = false;
    const std::wstring* timerText = nullptr;
};

enum class ButtonKind { StartStop, Export };

struct ButtonVisual {
    bool hover = false;
    bool pressed = false;
    bool focus = false;     // draw the keyboard focus ring
    bool disabled = false;
};

// Card background, border, header (dot, title, divider, timer pill) and the notes frame. Not the notes
// text and not the buttons (those are real child controls).
void PaintBubble(HDC dc, const Geometry& g, const Fonts& fonts, const ViewState& view);
void PaintNotesText(HDC dc, const Geometry& g, const Fonts& fonts, const std::wstring& notes);
// Draws one button into `rc` on `dc` (any DC, any origin).
void PaintButton(HDC dc, const RECT& rc, ButtonKind kind, const ButtonVisual& vis, const ViewState& view,
                 const Fonts& fonts, UINT dpi);
// Same, double-buffered onto a real DC (used from WM_DRAWITEM).
void PaintButtonBuffered(HDC target, const RECT& rc, ButtonKind kind, const ButtonVisual& vis,
                         const ViewState& view, const Fonts& fonts, UINT dpi);

// Full-size snapshot of the bubble including notes text and button faces (for the open/close animation).
UniqueGdi<HBITMAP> CreateSnapshot(HDC ref, const Geometry& g, const Fonts& fonts, const ViewState& view,
                                  const std::wstring& notes);
void DrawSnapshot(HDC dst, const RECT& dstRect, HBITMAP snapshot, SIZE snapshotSize);

}  // namespace gdi
