// DirtyRegion.h — pure partial-redraw planner (roadmap §14, WP-07 §S4).
//
// Extracted from NativeWindowHost::RenderNow so the "full vs partial redraw"
// decision is a pure function of its inputs and can be unit-tested headless (no
// GPU). The host feeds this frame's dirty-rect union (DIPs), the client size, DPI
// scale, pixel size, a coverage threshold, and whether a full draw is forced; it
// returns whether a partial redraw is safe plus the region to redraw (DIPs) and the
// pixel-space update rect passed to the content surface's BeginDraw.
//
// History — the redraw region used to be widened by LAST frame's dirty rect. That
// existed solely because the content was a swap chain with
// DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL / BufferCount=2: the back buffer about to be
// drawn held the content from TWO frames ago, so whatever changed last frame was
// stale in it and had to be repainted as well. The content is now an
// IDCompositionVirtualSurface, which is PERSISTENT — pixels outside the update rect
// keep LAST frame's content, which is exactly what the partial path already relies
// on for the pixels it doesn't touch. With no second buffer there is nothing stale
// to compensate for, and the widening only cost area: measured headless at up to
// 256x the necessary region for two distant small rects, and a real pairing (caret
// blinking while the scrollbar fades on the far side) crossed the coverage gate and
// collapsed into a full-window repaint. Dropped for that reason.
#pragma once

#include "../fl_common.h"

namespace fluent {

struct RedrawPlan {
    bool partial = false;        // true → clip + partial present; false → full clear
    // Redraw region in DIPs (clamped to client). On a partial plan this is snapped to
    // whole pixels and is exactly dirtyPx converted back to DIPs — the host pushes it
    // as an antialiased D2D clip, which would otherwise blend the boundary pixel.
    RectDip redrawDip;
    RECT dirtyPx = {0, 0, 0, 0}; // pixel-space dirty rect for DXGI (valid iff partial)
};

// Pure planner. `forceFull` collapses all the host-side "must repaint everything"
// conditions (warm-up frames, Measure/Arrange dirty, caption chrome change). When
// `dirtyDip` is empty (nothing changed this frame) the plan is always full.
inline RedrawPlan PlanRedraw(const RectDip& dirtyDip,
                             float clientWDip, float clientHDip, float dpiScale,
                             unsigned pixelW, unsigned pixelH,
                             float maxCoverage, bool forceFull) {
    RedrawPlan plan;

    // This frame's dirty union is the whole story: the content surface is
    // persistent, so untouched pixels already hold last frame's image (see header).
    RectDip redraw = dirtyDip;

    // Clamp to the client area so a stray oversized bound can't blow up the rect.
    RectDip client{0.0f, 0.0f, clientWDip, clientHDip};
    float x1 = redraw.x < client.x ? client.x : redraw.x;
    float y1 = redraw.y < client.y ? client.y : redraw.y;
    float x2 = redraw.right() > client.right() ? client.right() : redraw.right();
    float y2 = redraw.bottom() > client.bottom() ? client.bottom() : redraw.bottom();
    redraw = {x1, y1, (x2 > x1 ? x2 - x1 : 0.0f), (y2 > y1 ? y2 - y1 : 0.0f)};
    plan.redrawDip = redraw;

    // Full redraw when forced, or when nothing changed this frame, or the region
    // clamped to empty.
    if (forceFull || dirtyDip.isEmpty() || redraw.isEmpty())
        return plan;

    // Coverage gate: above `maxCoverage` of the window, the flip cost dominates —
    // just clear + repaint everything.
    const float windowArea = clientWDip * clientHDip;
    const float dirtyArea = redraw.w * redraw.h;
    if (windowArea <= 0.0f || dirtyArea / windowArea >= maxCoverage)
        return plan;

    // Compute the pixel-space dirty rect (round out so no sub-pixel edge is lost).
    if (dpiScale <= 0.0f)
        return plan;  // nonsensical scale → full
    long left   = static_cast<long>(redraw.x * dpiScale);
    long top    = static_cast<long>(redraw.y * dpiScale);
    long right  = static_cast<long>(redraw.right() * dpiScale + 0.999f);
    long bottom = static_cast<long>(redraw.bottom() * dpiScale + 0.999f);
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<long>(pixelW)) right = static_cast<long>(pixelW);
    if (bottom > static_cast<long>(pixelH)) bottom = static_cast<long>(pixelH);
    if (right <= left || bottom <= top)
        return plan;  // degenerate → full

    // Snap the DIP region back out of that WHOLE-PIXEL rect, so the D2D clip the host
    // pushes is exactly the surface update rect — pixel-aligned, no fractional edge.
    //
    // This matters because the clip is ANTIALIASED (ClipGuard uses
    // D2D1_ANTIALIAS_MODE_PER_PRIMITIVE). A clip edge landing mid-pixel gives that
    // boundary pixel partial coverage, so the host's Clear only partly clears it and
    // the repaint only partly covers it — the remainder is a blend of the PREVIOUS
    // frame, i.e. a faint 1px seam tracing the dirty rect. Invisible while dirty rects
    // were integers (element bounds), it appeared the moment controls started
    // reporting bounds inflated by a fractional pad for their focus rings (3.75 /
    // 4.75 DIP). Snapping here fixes it for every producer at once, instead of
    // requiring each control to pick a whole-number pad.
    //
    // Rounding OUT (never in) keeps the region a superset of what was reported, so no
    // dirty pixel is left unpainted.
    plan.redrawDip = {static_cast<float>(left) / dpiScale,
                      static_cast<float>(top) / dpiScale,
                      static_cast<float>(right - left) / dpiScale,
                      static_cast<float>(bottom - top) / dpiScale};

    plan.partial = true;
    plan.dirtyPx = RECT{left, top, right, bottom};
    return plan;
}

} // namespace fluent
