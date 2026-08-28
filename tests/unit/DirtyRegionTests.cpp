// DirtyRegionTests.cpp — unit tests for the pure partial-redraw planner
// (window/DirtyRegion.h, WP-07 §S4) and the RectDip geometry helpers it uses.

#include "../framework/Test.h"
#include "../../FluentUI/window/DirtyRegion.h"

using namespace fluent;

namespace {
// A 800x600 DIP window at DPI 1.0 (pixel size == DIP size), coverage gate 0.6.
RedrawPlan Plan(const RectDip& dirty, bool forceFull) {
    return PlanRedraw(dirty, 800.0f, 600.0f, 1.0f, 800, 600, 0.6f, forceFull);
}
} // namespace

// ---------------------------------------------------------------------------
// RectDip geometry
// ---------------------------------------------------------------------------

TEST(RectDip, IntersectsOverlapping) {
    RectDip a{0, 0, 100, 100};
    RectDip b{50, 50, 100, 100};
    EXPECT_TRUE(a.intersects(b));
    EXPECT_TRUE(b.intersects(a));
}

TEST(RectDip, IntersectsDisjoint) {
    RectDip a{0, 0, 100, 100};
    RectDip b{200, 200, 50, 50};
    EXPECT_FALSE(a.intersects(b));
}

TEST(RectDip, IntersectsEdgeTouchingIsFalse) {
    // Touching edges (a.right == b.x) do not overlap.
    RectDip a{0, 0, 100, 100};
    RectDip b{100, 0, 50, 100};
    EXPECT_FALSE(a.intersects(b));
}

TEST(RectDip, UnionOfTwoRects) {
    RectDip a{10, 20, 30, 40};   // [10,50) x [20,60)
    RectDip b{100, 0, 20, 10};   // [100,120) x [0,10)
    RectDip u = RectDip::Union(a, b);
    EXPECT_NEAR(u.x, 10.0f, 0.001f);
    EXPECT_NEAR(u.y, 0.0f, 0.001f);
    EXPECT_NEAR(u.right(), 120.0f, 0.001f);
    EXPECT_NEAR(u.bottom(), 60.0f, 0.001f);
}

TEST(RectDip, UnionWithEmptyIsOther) {
    RectDip empty{};
    RectDip a{5, 5, 10, 10};
    RectDip u = RectDip::Union(empty, a);
    EXPECT_NEAR(u.x, 5.0f, 0.001f);
    EXPECT_NEAR(u.w, 10.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// PlanRedraw
// ---------------------------------------------------------------------------

// A small dirty rect, no force → partial redraw with a valid pixel rect.
TEST(DirtyRegion, SmallDirtyIsPartial) {
    RedrawPlan p = Plan(RectDip{100, 100, 50, 30}, false);
    EXPECT_TRUE(p.partial);
    EXPECT_EQ(p.dirtyPx.left, 100);
    EXPECT_EQ(p.dirtyPx.top, 100);
    EXPECT_EQ(p.dirtyPx.right, 150);
    EXPECT_EQ(p.dirtyPx.bottom, 130);
}

// forceFull always yields a full frame regardless of dirty size.
TEST(DirtyRegion, ForceFullDefeatsPartial) {
    RedrawPlan p = Plan(RectDip{10, 10, 20, 20}, true);
    EXPECT_FALSE(p.partial);
}

// Empty dirty region (nothing changed) → full frame (defensive).
TEST(DirtyRegion, EmptyDirtyIsFull) {
    RedrawPlan p = Plan(RectDip{}, false);
    EXPECT_FALSE(p.partial);
}

// A dirty region above the coverage gate → full frame.
TEST(DirtyRegion, LargeDirtyExceedsCoverageGate) {
    // 800x600 window area = 480000; 0.6 gate = 288000.
    // A 700x500 = 350000 dirty rect exceeds it.
    RedrawPlan p = Plan(RectDip{0, 0, 700, 500}, false);
    EXPECT_FALSE(p.partial);
}

// A dirty rect partly outside the client is clamped to the client area.
TEST(DirtyRegion, ClampsToClient) {
    // Dirty rect extends beyond the 800x600 window; clamp to it.
    RedrawPlan p = Plan(RectDip{780, 580, 100, 100}, false);
    EXPECT_TRUE(p.partial);
    EXPECT_EQ(p.dirtyPx.right, 800);
    EXPECT_EQ(p.dirtyPx.bottom, 600);
}

// DPI scaling: a 50x30 DIP dirty rect at 2.0x → 100x60 pixel rect.
TEST(DirtyRegion, DpiScalesPixelRect) {
    // 1600x1200 px window at 2.0x = 800x600 DIP.
    RedrawPlan p = PlanRedraw(RectDip{100, 100, 50, 30},
                              800.0f, 600.0f, 2.0f, 1600, 1200, 0.6f, false);
    EXPECT_TRUE(p.partial);
    EXPECT_EQ(p.dirtyPx.left, 200);
    EXPECT_EQ(p.dirtyPx.top, 200);
    EXPECT_EQ(p.dirtyPx.right, 300);   // (100+50)*2
    EXPECT_EQ(p.dirtyPx.bottom, 260);  // (100+30)*2
}

// ---------------------------------------------------------------------------
// The redraw region is exactly THIS frame's dirty union
// ---------------------------------------------------------------------------
// The planner used to widen the region by LAST frame's dirty rect, a leftover from
// the swap-chain era (FLIP_SEQUENTIAL / BufferCount=2 meant the buffer about to be
// drawn held content from two frames ago, so last frame's changes were stale in it).
// The content is now a persistent IDCompositionVirtualSurface: pixels outside the
// update rect keep LAST frame's image, which is what the partial path already relies
// on. These cases are the shapes that made the old widening expensive; they now pin
// the absence of it, so a reintroduction shows up as a failure here rather than as
// silently larger repaints.

namespace {
// Redraw area (DIP^2) the planner would repaint for a given dirty union.
float RedrawArea(const RectDip& dirty) {
    RedrawPlan p = Plan(dirty, false);
    return p.redrawDip.w * p.redrawDip.h;
}
} // namespace

// Was the pathological case: a caret blinking at one end of the window on the frame
// after something 300 DIP away repainted cost the bounding box of both — 256x the
// area actually dirty. Only the caret's own 20x20 is repainted now.
TEST(DirtyRegionPlan, DistantPriorRectDoesNotEnlargeRedraw) {
    const RectDip caret{100, 100, 20, 20};
    EXPECT_NEAR(RedrawArea(caret), 400.0f, 0.5f);
    RedrawPlan p = Plan(caret, false);
    EXPECT_TRUE(p.partial);
    // Bounded by the caret itself, not by a union reaching out to [400,420).
    EXPECT_EQ(p.dirtyPx.right, 120);
    EXPECT_EQ(p.dirtyPx.bottom, 120);
}

// A caret blinking in a text box (lower left) while the scrollbar rail fades on the
// far side (upper right) is a real pairing in the demo. Unioned across frames the two
// crossed the 0.6 coverage gate and collapsed into a FULL window repaint; each is
// trivially partial on its own.
TEST(DirtyRegionPlan, TwoLiveRegionsEachStayPartial) {
    const RectDip caret{60, 500, 2, 18};
    const RectDip scrollbar{780, 40, 12, 400};
    EXPECT_TRUE(Plan(caret, false).partial);
    EXPECT_TRUE(Plan(scrollbar, false).partial);
    // Their bounding box is what the old widening produced: over the gate → full.
    EXPECT_FALSE(Plan(RectDip::Union(caret, scrollbar), false).partial);
}

// Adjacent rows (a control and its neighbour) cost their own area each, not the
// ~2.25x span of both — the modest case for the old widening, also gone.
TEST(DirtyRegionPlan, AdjacentRectsCostOnlyTheirOwnArea) {
    const RectDip a{100, 100, 120, 32};
    const RectDip b{100, 140, 120, 32};
    EXPECT_NEAR(RedrawArea(a), 120.0f * 32.0f, 0.5f);
    EXPECT_NEAR(RedrawArea(b), 120.0f * 32.0f, 0.5f);
    // Both dirty in the SAME frame still spans both rows — that union is real.
    EXPECT_NEAR(RedrawArea(RectDip::Union(a, b)), 120.0f * 72.0f, 0.5f);
}

// A control repainting in place (hover fade on one button) is unchanged: the same
// rect frame after frame always cost exactly its own area.
TEST(DirtyRegionPlan, RepeatingRectCostsItsOwnArea) {
    const RectDip r{100, 100, 120, 32};
    EXPECT_NEAR(RedrawArea(r), 120.0f * 32.0f, 0.5f);
}

// ---------------------------------------------------------------------------
// The partial redraw region is pixel-aligned
// ---------------------------------------------------------------------------
// The host pushes redrawDip as an ANTIALIASED D2D clip (ClipGuard uses
// D2D1_ANTIALIAS_MODE_PER_PRIMITIVE) and passes dirtyPx to the surface's BeginDraw.
// If the two disagree by a fraction of a pixel, the boundary pixel gets partial
// coverage: the Clear only partly clears it and the repaint only partly covers it, so
// what remains is blended with the previous frame — a faint 1px seam around the dirty
// rect, appearing and vanishing with whatever happens to be dirty. Controls report
// fractional bounds (a focus ring pad of 3.75 DIP), so the planner must snap.

// A fractional dirty rect yields a whole-pixel region, rounded OUT so nothing dirty
// is left outside it.
TEST(DirtyRegionPlan, FractionalDirtyRectSnapsOutToWholePixels) {
    // A button at 100,50 80x30 inflated by the 3.75 DIP focus-ring pad.
    RedrawPlan p = Plan(RectDip{96.25f, 46.25f, 87.5f, 37.5f}, false);
    EXPECT_TRUE(p.partial);
    // At DPI 1.0 the pixel rect is the DIP rect rounded out.
    EXPECT_EQ(p.dirtyPx.left, 96);
    EXPECT_EQ(p.dirtyPx.top, 46);
    EXPECT_EQ(p.dirtyPx.right, 184);    // 96.25+87.5 = 183.75 → 184
    EXPECT_EQ(p.dirtyPx.bottom, 84);    // 46.25+37.5 = 83.75 → 84
    // The DIP clip matches it exactly — integral, and a superset of what was asked.
    EXPECT_NEAR(p.redrawDip.x, 96.0f, 0.001f);
    EXPECT_NEAR(p.redrawDip.y, 46.0f, 0.001f);
    EXPECT_NEAR(p.redrawDip.right(), 184.0f, 0.001f);
    EXPECT_NEAR(p.redrawDip.bottom(), 84.0f, 0.001f);
}

// The invariant that actually prevents the seam, stated directly: redrawDip * dpi ==
// dirtyPx, at a DPI where DIP and pixel grids differ.
TEST(DirtyRegionPlan, ClipMatchesUpdateRectAtFractionalDpi) {
    const float dpi = 1.5f;  // 150% — a DIP is 1.5px, so integers do not coincide
    RedrawPlan p = PlanRedraw(RectDip{96.25f, 46.25f, 87.5f, 37.5f},
                              800.0f, 600.0f, dpi, 1200, 900, 0.6f, false);
    EXPECT_TRUE(p.partial);
    EXPECT_NEAR(p.redrawDip.x * dpi, static_cast<float>(p.dirtyPx.left), 0.01f);
    EXPECT_NEAR(p.redrawDip.y * dpi, static_cast<float>(p.dirtyPx.top), 0.01f);
    EXPECT_NEAR(p.redrawDip.right() * dpi, static_cast<float>(p.dirtyPx.right), 0.01f);
    EXPECT_NEAR(p.redrawDip.bottom() * dpi, static_cast<float>(p.dirtyPx.bottom), 0.01f);
}

// Snapping must never shrink the region: every pixel the caller reported dirty stays
// inside it, or that pixel would keep a stale image.
TEST(DirtyRegionPlan, SnappedRegionContainsTheRequestedRect) {
    const RectDip asked{96.25f, 46.25f, 87.5f, 37.5f};
    RedrawPlan p = Plan(asked, false);
    EXPECT_TRUE(p.partial);
    EXPECT_TRUE(p.redrawDip.x <= asked.x);
    EXPECT_TRUE(p.redrawDip.y <= asked.y);
    EXPECT_TRUE(p.redrawDip.right() >= asked.right());
    EXPECT_TRUE(p.redrawDip.bottom() >= asked.bottom());
}

// An already-integral rect is untouched (the common case must not grow).
TEST(DirtyRegionPlan, IntegralRectIsUnchangedBySnapping) {
    RedrawPlan p = Plan(RectDip{100, 100, 50, 30}, false);
    EXPECT_TRUE(p.partial);
    EXPECT_NEAR(p.redrawDip.x, 100.0f, 0.001f);
    EXPECT_NEAR(p.redrawDip.w, 50.0f, 0.001f);
    EXPECT_NEAR(p.redrawDip.h, 30.0f, 0.001f);
}
