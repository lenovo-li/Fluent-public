// PopupGeometry.h — pure anchor-rect geometry for popup-owning controls (WP-06).
//
// ComboBox (HeaderScreenRect) and MenuBar (ItemScreenRect) computed the same
// thing: given the owner window's screen rect, a child sub-rect in DIPs, and the
// DPI scale, produce the child's rect in physical screen pixels for anchoring a
// popup. AnchorScreenRect extracts that arithmetic as a pure function (no HWND, no
// Win32 call) so both share one definition and it can be unit-tested. The Win32
// half (GetWindowRect) stays at the call site.
#pragma once

#include <windows.h>

namespace fluent {

// Map a DIP sub-rect (xDip,yDip,wDip,hDip) inside a window whose top-left is at
// screen pixel (winLeft, winTop) to a physical-pixel screen RECT, using the
// window's DPI scale `s` (= dpi/96). Rounding matches the historical
// static_cast<int>(v * s + 0.5f) used by both controls.
inline RECT AnchorScreenRect(int winLeft, int winTop,
                             float xDip, float yDip, float wDip, float hDip,
                             float s) {
    RECT r;
    r.left   = winLeft + static_cast<int>(xDip * s + 0.5f);
    r.top    = winTop  + static_cast<int>(yDip * s + 0.5f);
    r.right  = r.left  + static_cast<int>(wDip * s + 0.5f);
    r.bottom = r.top   + static_cast<int>(hDip * s + 0.5f);
    return r;
}

} // namespace fluent
