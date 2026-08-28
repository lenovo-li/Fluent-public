// WindowState.h — plain window-placement data (no persistence).
//
// This is the FluentUI analogue of WPF's Window.RestoreBounds + WindowState:
// the window knows how to CAPTURE its placement and how to APPLY a saved one
// (including DPI rescaling and multi-monitor validation — genuine window/DPI
// knowledge that belongs in the rendering library), but it never decides where
// or how the data is stored. The application owns that (see FluentSettings).
//
// Geometry is in PHYSICAL PIXELS together with the DPI it was captured at, so a
// rect saved on a 200% monitor can be rescaled with MulDiv when restored on a
// 100% monitor (project documentation DPI pitfall #1).
#pragma once

#include "../fl_common.h"

namespace fluent {

// Non-client area configuration: the window reserves a narrow band on all four
// edges as true non-client area. This places the resize grip entirely outside
// the client rect, preventing it from conflicting with interactive content at
// window edges (scrollbars, edge-docked controls). The DComp content surface
// follows the client origin automatically (verified: 40px NC inset shifted
// content 40px inward), so no manual visual offsetting is needed.
constexpr float kResizeGripOuterDip = 8.0f;

struct WindowState {
    int x = 0, y = 0;          // top-left, physical pixels (normal/restored pos)
    int width = 0, height = 0; // physical pixels (normal/restored size)
    bool maximized = false;
    UINT dpi = 96;             // DPI the rect was captured at
    bool valid = false;        // false until a real state has been captured/set

    // True if this state's rect still intersects a connected display.
    bool OnMonitor() const {
        RECT r = {x, y, x + width, y + height};
        // MONITOR_DEFAULTTONULL => null when the rect is on no display.
        return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
    }
};

// The non-client band width in PHYSICAL PIXELS at a given DPI. Single source of
// truth for both sides of the WM_NCCALCSIZE / WM_NCHITTEST contract: the message
// handler insets rgrc[0] by this amount on all four edges, and HitTestNca claims
// exactly the same band as HTLEFT/HTRIGHT/HTTOP/HTBOTTOM. If the two disagree,
// the mismatched pixels are non-client (so the client area never sees the mouse)
// yet reported as HTCLIENT (so Windows gives no resize cursor) — the band goes
// dead in both directions, which is exactly the regression the first prototype
// produced.
//
// Rounds the same way as the WM_NCCALCSIZE arithmetic (+0.5 truncation) rather
// than rounding up like MinTrackSizePx: here the two call sites must agree to
// the pixel, which matters more than never under-reserving.
inline int ResizeGripOuterPx(UINT dpi) {
    if (dpi == 0) dpi = 96;
    return static_cast<int>(kResizeGripOuterDip * static_cast<float>(dpi) / 96.0f + 0.5f);
}

// A minimum draggable size expressed in DIPs, converted to the physical pixels
// WM_GETMINMAXINFO wants at the window's CURRENT DPI. Pure so the arithmetic is
// unit-testable headless.
//
// `outPx` is only written for a non-zero limit: 0 DIP means "no limit", and the
// caller must then leave the system's own MINMAXINFO value alone rather than
// overwrite it with 0 (which would be a floor of nothing but also discards whatever
// the system computed). Rounds UP, so the limit is never enforced a pixel short of
// what was asked for.
//
// ptMinTrackSize bounds the WINDOW rect, but the caller's limit describes the CLIENT
// area, so the reserved non-client bands must be added back. The bands are asymmetric:
// WM_NCCALCSIZE insets left+right (two bands on the width) and bottom only (one band on
// the height) — the top edge keeps no band, because a top inset makes DWM draw its own
// caption on top of ours.
inline bool MinTrackSizePx(float minWDip, float minHDip, UINT dpi, POINT& outPx) {
    if (dpi == 0) dpi = 96;
    const float s = static_cast<float>(dpi) / 96.0f;
    const int ncBand = ResizeGripOuterPx(dpi);
    bool any = false;
    if (minWDip > 0.0f) {
        outPx.x = static_cast<LONG>(minWDip * s + 0.999f) + 2 * ncBand;  // left + right
        any = true;
    }
    if (minHDip > 0.0f) {
        outPx.y = static_cast<LONG>(minHDip * s + 0.999f) + ncBand;      // bottom only
        any = true;
    }
    return any;
}

} // namespace fluent
