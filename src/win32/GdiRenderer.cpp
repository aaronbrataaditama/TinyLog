// WP-F1: GDI painting for the bubble. Spec 3.2, 3.3, 3.5.
#include "win32/GdiRenderer.h"

#include <algorithm>

#include "win32/Theme.h"

namespace gdi {
namespace {

constexpr COLORREF kExcelPage = RGB(0x21, 0xA3, 0x66);
constexpr COLORREF kBtnDisabled = RGB(0x2A, 0x31, 0x40);

int Px(int logical, UINT dpi) { return theme::Scale(logical, dpi); }

int Round(float v) { return static_cast<int>(v + (v >= 0.0f ? 0.5f : -0.5f)); }

int Width(const RECT& r) { return r.right - r.left; }
int Height(const RECT& r) { return r.bottom - r.top; }

// pctFg percent of fg over bg.
COLORREF Blend(COLORREF fg, COLORREF bg, int pctFg) {
    auto mix = [pctFg](int a, int b) { return (a * pctFg + b * (100 - pctFg)) / 100; };
    return RGB(mix(GetRValue(fg), GetRValue(bg)), mix(GetGValue(fg), GetGValue(bg)), mix(GetBValue(fg), GetBValue(bg)));
}

COLORREF Scaled(COLORREF c, int pct) {
    return RGB(GetRValue(c) * pct / 100, GetGValue(c) * pct / 100, GetBValue(c) * pct / 100);
}

void FillSolid(HDC dc, const RECT& rc, COLORREF c) {
    UniqueGdi<HBRUSH> br(CreateSolidBrush(c));
    if (br) FillRect(dc, &rc, br.get());
}

// Rounded rect with an inside-frame border (the shape never exceeds rc).
void RoundBox(HDC dc, const RECT& rc, int radius, COLORREF fill, COLORREF border, int borderW) {
    UniqueGdi<HBRUSH> br(CreateSolidBrush(fill));
    UniqueGdi<HPEN> pen(CreatePen(PS_INSIDEFRAME, std::max(1, borderW), border));
    if (!br || !pen) return;
    SelectGuard gb(dc, br.get());
    SelectGuard gp(dc, pen.get());
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
}

void RoundRing(HDC dc, const RECT& rc, int radius, COLORREF color, int w) {
    UniqueGdi<HPEN> pen(CreatePen(PS_INSIDEFRAME, std::max(1, w), color));
    if (!pen) return;
    SelectGuard gb(dc, GetStockObject(NULL_BRUSH));
    SelectGuard gp(dc, pen.get());
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
}

void Disc(HDC dc, int cx, int cy, int diameter, COLORREF c) {
    UniqueGdi<HBRUSH> br(CreateSolidBrush(c));
    UniqueGdi<HPEN> pen(CreatePen(PS_SOLID, 1, c));
    if (!br || !pen) return;
    SelectGuard gb(dc, br.get());
    SelectGuard gp(dc, pen.get());
    int r = diameter / 2;
    Ellipse(dc, cx - r, cy - r, cx - r + diameter, cy - r + diameter);
}

int TextWidth(HDC dc, HFONT font, const std::wstring& s) {
    SelectGuard gf(dc, font);
    SIZE sz{};
    if (!GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz)) return 0;
    return sz.cx;
}

void DrawLine(HDC dc, const wchar_t* s, RECT rc, HFONT font, COLORREF color, UINT flags) {
    SelectGuard gf(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, s, -1, &rc, flags | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
}

HFONT MakeFont(int pt, UINT dpi, int weight, const wchar_t* face) {
    return CreateFontW(-MulDiv(pt, static_cast<int>(dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void DrawClock(HDC dc, int cx, int cy, int diameter, COLORREF color, UINT dpi) {
    int w = std::max(1, Px(2, dpi));
    UniqueGdi<HPEN> pen(CreatePen(PS_SOLID, w, color));
    if (!pen) return;
    SelectGuard gb(dc, GetStockObject(NULL_BRUSH));
    SelectGuard gp(dc, pen.get());
    int r = diameter / 2;
    Ellipse(dc, cx - r, cy - r, cx - r + diameter, cy - r + diameter);
    MoveToEx(dc, cx, cy - Px(5, dpi), nullptr);
    LineTo(dc, cx, cy);
    LineTo(dc, cx + Px(4, dpi), cy + Px(2, dpi));
}

}  // namespace

SIZE BubbleSizeFor(UINT dpi, BubbleSide side) {
    int w = Px(theme::kWidth, dpi);
    int h = Px(theme::kHeight, dpi);
    int p = Px(theme::kPointerH, dpi);
    if (side == BubbleSide::Left || side == BubbleSide::Right) return {w + p, h};
    return {w, h + p};
}

Geometry MakeGeometry(UINT dpi, BubbleSide side, int tipAlongEdge) {
    Geometry g;
    g.dpi = dpi;
    g.side = side;
    g.size = BubbleSizeFor(dpi, side);
    const int w = Px(theme::kWidth, dpi);
    const int h = Px(theme::kHeight, dpi);
    const int p = Px(theme::kPointerH, dpi);
    g.radius = Px(theme::kRadius, dpi);
    g.ptrW = Px(theme::kPointerW, dpi);
    g.ptrH = p;

    switch (side) {
        case BubbleSide::Above: g.body = {0, 0, w, h}; break;
        case BubbleSide::Below: g.body = {0, p, w, p + h}; break;
        case BubbleSide::Left:  g.body = {0, 0, w, h}; break;
        case BubbleSide::Right: g.body = {p, 0, p + w, h}; break;
    }

    const int margin = g.radius + g.ptrW / 2 + 1;
    if (side == BubbleSide::Above || side == BubbleSide::Below) {
        g.tip = std::clamp(tipAlongEdge, static_cast<int>(g.body.left) + margin, static_cast<int>(g.body.right) - margin);
    } else {
        g.tip = std::clamp(tipAlongEdge, static_cast<int>(g.body.top) + margin, static_cast<int>(g.body.bottom) - margin);
    }

    const int ox = g.body.left;
    const int oy = g.body.top;
    const int pad = Px(theme::kPadding, dpi);

    g.headerRow = {ox + pad, oy + Px(theme::kHeaderY, dpi), ox + w - pad, oy + Px(theme::kHeaderY, dpi) + Px(theme::kHeaderH, dpi)};
    const int dotD = Px(10, dpi);
    int cy = (g.headerRow.top + g.headerRow.bottom) / 2;
    g.dot = {g.headerRow.left, cy - dotD / 2, g.headerRow.left + dotD, cy - dotD / 2 + dotD};
    g.pill = {g.headerRow.right - Px(theme::kTimerPillW, dpi), g.headerRow.top, g.headerRow.right, g.headerRow.bottom};

    g.notesBox = {ox + pad, oy + Px(theme::kNotesY, dpi), ox + w - pad, oy + Px(theme::kNotesY, dpi) + Px(theme::kNotesH, dpi)};
    const int inset = Px(theme::kNotesInset, dpi);
    g.edit = {g.notesBox.left + inset, g.notesBox.top + inset, g.notesBox.right - inset, g.notesBox.bottom - inset};

    const int gap = Px(theme::kButtonGap, dpi);
    const int btnW = (w - 2 * pad - gap) / 2;
    const int btnTop = oy + Px(theme::kButtonsY, dpi);
    const int btnBot = btnTop + Px(theme::kButtonsH, dpi);
    g.startBtn = {ox + pad, btnTop, ox + pad + btnW, btnBot};
    g.exportBtn = {ox + pad + btnW + gap, btnTop, ox + w - pad, btnBot};
    return g;
}

UniqueGdi<HRGN> CreateShapeRegion(const Geometry& g, int curW, int curH) {
    const float sx = g.size.cx > 0 ? static_cast<float>(curW) / static_cast<float>(g.size.cx) : 1.0f;
    const float sy = g.size.cy > 0 ? static_cast<float>(curH) / static_cast<float>(g.size.cy) : 1.0f;
    const RECT b = {Round(static_cast<float>(g.body.left) * sx), Round(static_cast<float>(g.body.top) * sy),
                    Round(static_cast<float>(g.body.right) * sx), Round(static_cast<float>(g.body.bottom) * sy)};
    const int rad = std::max(1, Round(static_cast<float>(g.radius) * std::min(sx, sy)));
    const int hw = std::max(1, Round(static_cast<float>(g.ptrW) * std::min(sx, sy) / 2.0f));
    const int ph = std::max(1, Round(static_cast<float>(g.ptrH) * std::min(sx, sy)));

    UniqueGdi<HRGN> body(CreateRoundRectRgn(b.left, b.top, b.right + 1, b.bottom + 1, rad * 2, rad * 2));
    if (!body) return body;

    POINT pts[3]{};
    const bool horizontalEdge = g.side == BubbleSide::Above || g.side == BubbleSide::Below;
    const int tip = Round(static_cast<float>(g.tip) * (horizontalEdge ? sx : sy));
    switch (g.side) {
        case BubbleSide::Above:
            pts[0] = {tip - hw, b.bottom - 1};
            pts[1] = {tip + hw, b.bottom - 1};
            pts[2] = {tip, b.bottom + ph};
            break;
        case BubbleSide::Below:
            pts[0] = {tip - hw, b.top + 1};
            pts[1] = {tip + hw, b.top + 1};
            pts[2] = {tip, b.top - ph};
            break;
        case BubbleSide::Left:
            pts[0] = {b.right - 1, tip - hw};
            pts[1] = {b.right - 1, tip + hw};
            pts[2] = {b.right + ph, tip};
            break;
        case BubbleSide::Right:
            pts[0] = {b.left + 1, tip - hw};
            pts[1] = {b.left + 1, tip + hw};
            pts[2] = {b.left - ph, tip};
            break;
    }
    UniqueGdi<HRGN> tri(CreatePolygonRgn(pts, 3, ALTERNATE));
    if (tri) CombineRgn(body.get(), body.get(), tri.get(), RGN_OR);
    return body;
}

bool Fonts::Create(UINT dpi) {
    // All-or-nothing: on failure the existing fonts are left untouched.
    UniqueGdi<HFONT> t(MakeFont(15, dpi, FW_SEMIBOLD, L"Segoe UI Semibold"));
    UniqueGdi<HFONT> tm(MakeFont(14, dpi, FW_SEMIBOLD, L"Segoe UI Semibold"));
    UniqueGdi<HFONT> n(MakeFont(11, dpi, FW_NORMAL, L"Segoe UI"));
    UniqueGdi<HFONT> b(MakeFont(12, dpi, FW_SEMIBOLD, L"Segoe UI Semibold"));
    if (!t || !tm || !n || !b) return false;
    title = std::move(t);
    timer = std::move(tm);
    notes = std::move(n);
    button = std::move(b);
    return true;
}

void PaintBubble(HDC dc, const Geometry& g, const Fonts& fonts, const ViewState& view) {
    const UINT dpi = g.dpi;
    const RECT all = {0, 0, g.size.cx, g.size.cy};
    FillSolid(dc, all, theme::kBubbleBg);

    {   // 1 px outline of card + pointer
        UniqueGdi<HRGN> rgn = CreateShapeRegion(g, g.size.cx, g.size.cy);
        UniqueGdi<HBRUSH> br(CreateSolidBrush(theme::kBorder));
        if (rgn && br) {
            int bw = std::max(1, Px(1, dpi));
            FrameRgn(dc, rgn.get(), br.get(), bw, bw);
        }
    }

    // Header: status dot (with soft halo while recording), title, divider.
    const int cy = (g.headerRow.top + g.headerRow.bottom) / 2;
    const int dotCx = (g.dot.left + g.dot.right) / 2;
    if (view.recording) Disc(dc, dotCx, cy, Px(18, dpi), Blend(theme::kRecording, theme::kBubbleBg, 16));
    Disc(dc, dotCx, cy, Px(10, dpi), view.recording ? theme::kRecording : theme::kTextSecondary);

    const wchar_t* title = L"TinyLog";
    const int titleX = g.dot.right + Px(10, dpi);
    const int titleW = TextWidth(dc, fonts.title.get(), title);
    RECT titleRc = {titleX, g.headerRow.top, titleX + titleW + Px(4, dpi), g.headerRow.bottom};
    DrawLine(dc, title, titleRc, fonts.title.get(), theme::kTextPrimary, DT_LEFT);
    const int divX = titleX + titleW + Px(14, dpi);
    RECT div = {divX, cy - Px(10, dpi), divX + std::max(1, Px(1, dpi)), cy + Px(10, dpi)};
    FillSolid(dc, div, theme::kBorder);

    // Timer pill.
    const COLORREF pillFill = view.recording ? Blend(theme::kRecording, theme::kBubbleBg, 14) : theme::kEditorBg;
    const COLORREF pillBorder = view.recording ? Blend(theme::kRecording, theme::kBubbleBg, 55) : theme::kBorder;
    const COLORREF pillText = view.recording ? theme::kRecording : theme::kTextSecondary;
    RoundBox(dc, g.pill, Height(g.pill) / 2, pillFill, pillBorder, Px(1, dpi));
    const std::wstring timerText = view.timerText ? *view.timerText : std::wstring(L"00:00:00");
    const int glyphD = Px(16, dpi);
    const int gapG = Px(8, dpi);
    const int textW = TextWidth(dc, fonts.timer.get(), timerText);
    const int content = glyphD + gapG + textW;
    const int startX = g.pill.left + (Width(g.pill) - content) / 2;
    const int pcy = (g.pill.top + g.pill.bottom) / 2;
    DrawClock(dc, startX + glyphD / 2, pcy, glyphD, pillText, dpi);
    RECT trc = {startX + glyphD + gapG, g.pill.top, startX + content + Px(2, dpi), g.pill.bottom};
    DrawLine(dc, timerText.c_str(), trc, fonts.timer.get(), pillText, DT_LEFT);

    // Notes frame.
    RoundBox(dc, g.notesBox, Px(10, dpi), theme::kEditorBg, view.notesFocused ? theme::kAccent : theme::kBorder,
             Px(view.notesFocused ? 2 : 1, dpi));
}

void PaintNotesText(HDC dc, const Geometry& g, const Fonts& fonts, const std::wstring& notes) {
    if (notes.empty()) return;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, g.edit.left, g.edit.top, g.edit.right, g.edit.bottom);
    {
        SelectGuard gf(dc, fonts.notes.get());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme::kTextPrimary);
        RECT rc = g.edit;
        DrawTextW(dc, notes.c_str(), static_cast<int>(notes.size()), &rc, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    }
    if (saved != 0) RestoreDC(dc, saved);
}

void PaintButton(HDC dc, const RECT& rc, ButtonKind kind, const ButtonVisual& vis, const ViewState& view,
                 const Fonts& fonts, UINT dpi) {
    FillSolid(dc, rc, theme::kBubbleBg);

    const bool disabled = vis.disabled || (kind == ButtonKind::StartStop && view.busy);
    COLORREF base = kind == ButtonKind::StartStop ? theme::kStopBtn : theme::kExportBtn;
    COLORREF hover = kind == ButtonKind::StartStop ? theme::kStopBtnHover : theme::kExportHover;
    COLORREF fill = base;
    if (disabled) fill = kBtnDisabled;
    else if (vis.pressed) fill = Scaled(hover, 85);
    else if (vis.hover) fill = hover;
    const COLORREF textColor = disabled ? theme::kTextSecondary : RGB(0xFF, 0xFF, 0xFF);
    const COLORREF border = kind == ButtonKind::Export ? theme::kBorder : fill;
    const int radius = Px(10, dpi);
    RoundBox(dc, rc, radius, fill, border, Px(1, dpi));

    const int cy = (rc.top + rc.bottom) / 2;
    const int gap = Px(kind == ButtonKind::Export ? 8 : 12, dpi);
    const HFONT font = fonts.button.get();

    if (kind == ButtonKind::StartStop) {
        const wchar_t* label = view.recording ? L"Stop" : L"Start";
        const int gw = Px(16, dpi);
        const int tw = TextWidth(dc, font, label);
        const int x0 = rc.left + (Width(rc) - (gw + gap + tw)) / 2;
        const int gcx = x0 + gw / 2;
        UniqueGdi<HBRUSH> br(CreateSolidBrush(textColor));
        UniqueGdi<HPEN> pen(CreatePen(PS_SOLID, 1, textColor));
        if (br && pen) {
            SelectGuard gb(dc, br.get());
            SelectGuard gp(dc, pen.get());
            if (view.recording) {
                const int s = Px(14, dpi);
                RoundRect(dc, gcx - s / 2, cy - s / 2, gcx - s / 2 + s, cy - s / 2 + s, Px(4, dpi), Px(4, dpi));
            } else {
                const int s = Px(14, dpi);
                POINT tri[3] = {{gcx - s / 3, cy - s / 2}, {gcx - s / 3, cy + s / 2}, {gcx + s * 2 / 3, cy}};
                Polygon(dc, tri, 3);
            }
        }
        RECT trc = {x0 + gw + gap, rc.top, x0 + gw + gap + tw + Px(4, dpi), rc.bottom};
        DrawLine(dc, label, trc, font, textColor, DT_LEFT);
    } else {
        const wchar_t* label = L"Export to Excel";
        const int gw = Px(24, dpi);
        const int tw = TextWidth(dc, font, label);
        const int x0 = rc.left + (Width(rc) - (gw + gap + tw)) / 2;
        const int sq = Px(18, dpi);
        const COLORREF pageC = disabled ? theme::kTextSecondary : kExcelPage;
        const COLORREF sqC = disabled ? theme::kBorder : theme::kExcelGreen;
        RECT page = {x0 + Px(8, dpi), cy - Px(11, dpi), x0 + gw, cy + Px(11, dpi)};
        RoundBox(dc, page, Px(3, dpi), pageC, pageC, 1);
        RECT square = {x0, cy - sq / 2, x0 + sq, cy - sq / 2 + sq};
        RoundBox(dc, square, Px(3, dpi), sqC, sqC, 1);
        DrawLine(dc, L"X", square, font, textColor, DT_CENTER);
        RECT trc = {x0 + gw + gap, rc.top, x0 + gw + gap + tw + Px(4, dpi), rc.bottom};
        DrawLine(dc, label, trc, font, textColor, DT_LEFT);
    }

    if (vis.focus && !disabled) RoundRing(dc, rc, radius, theme::kAccent, Px(2, dpi));
}

void PaintButtonBuffered(HDC target, const RECT& rc, ButtonKind kind, const ButtonVisual& vis, const ViewState& view,
                         const Fonts& fonts, UINT dpi) {
    const int w = Width(rc);
    const int h = Height(rc);
    UniqueDC mem(CreateCompatibleDC(target));
    UniqueGdi<HBITMAP> bmp(CreateCompatibleBitmap(target, w, h));
    if (!mem || !bmp) {
        PaintButton(target, rc, kind, vis, view, fonts, dpi);   // unbuffered fallback
        return;
    }
    {
        SelectGuard gb(mem.get(), bmp.get());
        const RECT local = {0, 0, w, h};
        PaintButton(mem.get(), local, kind, vis, view, fonts, dpi);
        BitBlt(target, rc.left, rc.top, w, h, mem.get(), 0, 0, SRCCOPY);
    }
}

UniqueGdi<HBITMAP> CreateSnapshot(HDC ref, const Geometry& g, const Fonts& fonts, const ViewState& view,
                                  const std::wstring& notes) {
    UniqueDC mem(CreateCompatibleDC(ref));
    UniqueGdi<HBITMAP> bmp(CreateCompatibleBitmap(ref, g.size.cx, g.size.cy));
    if (!mem || !bmp) return UniqueGdi<HBITMAP>();
    {
        SelectGuard gb(mem.get(), bmp.get());
        PaintBubble(mem.get(), g, fonts, view);
        PaintNotesText(mem.get(), g, fonts, notes);
        PaintButton(mem.get(), g.startBtn, ButtonKind::StartStop, ButtonVisual{}, view, fonts, g.dpi);
        PaintButton(mem.get(), g.exportBtn, ButtonKind::Export, ButtonVisual{}, view, fonts, g.dpi);
    }
    return bmp;
}

void DrawSnapshot(HDC dst, const RECT& dstRect, HBITMAP snapshot, SIZE snapshotSize) {
    if (!snapshot) return;
    UniqueDC mem(CreateCompatibleDC(dst));
    if (!mem) return;
    SelectGuard gb(mem.get(), snapshot);
    SetStretchBltMode(dst, HALFTONE);
    SetBrushOrgEx(dst, 0, 0, nullptr);
    StretchBlt(dst, dstRect.left, dstRect.top, Width(dstRect), Height(dstRect), mem.get(), 0, 0, snapshotSize.cx,
               snapshotSize.cy, SRCCOPY);
}

}  // namespace gdi
