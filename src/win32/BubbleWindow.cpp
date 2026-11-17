// WP-F1: the bubble popup window. Spec 3.1-3.6, 8.5, 8.6, 8.8, 9.1, 9.2.
#include "win32/BubbleWindow.h"

#include <algorithm>

#include "util/Handles.h"
#include "util/TimeHelpers.h"
#include "win32/BubblePlacement.h"
#include "win32/GdiRenderer.h"
#include "win32/LayeredAnimation.h"
#include "win32/NativeControls.h"
#include "win32/Theme.h"

namespace {

constexpr wchar_t kClassName[] = L"TinyLogBubbleWindow";
constexpr UINT_PTR kDisplayTimerId = 1;

constexpr UINT kReactivateMsg = WM_APP + 1;          // one-shot: take the foreground back after a taskbar click
constexpr ULONGLONG kTaskbarGraceMs = 500;           // deactivation by the taskbar this soon after open is the double-click

bool IsTaskbarClass(HWND w) {
    if (!w) return false;
    wchar_t cls[64] = {};
    if (GetClassNameW(w, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0]))) <= 0) return false;
    return wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
           wcscmp(cls, L"NotifyIconOverflowWindow") == 0;
}

bool IsTaskbarWindow(HWND w) {
    if (!w) return false;
    if (IsTaskbarClass(w)) return true;
    const HWND root = GetAncestor(w, GA_ROOT);
    return root && root != w && IsTaskbarClass(root);
}

int RectW(const RECT& r) { return r.right - r.left; }
int RectH(const RECT& r) { return r.bottom - r.top; }

// Window rect for `side` that touches the anchor, clamped into the work area.
RECT RectFromAnchor(POINT anchor, BubbleSide side, SIZE sz, int gap, const RECT& work) {
    LONG l = 0;
    LONG t = 0;
    switch (side) {
        case BubbleSide::Above: l = anchor.x - sz.cx / 2; t = anchor.y - gap - sz.cy; break;
        case BubbleSide::Below: l = anchor.x - sz.cx / 2; t = anchor.y + gap; break;
        case BubbleSide::Left:  l = anchor.x - gap - sz.cx; t = anchor.y - sz.cy / 2; break;
        case BubbleSide::Right: l = anchor.x + gap; t = anchor.y - sz.cy / 2; break;
    }
    l = std::max(work.left, std::min(l, work.right - sz.cx));
    t = std::max(work.top, std::min(t, work.bottom - sz.cy));
    return {l, t, l + sz.cx, t + sz.cy};
}

Placement ResolvePlacement(const RECT* icon, POINT pt, const RECT& work, UINT dpi) {
    const int gap = theme::Scale(theme::kGapToIcon, dpi);
    auto compute = [&](SIZE s) {
        return icon ? ComputePlacement(*icon, work, s, gap) : FallbackPlacement(pt, work, s, gap);
    };
    Placement p = compute(gdi::BubbleSizeFor(dpi, BubbleSide::Above));
    if (p.side == BubbleSide::Left || p.side == BubbleSide::Right) p = compute(gdi::BubbleSizeFor(dpi, p.side));
    // Whatever the placement code returned, the window must have exactly the size its side needs.
    const SIZE want = gdi::BubbleSizeFor(dpi, p.side);
    if (RectW(p.rect) != want.cx || RectH(p.rect) != want.cy) p.rect = RectFromAnchor(p.anchor, p.side, want, gap, work);
    return p;
}

}  // namespace

struct BubbleWindow::Impl {
    enum class State { Hidden, Opening, Open, Closing };

    IBubbleHost& host;
    HINSTANCE    inst;
    HWND         owner;

    HWND hwnd = nullptr;
    HWND edit = nullptr;
    HWND startBtn = nullptr;
    HWND exportBtn = nullptr;

    State        state = State::Hidden;
    BubbleModel  model;
    std::wstring timerText = L"00:00:00";
    std::wstring pendingNotes;   // used only while the EDIT does not exist yet

    bool modalGuard = false;
    bool suppressNotify = false;
    bool kbNav = false;          // keyboard navigation used: draw focus rings on buttons
    bool displayTimerOn = false;
    bool closeByDeactivation = false;
    bool reactivatePending = false;
    ULONGLONG openTick = 0;      // GetTickCount64 when the open animation last started

    UINT             dpi = 96;
    UINT               fontsDpi = 0;
    gdi::Fonts         fonts;
    UniqueGdi<HBRUSH>  editBrush;
    gdi::Geometry      geom;
    Placement          placement{};
    int                tipLogical = 0;   // pointer tip along its edge at 96 DPI (survives DPI changes)
    UniqueGdi<HBITMAP> snapshot;
    SIZE               snapSize{};
    LayeredAnimation   anim;
    controls::HoverState hoverStart;
    controls::HoverState hoverExport;

    Impl(IBubbleHost& h, HINSTANCE i, HWND o) : host(h), inst(i), owner(o) {}
    ~Impl() {
        anim.Cancel();
        if (hwnd) {
            KillTimer(hwnd, kDisplayTimerId);
            DestroyWindow(hwnd);   // children go with it; WM_NCDESTROY nulls the handles
        }
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // ---- creation ----
    bool EnsureWindow();
    bool EnsureFonts();
    void ApplyLayout();
    void ApplyShape(int curW, int curH);
    gdi::ViewState View() const;

    // ---- behaviour ----
    void Show(const RECT* iconRect, POINT fallbackPt);
    void BeginClose(bool animate, bool byDeactivation);
    void ApplyFrame(const LayeredAnimation::Frame& f);
    void OnAnimationTick();
    void CompleteOpen();
    void CompleteClose();
    void FinishAnimationNow();
    bool BuildSnapshot(bool notesFocused);
    void SetChildrenVisible(bool visible);
    bool UpdateTimerText();
    void UpdateDisplayTimer();
    void ApplyModel();
    std::wstring ReadNotes() const;
    bool PreTranslate(MSG& msg);

    // ---- window procedure ----
    LRESULT OnMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint(HDC dc);
    static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
};

// ===================================================================================================
// creation
// ===================================================================================================

bool BubbleWindow::Impl::EnsureWindow() {
    if (hwnd) return true;

    WNDCLASSEXW probe{};
    if (!GetClassInfoExW(inst, kClassName, &probe)) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
    }

    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kClassName, L"TinyLog",
                             WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, nullptr, inst, this);
    if (!h) return false;
    hwnd = h;   // also set from WM_NCCREATE
    if (!SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA)) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
        return false;
    }

    editBrush.reset(CreateSolidBrush(theme::kEditorBg));
    edit = controls::CreateNotesEdit(hwnd, inst);
    startBtn = controls::CreateOwnerDrawButton(hwnd, inst, controls::kIdStartStop, L"Start", &hoverStart);
    exportBtn = controls::CreateOwnerDrawButton(hwnd, inst, controls::kIdExport, L"Export to Excel", &hoverExport);
    if (!edit || !startBtn || !exportBtn || !editBrush) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
        return false;
    }
    controls::ApplyDarkTheme(edit);
    if (!pendingNotes.empty()) {
        suppressNotify = true;
        SetWindowTextW(edit, pendingNotes.c_str());
        suppressNotify = false;
        pendingNotes.clear();
    }
    return true;
}

bool BubbleWindow::Impl::EnsureFonts() {
    if (fontsDpi == dpi && fonts.title) return true;
    // Create into a local set: on failure the current fonts (still selected into the controls) stay untouched.
    gdi::Fonts nf;
    if (!nf.Create(dpi)) return false;
    // Point the controls at the new fonts first, then let the swap release the old ones.
    controls::SetControlFont(edit, nf.notes.get());
    controls::SetControlFont(startBtn, nf.button.get());
    controls::SetControlFont(exportBtn, nf.button.get());
    fonts.title = std::move(nf.title);
    fonts.timer = std::move(nf.timer);
    fonts.notes = std::move(nf.notes);
    fonts.button = std::move(nf.button);
    fontsDpi = dpi;
    return true;
}

void BubbleWindow::Impl::ApplyLayout() {
    geom = gdi::MakeGeometry(dpi, placement.side, MulDiv(tipLogical, static_cast<int>(dpi), 96));
    auto place = [](HWND h, const RECT& r) {
        if (h) SetWindowPos(h, nullptr, r.left, r.top, RectW(r), RectH(r), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(edit, geom.edit);
    place(startBtn, geom.startBtn);
    place(exportBtn, geom.exportBtn);
}

void BubbleWindow::Impl::ApplyShape(int curW, int curH) {
    UniqueGdi<HRGN> rgn = gdi::CreateShapeRegion(geom, curW, curH);
    if (!rgn) return;
    if (SetWindowRgn(hwnd, rgn.get(), FALSE)) rgn.release();   // ownership moved to the window
}

gdi::ViewState BubbleWindow::Impl::View() const {
    gdi::ViewState v;
    v.recording = model.recording;
    v.busy = model.busy;
    v.notesFocused = edit && GetFocus() == edit;
    v.timerText = &timerText;
    return v;
}

std::wstring BubbleWindow::Impl::ReadNotes() const {
    if (!edit) return pendingNotes;
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0) return {};
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(edit, s.data(), len + 1);
    s.resize(static_cast<size_t>(std::max(got, 0)));
    return s;
}

// ===================================================================================================
// model / timers
// ===================================================================================================

bool BubbleWindow::Impl::UpdateTimerText() {
    const int64_t elapsed = model.recording ? CaptureNow().utc - model.startUtc : 0;
    std::wstring t = FormatElapsed(elapsed);
    if (t == timerText) return false;
    timerText = std::move(t);
    return true;
}

void BubbleWindow::Impl::UpdateDisplayTimer() {
    if (!hwnd) return;
    const bool want = (state == State::Opening || state == State::Open) && model.recording;
    if (want && !displayTimerOn) {
        displayTimerOn = SetTimer(hwnd, kDisplayTimerId, 1000, nullptr) != 0;
    } else if (!want && displayTimerOn) {
        KillTimer(hwnd, kDisplayTimerId);
        displayTimerOn = false;
    }
}

void BubbleWindow::Impl::ApplyModel() {
    if (!hwnd) return;
    SetWindowTextW(startBtn, model.recording ? L"Stop" : L"Start");
    EnableWindow(startBtn, model.busy ? FALSE : TRUE);
    if (model.busy && state == State::Open && GetFocus() == startBtn) SetFocus(edit);
    UpdateTimerText();
    UpdateDisplayTimer();
    InvalidateRect(hwnd, nullptr, FALSE);
    InvalidateRect(startBtn, nullptr, FALSE);
}

void BubbleWindow::Impl::SetChildrenVisible(bool visible) {
    const int cmd = visible ? SW_SHOWNA : SW_HIDE;
    ShowWindow(edit, cmd);
    ShowWindow(startBtn, cmd);
    ShowWindow(exportBtn, cmd);
}

// ===================================================================================================
// open / close / animation
// ===================================================================================================

bool BubbleWindow::Impl::BuildSnapshot(bool notesFocused) {
    snapshot.reset();
    HDC wdc = GetDC(hwnd);
    if (!wdc) return false;
    gdi::ViewState v = View();
    v.notesFocused = notesFocused;
    snapshot = gdi::CreateSnapshot(wdc, geom, fonts, v, ReadNotes());
    ReleaseDC(hwnd, wdc);
    snapSize = geom.size;
    return static_cast<bool>(snapshot);
}

void BubbleWindow::Impl::Show(const RECT* iconRect, POINT fallbackPt) {
    if (state == State::Opening || state == State::Open) {
        if (state == State::Open) {
            SetForegroundWindow(hwnd);
            SetFocus(edit);
        }
        return;
    }
    if (!EnsureWindow()) return;

    if (state == State::Closing) {   // reopen while collapsing: drop the close animation
        anim.Cancel();
        snapshot.reset();
        state = State::Hidden;
    }

    // Move the (hidden) window onto the target monitor first so GetDpiForWindow reports that monitor's DPI.
    HMONITOR mon = iconRect ? MonitorFromRect(iconRect, MONITOR_DEFAULTTONEAREST)
                            : MonitorFromPoint(fallbackPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        mi.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        mi.rcMonitor = mi.rcWork;
    }
    SetWindowPos(hwnd, nullptr, mi.rcWork.left, mi.rcWork.top, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
    UINT d = GetDpiForWindow(hwnd);
    dpi = d ? d : 96;
    if (!EnsureFonts()) return;

    placement = ResolvePlacement(iconRect, fallbackPt, mi.rcWork, dpi);
    const bool horizontal = placement.side == BubbleSide::Above || placement.side == BubbleSide::Below;
    const int tipPx = horizontal ? placement.anchor.x - placement.rect.left : placement.anchor.y - placement.rect.top;
    tipLogical = MulDiv(tipPx, 96, static_cast<int>(dpi));
    ApplyLayout();
    ApplyModel();
    UpdateTimerText();
    kbNav = false;

    // Snapshot of the full-size bubble, then collapse the window to a small rect at the anchor.
    SetChildrenVisible(false);
    BuildSnapshot(true);
    state = State::Opening;
    openTick = GetTickCount64();
    const RECT r0 = ScaleRectAboutAnchor(placement.rect, placement.anchor, 0.10f);
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    SetWindowPos(hwnd, nullptr, r0.left, r0.top, std::max(1, RectW(r0)), std::max(1, RectH(r0)),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyShape(std::max(1, RectW(r0)), std::max(1, RectH(r0)));
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    UpdateDisplayTimer();

    anim.Start(hwnd, LayeredAnimation::Dir::Open, placement.rect, placement.anchor);
    if (!anim.Running()) OnAnimationTick();   // animation finished (or could not start): apply end frame now
}

void BubbleWindow::Impl::ApplyFrame(const LayeredAnimation::Frame& f) {
    const int w = std::max(1, RectW(f.rect));
    const int h = std::max(1, RectH(f.rect));
    SetWindowPos(hwnd, nullptr, f.rect.left, f.rect.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(hwnd, 0, f.alpha, LWA_ALPHA);
    ApplyShape(w, h);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
}

void BubbleWindow::Impl::OnAnimationTick() {
    if (state != State::Opening && state != State::Closing) {
        anim.Cancel();
        return;
    }
    const LayeredAnimation::Frame f = anim.Tick();
    ApplyFrame(f);
    if (!f.done) return;
    if (state == State::Opening) CompleteOpen();
    else CompleteClose();
}

void BubbleWindow::Impl::CompleteOpen() {
    state = State::Open;
    snapshot.reset();
    SetWindowPos(hwnd, nullptr, placement.rect.left, placement.rect.top, RectW(placement.rect), RectH(placement.rect),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ApplyShape(geom.size.cx, geom.size.cy);
    SetChildrenVisible(true);
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
    UpdateDisplayTimer();
    SetFocus(edit);
    const LRESULT len = SendMessageW(edit, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
}

void BubbleWindow::Impl::CompleteClose() {
    state = State::Hidden;
    snapshot.reset();
    UpdateDisplayTimer();
    ShowWindow(hwnd, SW_HIDE);
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    host.OnBubbleHidden(closeByDeactivation);
}

void BubbleWindow::Impl::FinishAnimationNow() {
    if (state == State::Opening) {
        anim.Cancel();
        CompleteOpen();
    } else if (state == State::Closing) {
        anim.Cancel();
        CompleteClose();
    }
}

void BubbleWindow::Impl::BeginClose(bool animate, bool byDeactivation) {
    if (!hwnd || (state != State::Open && state != State::Opening)) return;
    if (state == State::Opening) {   // snap the opening animation to its end, then collapse from there
        anim.Cancel();
        CompleteOpen();
    }
    closeByDeactivation = byDeactivation;

    if (!animate) {
        state = State::Hidden;
        snapshot.reset();
        UpdateDisplayTimer();
        ShowWindow(hwnd, SW_HIDE);
        host.OnBubbleHidden(byDeactivation);
        return;
    }

    BuildSnapshot(false);   // the EDIT is still the source of truth for the notes; the snapshot only mirrors it
    SetChildrenVisible(false);
    state = State::Closing;
    UpdateDisplayTimer();
    RECT cur{};
    GetWindowRect(hwnd, &cur);
    anim.Start(hwnd, LayeredAnimation::Dir::Close, cur, placement.anchor);
    if (!anim.Running()) OnAnimationTick();
}

// ===================================================================================================
// input
// ===================================================================================================

bool BubbleWindow::Impl::PreTranslate(MSG& msg) {
    if (!hwnd || (state != State::Open && state != State::Opening)) return false;
    if (msg.hwnd != hwnd && !IsChild(hwnd, msg.hwnd)) return false;

    if (msg.message == WM_KEYDOWN) {
        if (msg.wParam == VK_ESCAPE) {
            BeginClose(true, false);
            return true;
        }
        if (state != State::Open) return false;
        if (msg.wParam == VK_TAB && !kbNav) {
            kbNav = true;
            InvalidateRect(startBtn, nullptr, FALSE);
            InvalidateRect(exportBtn, nullptr, FALSE);
        }
        if (msg.wParam == VK_RETURN) {
            HWND f = GetFocus();
            if (f == startBtn || f == exportBtn) {
                SendMessageW(f, BM_CLICK, 0, 0);
                return true;
            }
        }
    }
    if (state != State::Open) return false;
    return IsDialogMessageW(hwnd, &msg) != FALSE;
}

// ===================================================================================================
// painting + window procedure
// ===================================================================================================

void BubbleWindow::Impl::OnPaint(HDC dc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if ((state == State::Opening || state == State::Closing) && snapshot) {
        gdi::DrawSnapshot(dc, rc, snapshot.get(), snapSize);
        return;
    }
    UniqueDC mem(CreateCompatibleDC(dc));
    UniqueGdi<HBITMAP> bmp(CreateCompatibleBitmap(dc, RectW(rc), RectH(rc)));
    const gdi::ViewState v = View();
    if (!mem || !bmp) {
        gdi::PaintBubble(dc, geom, fonts, v);
        return;
    }
    SelectGuard gb(mem.get(), bmp.get());
    gdi::PaintBubble(mem.get(), geom, fonts, v);
    BitBlt(dc, 0, 0, RectW(rc), RectH(rc), mem.get(), 0, 0, SRCCOPY);
}

LRESULT CALLBACK BubbleWindow::Impl::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Impl* self = nullptr;
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        self = static_cast<Impl*>(cs->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd = h;
    } else {
        self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->OnMessage(h, msg, wp, lp);
}

LRESULT BubbleWindow::Impl::OnMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (dc) OnPaint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (!dis || dis->CtlType != ODT_BUTTON) break;
            const bool isStart = dis->CtlID == static_cast<UINT>(controls::kIdStartStop);
            gdi::ButtonVisual vis;
            vis.hover = isStart ? hoverStart.hover : hoverExport.hover;
            vis.pressed = (dis->itemState & ODS_SELECTED) != 0;
            vis.focus = (dis->itemState & ODS_FOCUS) != 0 && kbNav;
            vis.disabled = (dis->itemState & ODS_DISABLED) != 0;
            const gdi::ViewState v = View();
            gdi::PaintButtonBuffered(dis->hDC, dis->rcItem, isStart ? gdi::ButtonKind::StartStop : gdi::ButtonKind::Export,
                                     vis, v, fonts, dpi);
            return TRUE;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            if (reinterpret_cast<HWND>(lp) != edit) break;
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, theme::kTextPrimary);
            SetBkColor(dc, theme::kEditorBg);
            return reinterpret_cast<LRESULT>(editBrush.get());
        }

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == controls::kIdStartStop && code == BN_CLICKED) {
                if (!model.busy) host.OnStartStopClicked();
            } else if (id == controls::kIdExport && code == BN_CLICKED) {
                host.OnExportClicked();
            } else if (id == controls::kIdNotes) {
                if (code == EN_CHANGE) {
                    if (!suppressNotify) host.OnNotesChanged(ReadNotes());
                } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    InvalidateRect(hwnd, &geom.notesBox, FALSE);
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wp == kDisplayTimerId) {
                if (state == State::Open && UpdateTimerText()) InvalidateRect(hwnd, &geom.pill, FALSE);
            } else if (wp == LayeredAnimation::kTimerId) {
                OnAnimationTick();
            }
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !modalGuard && (state == State::Open || state == State::Opening)) {
                HWND other = reinterpret_cast<HWND>(lp);
                const bool ownedByUs = other && (other == hwnd || GetWindow(other, GW_OWNER) == hwnd || IsChild(hwnd, other));
                if (!ownedByUs) {
                    // A double-click on the tray icon: NIN_SELECT opened us, the second mouse-down activates
                    // the taskbar. Ignore that deactivation and take the foreground back.
                    const HWND fg = other ? other : GetForegroundWindow();
                    if (openTick != 0 && GetTickCount64() - openTick < kTaskbarGraceMs && IsTaskbarWindow(fg)) {
                        if (!reactivatePending) {
                            reactivatePending = true;
                            PostMessageW(hwnd, kReactivateMsg, 0, 0);
                        }
                    } else {
                        BeginClose(true, true);
                    }
                }
            }
            return 0;

        case kReactivateMsg:
            reactivatePending = false;
            if (state == State::Open || state == State::Opening) SetForegroundWindow(hwnd);
            return 0;

        case WM_KEYDOWN:   // keys reaching the frame itself (focus on no child)
            if (wp == VK_ESCAPE) {
                BeginClose(true, false);
                return 0;
            }
            break;

        case WM_DPICHANGED: {
            FinishAnimationNow();
            const UINT nd = HIWORD(wp);
            const RECT* sug = reinterpret_cast<const RECT*>(lp);
            if (nd != 0) dpi = nd;
            if (edit && EnsureFonts()) {
                ApplyLayout();
                if (state == State::Open && sug) {
                    SetWindowPos(hwnd, nullptr, sug->left, sug->top, geom.size.cx, geom.size.cy,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    GetWindowRect(hwnd, &placement.rect);
                    ApplyShape(geom.size.cx, geom.size.cy);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_DISPLAYCHANGE:
            FinishAnimationNow();
            return 0;

        case WM_NCDESTROY:
            anim.Cancel();
            displayTimerOn = false;
            state = State::Hidden;
            snapshot.reset();
            hwnd = nullptr;
            edit = startBtn = exportBtn = nullptr;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;

        default:
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ===================================================================================================
// public API
// ===================================================================================================

BubbleWindow::BubbleWindow(HINSTANCE inst, HWND owner, IBubbleHost& host)
    : impl_(std::make_unique<Impl>(host, inst, owner)) {}

BubbleWindow::~BubbleWindow() = default;

void BubbleWindow::Show(const RECT* iconRect, POINT fallbackPt) { impl_->Show(iconRect, fallbackPt); }

void BubbleWindow::Hide(bool animate) { impl_->BeginClose(animate, false); }

bool BubbleWindow::IsVisible() const {
    return impl_->state == Impl::State::Opening || impl_->state == Impl::State::Open;
}

void BubbleWindow::SetModel(const BubbleModel& model) {
    impl_->model = model;
    impl_->ApplyModel();
}

void BubbleWindow::SetNotes(const std::wstring& notes) {
    if (!impl_->edit) {
        impl_->pendingNotes = notes;
        return;
    }
    impl_->suppressNotify = true;
    SetWindowTextW(impl_->edit, notes.c_str());
    impl_->suppressNotify = false;
}

std::wstring BubbleWindow::GetNotes() const { return impl_->ReadNotes(); }

void BubbleWindow::SetModalGuard(bool active) { impl_->modalGuard = active; }

bool BubbleWindow::PreTranslate(MSG& msg) { return impl_->PreTranslate(msg); }

HWND BubbleWindow::Hwnd() const { return impl_->hwnd; }
