// WP-F1: native child controls of the bubble. Spec 3.4.
#include "win32/NativeControls.h"

#include <commctrl.h>
#include <uxtheme.h>

namespace controls {
namespace {

constexpr UINT_PTR kHoverSubclassId = 1;

LRESULT CALLBACK HoverProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    HoverState* st = reinterpret_cast<HoverState*>(ref);
    switch (msg) {
        case WM_MOUSEMOVE:
            if (st) {
                if (!st->tracking) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                    if (TrackMouseEvent(&tme)) st->tracking = true;
                }
                if (!st->hover) {
                    st->hover = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;
        case WM_MOUSELEAVE:
            if (st) {
                st->tracking = false;
                st->hover = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, HoverProc, id);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

HMENU IdAsMenu(int id) { return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)); }

}  // namespace

HWND CreateNotesEdit(HWND parent, HINSTANCE inst) {
    return CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0,
                           10, 10, parent, IdAsMenu(kIdNotes), inst, nullptr);
}

HWND CreateOwnerDrawButton(HWND parent, HINSTANCE inst, int id, const wchar_t* text, HoverState* hover) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, parent, IdAsMenu(id),
                             inst, nullptr);
    if (h && hover) SetWindowSubclass(h, HoverProc, kHoverSubclassId, reinterpret_cast<DWORD_PTR>(hover));
    return h;
}

void SetControlFont(HWND control, HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void ApplyDarkTheme(HWND control) {
    if (control) SetWindowTheme(control, L"DarkMode_Explorer", nullptr);   // best effort
}

}  // namespace controls
