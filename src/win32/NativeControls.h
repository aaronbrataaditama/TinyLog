#pragma once
// Owned by WP-F1 (not a frozen contract). Creation/fonts/colors of the EDIT and the two owner-drawn buttons.
#include <windows.h>

namespace controls {

inline constexpr int kIdNotes     = 101;
inline constexpr int kIdStartStop = 102;
inline constexpr int kIdExport    = 103;

// Mouse-hover tracking for an owner-drawn button (the subclass updates it and repaints the button).
struct HoverState {
    bool hover = false;
    bool tracking = false;
};

// Child controls are created hidden (no WS_VISIBLE); the bubble shows them after the open animation.
// Creation order defines the Tab order: EDIT, Start/Stop, Export.
HWND CreateNotesEdit(HWND parent, HINSTANCE inst);
// hover must outlive the button; the subclass is removed automatically when the button is destroyed.
HWND CreateOwnerDrawButton(HWND parent, HINSTANCE inst, int id, const wchar_t* text, HoverState* hover);

void SetControlFont(HWND control, HFONT font);
void ApplyDarkTheme(HWND control);   // best effort, ignores failure

}  // namespace controls
