// FocusRingPixelTests.cpp — the focus ring, asserted on PIXELS rather than geometry.
//
// WHY A SECOND FOCUS-RING TEST FILE. DirtyBoundsOverflowTests already checks that each
// control's VisualOverflowDip() covers the rect its ring paints. That is a necessary
// condition and it is not sufficient: it only proves the control ASKS for enough
// redraw area. It cannot see an ancestor's ClipGuard throwing the ring away on the way
// to the surface, because no surface is involved. The user's screenshots show exactly
// that failure — rings missing one to three edges inside Expander content, inside a
// ScrollPanel, and on a Button — with the overflow declarations already correct.
//
// So these tests render into a real offscreen D2D target (tests/framework/PixelSurface.h)
// and count ring edges in the pixels. An edge that a clip ate is absent here and
// present in the geometry test; that difference is the whole point.
//
// WHAT A "RING EDGE PRESENT" ASSERTION MEANS. The ring is a 1.5 DIP centered stroke in
// colors.focusStroke, antialiased, drawn on a background of a known fill. So the test
// does not look for an exact color: it scans the band where the edge must lie and asks
// whether ANY pixel there moved measurably toward the ring color and away from the
// background. That is robust to antialiasing and to the exact subpixel position, and it
// still fails hard when the edge is simply not drawn — which is the bug.

#include "../framework/Test.h"
#include "../framework/PixelSurface.h"
#include "../../FluentUI/styling/FocusVisual.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <algorithm>
#include <cmath>

using namespace fluent;
using fltest::Pixel;
using fltest::PixelSurface;

namespace {

constexpr Pixel kBg{255, 255, 255, 255};   // opaque white page

// The same static default snapshot UIElement::Theme() falls back to when an element
// is unattached (UIElement.cpp:17 builds it exactly this way). Going through
// BuildSnapshot rather than hard-coding a color means a token change cannot make
// this test assert against a color no control would ever draw.
const ThemeSnapshot& DefaultTheme() {
    static const ThemeSnapshot kDefault = BuildSnapshot(ThemeInputs{}, 0);
    return kDefault;
}

// The ring color the theme actually uses, as a Pixel.
Pixel RingPixel() {
    const ColorTokens& pal = DefaultTheme().colors;
    const D2D1_COLOR_F c = pal.focusStroke;
    Pixel p;
    p.r = static_cast<uint8_t>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
    p.g = static_cast<uint8_t>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
    p.b = static_cast<uint8_t>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
    p.a = static_cast<uint8_t>(std::lround(std::clamp(c.a, 0.0f, 1.0f) * 255.0f));
    return p;
}

// How far a pixel has moved from the background toward the ring color, 0..1-ish.
// Antialiased coverage shows up as a partial value; a missing edge stays at 0.
float Ringness(const Pixel& px, const Pixel& ring) {
    const int dr = std::abs(int(px.r) - int(kBg.r));
    const int dg = std::abs(int(px.g) - int(kBg.g));
    const int db = std::abs(int(px.b) - int(kBg.b));
    const int moved = std::max({dr, dg, db});

    const int fr = std::abs(int(ring.r) - int(kBg.r));
    const int fg = std::abs(int(ring.g) - int(kBg.g));
    const int fb = std::abs(int(ring.b) - int(kBg.b));
    const int full = std::max({fr, fg, fb});
    if (full == 0) return 0.0f;
    return static_cast<float>(moved) / static_cast<float>(full);
}

// Is there ring ink anywhere along the horizontal span [x0,x1] at row y (+/- 1 row,
// to absorb subpixel placement)?
bool RowHasRing(const PixelSurface& s, int y, int x0, int x1, const Pixel& ring) {
    for (int yy = y - 1; yy <= y + 1; ++yy)
        for (int x = x0; x <= x1; ++x)
            if (Ringness(s.At(x, yy), ring) > 0.25f) return true;
    return false;
}

// Same, for a vertical span at column x.
bool ColHasRing(const PixelSurface& s, int x, int y0, int y1, const Pixel& ring) {
    for (int xx = x - 1; xx <= x + 1; ++xx)
        for (int y = y0; y <= y1; ++y)
            if (Ringness(s.At(xx, y), ring) > 0.25f) return true;
    return false;
}

// The four edges of the ring around `content`, each reported present/absent. The
// probe lines sit at the ring's nominal stroke centerline: content inflated by the
// spec's inset. Corners are excluded from each span (the rounded corner belongs to
// two edges and would let one edge's ink vouch for its neighbour).
struct RingEdges {
    bool top = false, bottom = false, left = false, right = false;
    int Count() const { return int(top) + int(bottom) + int(left) + int(right); }
};

RingEdges ProbeRing(const PixelSurface& s, const RectDip& content,
                    const FocusRingSpec& spec) {
    const Pixel ring = RingPixel();
    const float inset = spec.inset;
    const int top    = static_cast<int>(std::lround(content.y - inset));
    const int bottom = static_cast<int>(std::lround(content.bottom() + inset));
    const int left   = static_cast<int>(std::lround(content.x - inset));
    const int right  = static_cast<int>(std::lround(content.right() + inset));

    // Stay clear of the rounded corners: the ring's corner radius is
    // cornerRadius + inset, so skip that much from each end.
    const int skip = static_cast<int>(std::lround(spec.cornerRadius + inset)) + 1;

    RingEdges e;
    e.top    = RowHasRing(s, top,    left + skip, right - skip, ring);
    e.bottom = RowHasRing(s, bottom, left + skip, right - skip, ring);
    e.left   = ColHasRing(s, left,   top + skip,  bottom - skip, ring);
    e.right  = ColHasRing(s, right,  top + skip,  bottom - skip, ring);
    return e;
}

} // namespace

// --- Harness self-check -----------------------------------------------------
// Before trusting any negative result from this file, prove the harness can see a
// ring that IS fully drawn. Without this, a broken probe would report "all four
// edges missing" for every control and look like a catastrophic product bug.
TEST(FocusRingPixel, HarnessSeesAllFourEdgesOfAnUnclippedRing) {
    PixelSurface s(200, 120);
    if (fltest::SkipIfNoDevice(s, "FocusRingPixel.HarnessSeesAllFourEdgesOfAnUnclippedRing"))
        return;

    const RectDip content{40.0f, 30.0f, 120.0f, 50.0f};
    const FocusRingSpec spec{};   // inset 2, corner 4, stroke 1.5 — the Button ring

    s.Paint(kBg, [&](const DrawingContext& dc) {
        DrawFocusRing(dc, content, DefaultTheme().colors, spec);
    });
    EXPECT_TRUE(s.ReadBack());

    const RingEdges e = ProbeRing(s, content, spec);
    EXPECT_TRUE(e.top);
    EXPECT_TRUE(e.bottom);
    EXPECT_TRUE(e.left);
    EXPECT_TRUE(e.right);
    EXPECT_EQ(e.Count(), 4);
}

// The probe must also be able to report ABSENCE. If it cannot distinguish a blank
// surface from a drawn ring, every other assertion in this file is vacuous.
TEST(FocusRingPixel, HarnessReportsNoEdgesWhenNothingIsDrawn) {
    PixelSurface s(200, 120);
    if (fltest::SkipIfNoDevice(s, "FocusRingPixel.HarnessReportsNoEdgesWhenNothingIsDrawn"))
        return;

    const RectDip content{40.0f, 30.0f, 120.0f, 50.0f};
    s.Paint(kBg, [](const DrawingContext&) { /* paint nothing */ });
    EXPECT_TRUE(s.ReadBack());

    EXPECT_EQ(ProbeRing(s, content, FocusRingSpec{}).Count(), 0);
}

// --- The actual failure the screenshots show --------------------------------
// A ClipGuard on the CONTENT rect eats the ring on all four sides, because the ring
// is drawn entirely outside that rect. This is the mechanism behind the Expander
// nested-content and GroupBox screenshots: the ring is emitted while an ancestor's
// content clip is still on the device context.
TEST(FocusRingPixel, ContentClipRemovesEveryRingEdge) {
    PixelSurface s(200, 120);
    if (fltest::SkipIfNoDevice(s, "FocusRingPixel.ContentClipRemovesEveryRingEdge"))
        return;

    const RectDip content{40.0f, 30.0f, 120.0f, 50.0f};
    const FocusRingSpec spec{};

    s.Paint(kBg, [&](const DrawingContext& dc) {
        ClipGuard clip = dc.PushClip(D2D1::RectF(content.x, content.y,
                                                 content.right(), content.bottom()));
        DrawFocusRing(dc, content, DefaultTheme().colors, spec);
    });
    EXPECT_TRUE(s.ReadBack());

    // Every edge is outside the clip, so none survive. This is the assertion that
    // makes the ordering constraint in FocusVisual.h ("draw the ring BEFORE pushing
    // any content clip") a tested contract rather than a comment.
    EXPECT_EQ(ProbeRing(s, content, spec).Count(), 0);
}

// A clip that cuts only the BOTTOM — the shape of "last row inside a scrolling or
// GroupBox content area" — removes exactly the bottom edge and leaves the other
// three. This is the single-missing-edge signature in the Button screenshot.
TEST(FocusRingPixel, ClipAtContentBottomRemovesOnlyTheBottomEdge) {
    PixelSurface s(200, 140);
    if (fltest::SkipIfNoDevice(s, "FocusRingPixel.ClipAtContentBottomRemovesOnlyTheBottomEdge"))
        return;

    const RectDip content{40.0f, 30.0f, 120.0f, 50.0f};
    const FocusRingSpec spec{};

    s.Paint(kBg, [&](const DrawingContext& dc) {
        // Clip bottom exactly at the content's bottom edge: the ring's lower edge
        // sits `inset + stroke/2` below that and is cut off.
        ClipGuard clip = dc.PushClip(D2D1::RectF(0.0f, 0.0f, 200.0f, content.bottom()));
        DrawFocusRing(dc, content, DefaultTheme().colors, spec);
    });
    EXPECT_TRUE(s.ReadBack());

    const RingEdges e = ProbeRing(s, content, spec);
    EXPECT_TRUE(e.top);
    EXPECT_TRUE(e.left);
    EXPECT_TRUE(e.right);
    EXPECT_FALSE(e.bottom);
}

// The converse, and the reason a partial-redraw bug and a clip bug must not be
// conflated: a DrawingContext clipHint does NOT clip drawing. clipHint only culls
// Panel::Render's traversal. A control drawn through a context whose clipHint
// excludes it still puts ink on the surface — so a ring that is missing on screen
// with a correct overflow declaration is a ClipGuard problem, not a clipHint one.
TEST(FocusRingPixel, ClipHintDoesNotClipRingPixels) {
    PixelSurface s(200, 120);
    if (fltest::SkipIfNoDevice(s, "FocusRingPixel.ClipHintDoesNotClipRingPixels"))
        return;

    const RectDip content{40.0f, 30.0f, 120.0f, 50.0f};
    const FocusRingSpec spec{};
    // A hint far away from the ring.
    const RectDip hint{0.0f, 0.0f, 5.0f, 5.0f};

    s.Paint(kBg, [&](const DrawingContext& dc) {
        DrawFocusRing(dc, content, DefaultTheme().colors, spec);
    }, &hint);
    EXPECT_TRUE(s.ReadBack());

    EXPECT_EQ(ProbeRing(s, content, spec).Count(), 4);
}

// --- Partial redraw: the residue case ---------------------------------------
// The redraw region is this frame's dirty union. When a control's declared overflow
// is too small, the clear does not cover the ring's outer edge, and the old ring
// pixels survive the repaint as residue. Reproduced here by clearing only the
// under-declared rect over a surface that already holds a ring.
TEST(FocusRingPixel, UnderDeclaredOverflowLeavesRingResidue) {
    PixelSurface s(200, 140);
    if (fltest::SkipIfNoDevice(s, "FocusRingPixel.UnderDeclaredOverflowLeavesRingResidue"))
        return;

    const RectDip content{40.0f, 30.0f, 120.0f, 50.0f};
    const FocusRingSpec spec{};
    const Pixel ring = RingPixel();

    // Frame 1: focused, ring drawn.
    s.Paint(kBg, [&](const DrawingContext& dc) {
        DrawFocusRing(dc, content, DefaultTheme().colors, spec);
    });
    EXPECT_TRUE(s.ReadBack());
    EXPECT_EQ(ProbeRing(s, content, spec).Count(), 4);

    // Frame 2: focus left. The control repaints, and the host clears only the
    // region the control declared. A control declaring 0.0f overflow clears just
    // bounds_ — so simulate that by filling the content rect with the page color
    // and nothing more. The ring's ink lies outside it and stays.
    ID2D1DeviceContext* raw = nullptr;   // not needed; use the typed path below
    (void)raw;
    s.Paint(kBg, [&](const DrawingContext& dc) {
        // Draw a "previous frame" ring, then clear only bounds_ — the exact
        // sequence an under-declared overflow produces.
        DrawFocusRing(dc, content, DefaultTheme().colors, spec);
        dc.FillRect(D2D1::RectF(content.x, content.y, content.right(),
                                     content.bottom()),
                         D2D1::ColorF(kBg.r / 255.0f, kBg.g / 255.0f,
                                      kBg.b / 255.0f, 1.0f));
    });
    EXPECT_TRUE(s.ReadBack());

    // All four edges are still on screen: clearing bounds_ cleared none of them,
    // because every one of them is outside bounds_. This is the residue the
    // Hyperlink / Expander overflow fixes exist to prevent, now visible as pixels.
    EXPECT_EQ(ProbeRing(s, content, spec).Count(), 4);
    // And a pixel on the ring line really is ring-colored, not a stray artifact.
    const int ringTop = static_cast<int>(std::lround(content.y - spec.inset));
    const int midX = static_cast<int>(std::lround(content.x + content.w * 0.5f));
    EXPECT_TRUE(Ringness(s.At(midX, ringTop), ring) > 0.25f);
}
