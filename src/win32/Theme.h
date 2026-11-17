#pragma once
// Spec 3.3 palette + bubble layout constants (logical px at 96 DPI). Values owned by WP-F1 (may tune);
// names are a contract.
#include <windows.h>

namespace theme {

inline constexpr COLORREF kBubbleBg      = RGB(0x17, 0x1B, 0x26);
inline constexpr COLORREF kEditorBg      = RGB(0x11, 0x16, 0x21);
inline constexpr COLORREF kTextPrimary   = RGB(0xF3, 0xF5, 0xFA);
inline constexpr COLORREF kTextSecondary = RGB(0x9A, 0xA5, 0xB5);
inline constexpr COLORREF kBorder        = RGB(0x34, 0x3D, 0x50);
inline constexpr COLORREF kRecording     = RGB(0x32, 0xD1, 0x6D);
inline constexpr COLORREF kStopBtn       = RGB(0x15, 0x96, 0x4A);
inline constexpr COLORREF kStopBtnHover  = RGB(0x11, 0x7D, 0x3D);
inline constexpr COLORREF kExportBtn     = RGB(0x25, 0x2D, 0x3D);
inline constexpr COLORREF kExportHover   = RGB(0x2E, 0x38, 0x4C);
inline constexpr COLORREF kAccent        = RGB(0x39, 0x87, 0xFF);
inline constexpr COLORREF kExcelGreen    = RGB(0x1D, 0x6F, 0x42);

// Layout (logical px)
inline constexpr int kWidth        = 360;
inline constexpr int kPadding      = 16;
inline constexpr int kRadius       = 14;
inline constexpr int kPointerW     = 16;
inline constexpr int kPointerH     = 9;
inline constexpr int kHeaderY      = 14;
inline constexpr int kHeaderH      = 32;
inline constexpr int kNotesY       = 58;
inline constexpr int kNotesH       = 84;
inline constexpr int kNotesInset   = 10;
inline constexpr int kButtonsY     = 154;
inline constexpr int kButtonsH     = 44;
inline constexpr int kButtonGap    = 12;
inline constexpr int kHeight       = kButtonsY + kButtonsH + kPadding;   // 214, excluding pointer
inline constexpr int kGapToIcon    = 6;
inline constexpr int kTimerPillW   = 128;

inline int Scale(int logical, UINT dpi) { return MulDiv(logical, static_cast<int>(dpi), 96); }

}  // namespace theme
