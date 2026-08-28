// OverlaySignature.h — change detection for a composited scrollbar overlay.
//
// WHY THIS EXISTS (a bug this guards against, twice reported on hardware):
// A control that scrolls on the compositor keeps its scrollbar + focus ring on a
// separate "overlay" composition surface. While the scrollbar is visible its host
// keeps asking for animation ticks (the idle-hide countdown runs for over a second),
// and re-rasterizing a LIVE composition surface every frame makes its antialiased
// edges shimmer. So an overlay must only be redrawn when its pixels would actually
// differ.
//
// This is the set of values those pixels depend on. Both TreeView and TextArea embed
// a ScrollViewer and draw the same overlay content, so they share one signature type
// rather than each hand-rolling the comparison.
//
// USAGE: sample AFTER pushing the current offset into the scroll model (thumbY
// depends on it), compare against the last-rasterized signature, and redraw only on
// a difference (or when the caller forces it — a fresh surface after a resize / DPI
// change / device restore has no valid previous pixels).
#pragma once

#include <cmath>

namespace fluent {

struct OverlaySignature {
    float visibility = 0.0f;    // scrollbar fade (0..1)
    float expand = 0.0f;        // thin rail -> pill (0..1)
    float thumbY = 0.0f;        // thumb top (DIP)
    bool focused = false;       // focus ring drawn?
    bool dragging = false;      // thumb held (widened)?
    // Horizontal rail (NoWrap text). Left at zero by a control with no horizontal
    // axis, so a vertical-only overlay compares exactly as it did before these
    // existed — which is what keeps TreeView's redraw gating untouched.
    float expandX = 0.0f;       // horizontal thin rail -> pill (0..1)
    float thumbX = 0.0f;        // horizontal thumb left (DIP)
    bool draggingX = false;     // horizontal thumb held?

    // Tolerances are "smaller than one rendered step": ~1/255 for the 0..1 factors
    // (below that the blend rounds to the same pixel) and half a DIP for the thumbs.
    // Booleans compare exactly.
    bool Differs(const OverlaySignature& o) const {
        return focused != o.focused || dragging != o.dragging ||
               draggingX != o.draggingX ||
               std::fabs(visibility - o.visibility) > 0.004f ||
               std::fabs(expand - o.expand) > 0.004f ||
               std::fabs(expandX - o.expandX) > 0.004f ||
               std::fabs(thumbY - o.thumbY) > 0.5f ||
               std::fabs(thumbX - o.thumbX) > 0.5f;
    }
};

} // namespace fluent
