// PopupGeometryTests.cpp — unit tests for the pure popup/scroll helpers extracted
// in WP-06 Stage 4 (AnchorScreenRect + EnsureVisibleOffset).

#include "../framework/Test.h"
#include "../../FluentUI/services/PopupGeometry.h"
#include "../../FluentUI/core/ScrollMath.h"

using namespace fluent;

// ---------------------------------------------------------------------------
// AnchorScreenRect
// ---------------------------------------------------------------------------

// At DPI 1.0 the sub-rect simply offsets by the window origin.
TEST(PopupGeometry, AnchorNoScaling) {
    RECT r = AnchorScreenRect(100, 200, 10.0f, 20.0f, 50.0f, 30.0f, 1.0f);
    EXPECT_EQ(r.left, 110);
    EXPECT_EQ(r.top, 220);
    EXPECT_EQ(r.right, 160);
    EXPECT_EQ(r.bottom, 250);
}

// At DPI 1.5 the DIP extents scale by 1.5 (with the historical +0.5 rounding).
TEST(PopupGeometry, AnchorScaled150) {
    RECT r = AnchorScreenRect(0, 0, 10.0f, 20.0f, 50.0f, 30.0f, 1.5f);
    EXPECT_EQ(r.left, 15);   // 10*1.5
    EXPECT_EQ(r.top, 30);    // 20*1.5
    EXPECT_EQ(r.right, 90);  // 15 + 50*1.5(=75)
    EXPECT_EQ(r.bottom, 75); // 30 + 30*1.5(=45)
}

// Window origin is added in physical pixels after scaling the DIP offsets.
TEST(PopupGeometry, AnchorWithOriginAndScale) {
    RECT r = AnchorScreenRect(1000, 500, 8.0f, 4.0f, 40.0f, 32.0f, 2.0f);
    EXPECT_EQ(r.left, 1016);  // 1000 + 8*2
    EXPECT_EQ(r.top, 508);    // 500 + 4*2
    EXPECT_EQ(r.right, 1096); // 1016 + 40*2
    EXPECT_EQ(r.bottom, 572); // 508 + 32*2
}

// ---------------------------------------------------------------------------
// EnsureVisibleOffset
// ---------------------------------------------------------------------------

// Item already fully inside the viewport: offset unchanged.
TEST(ScrollMath, AlreadyVisibleUnchanged) {
    // viewport [0,100), item [40,72) — fully inside.
    float off = EnsureVisibleOffset(40.0f, 32.0f, 0.0f, 100.0f);
    EXPECT_NEAR(off, 0.0f, 0.001f);
}

// Item above the viewport: scroll up to its top.
TEST(ScrollMath, ItemAboveScrollsToTop) {
    // viewport top = 200, item at 100 — above.
    float off = EnsureVisibleOffset(100.0f, 32.0f, 200.0f, 100.0f);
    EXPECT_NEAR(off, 100.0f, 0.001f);
}

// Item below the viewport: scroll so its bottom aligns with the viewport bottom.
TEST(ScrollMath, ItemBelowScrollsToBottom) {
    // viewport [0,100), item [120,152) — below. New offset = 152 - 100 = 52.
    float off = EnsureVisibleOffset(120.0f, 32.0f, 0.0f, 100.0f);
    EXPECT_NEAR(off, 52.0f, 0.001f);
}

// Item exactly at the top edge stays put.
TEST(ScrollMath, ItemAtTopEdge) {
    float off = EnsureVisibleOffset(50.0f, 32.0f, 50.0f, 100.0f);
    EXPECT_NEAR(off, 50.0f, 0.001f);
}
