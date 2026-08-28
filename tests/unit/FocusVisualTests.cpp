// FocusVisualTests.cpp — unit tests for the shared focus ring geometry (WP-05
// Stage 2, roadmap §11). Pure math: FocusRingRect is a plain function of a rect
// + spec, no DrawingContext / D2D device needed. (The actual stroke color comes
// from ColorTokens.focusStroke and is exercised by the color-token tests; here
// we only pin the geometry so a control's ring stays outside its content.)
//
// Coverage:
//   * the ring rect is the content inflated by `inset` on all four sides;
//   * the corner radius grows by the same inset (stays concentric);
//   * the historical Button spec (inset 2, corner 4) and CheckBox spec (inset 3)
//     reproduce the old hand-rolled rects.

#include "../framework/Test.h"
#include "../../FluentUI/styling/FocusVisual.h"

using namespace fluent;

TEST(FocusVisual, RingInflatesContentByInset) {
    RectDip content{10.0f, 20.0f, 100.0f, 40.0f};  // x,y,w,h
    FocusRingSpec spec;
    spec.inset = 2.0f;
    spec.cornerRadius = 4.0f;
    D2D1_ROUNDED_RECT rr = FocusRingRect(content, spec);
    EXPECT_NEAR(rr.rect.left, 8.0f, 0.001f);
    EXPECT_NEAR(rr.rect.top, 18.0f, 0.001f);
    EXPECT_NEAR(rr.rect.right, 112.0f, 0.001f);   // 10+100 + 2
    EXPECT_NEAR(rr.rect.bottom, 62.0f, 0.001f);   // 20+40 + 2
    EXPECT_NEAR(rr.radiusX, 6.0f, 0.001f);        // 4 + 2
    EXPECT_NEAR(rr.radiusY, 6.0f, 0.001f);
}

TEST(FocusVisual, ButtonHistoricalRingMatches) {
    // Old Button: rect inflated 2, corner kCorner(4)+2.
    RectDip b{0.0f, 0.0f, 80.0f, 30.0f};
    FocusRingSpec spec;  // defaults: inset 2, corner 4
    D2D1_ROUNDED_RECT rr = FocusRingRect(b, spec);
    EXPECT_NEAR(rr.rect.left, -2.0f, 0.001f);
    EXPECT_NEAR(rr.rect.top, -2.0f, 0.001f);
    EXPECT_NEAR(rr.rect.right, 82.0f, 0.001f);
    EXPECT_NEAR(rr.rect.bottom, 32.0f, 0.001f);
    EXPECT_NEAR(rr.radiusX, 6.0f, 0.001f);
}

// The pad a control must inflate its dirty rect by covers the whole ring: the inset,
// the outward half of the centered stroke, and a pixel of antialiasing.
TEST(FocusVisual, PadCoversRingOuterEdge) {
    FocusRingSpec spec;  // inset 2, stroke 1.5
    const float pad = FocusRingPadDip(spec);
    EXPECT_NEAR(pad, 3.75f, 0.001f);   // 2 + 0.75 + 1

    // The ring's outermost painted pixel must fall inside bounds inflated by the pad.
    RectDip b{50.0f, 60.0f, 80.0f, 30.0f};
    D2D1_ROUNDED_RECT rr = FocusRingRect(b, spec);
    const float outerLeft = rr.rect.left - spec.strokeWidth * 0.5f;
    const float outerRight = rr.rect.right + spec.strokeWidth * 0.5f;
    RectDip dirty = b.inflated(pad);
    EXPECT_TRUE(dirty.x < outerLeft);
    EXPECT_TRUE(dirty.right() > outerRight);
}

// A larger inset needs a proportionally larger pad (CheckBox's inset 3 vs Button's 2).
TEST(FocusVisual, PadTracksInset) {
    FocusRingSpec wide;
    wide.inset = 3.0f;
    EXPECT_NEAR(FocusRingPadDip(wide), 4.75f, 0.001f);
    EXPECT_TRUE(FocusRingPadDip(wide) > FocusRingPadDip(FocusRingSpec{}));
}

TEST(FocusVisual, CheckBoxHistoricalRingMatches) {
    // Old CheckBox: box inflated 3, corner kBoxCorner(4)+3.
    RectDip box{5.0f, 6.0f, 20.0f, 20.0f};
    FocusRingSpec spec;
    spec.inset = 3.0f;
    spec.cornerRadius = 4.0f;
    D2D1_ROUNDED_RECT rr = FocusRingRect(box, spec);
    EXPECT_NEAR(rr.rect.left, 2.0f, 0.001f);
    EXPECT_NEAR(rr.rect.top, 3.0f, 0.001f);
    EXPECT_NEAR(rr.rect.right, 28.0f, 0.001f);   // 5+20 + 3
    EXPECT_NEAR(rr.rect.bottom, 29.0f, 0.001f);  // 6+20 + 3
    EXPECT_NEAR(rr.radiusX, 7.0f, 0.001f);       // 4 + 3
}
