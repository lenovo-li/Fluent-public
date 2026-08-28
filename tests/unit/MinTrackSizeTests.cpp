// MinTrackSizeTests.cpp — the DIP→pixel minimum-size conversion fed to
// WM_GETMINMAXINFO (window/WindowState.h).
//
// Without a minimum the system floor (~SM_CXMIN, about 136px) applies and a fixed
// layout collapses. The limit is stored in DIPs and converted at the window's CURRENT
// DPI on every message, so it means the same amount of layout room on a 200% monitor
// as on a 100% one. Pure arithmetic, so it is testable without a window.
//
// The caller's limit describes CLIENT area (that is what layout measures), but
// ptMinTrackSize bounds the WINDOW rect, so the reserved non-client bands must be added
// back. The bands are ASYMMETRIC: WM_NCCALCSIZE insets left+right (so the width gains
// two bands) and bottom only (so the height gains one). The top edge keeps no band —
// a top inset makes DWM draw its own caption over ours, the double-title-bar artifact.

#include "../framework/Test.h"
#include "../../FluentUI/window/WindowState.h"

using namespace fluent;

// At 96 DPI, DIPs and pixels coincide. Width gains two bands, height gains one.
TEST(MinTrackSize, IdentityAt96Dpi) {
    POINT p = {0, 0};
    EXPECT_TRUE(MinTrackSizePx(420.0f, 320.0f, 96, p));
    EXPECT_EQ(p.x, 420 + 16);  // 2 x 8px (kResizeGripOuterDip = 8), left + right
    EXPECT_EQ(p.y, 320 + 8);   // 1 x 8px, bottom only — top has no band
}

// At 200% the same DIP limit must ask for twice the pixels — otherwise the window
// could be dragged to half the intended layout room on a high-DPI monitor. The band
// scales with it (8 DIP -> 16px per edge).
TEST(MinTrackSize, ScalesWithDpi) {
    POINT p = {0, 0};
    EXPECT_TRUE(MinTrackSizePx(420.0f, 320.0f, 192, p));
    EXPECT_EQ(p.x, 840 + 32);
    EXPECT_EQ(p.y, 640 + 16);
}

// 150% lands on fractional pixels; round UP so the limit is never a pixel short.
// The band at 144 DPI is 12px per edge (8 * 1.5).
TEST(MinTrackSize, RoundsUpOnFractionalDpi) {
    POINT p = {0, 0};
    EXPECT_TRUE(MinTrackSizePx(101.0f, 101.0f, 144, p));   // 101 * 1.5 = 151.5
    EXPECT_EQ(p.x, 152 + 24);   // width: 152 + 2×12 (left+right)
    EXPECT_EQ(p.y, 152 + 12);   // height: 152 + 12 (bottom only)
}

// A whole-number result must not be inflated by the rounding.
TEST(MinTrackSize, ExactValueIsNotInflated) {
    POINT p = {0, 0};
    EXPECT_TRUE(MinTrackSizePx(100.0f, 100.0f, 144, p));   // 150.0 exactly
    EXPECT_EQ(p.x, 150 + 24);
    EXPECT_EQ(p.y, 150 + 12);
}

// Zero means "no limit": the caller's value is LEFT ALONE, so the system's own
// MINMAXINFO figure survives instead of being overwritten with 0. In particular the
// non-client band must NOT be added to an axis that has no limit.
TEST(MinTrackSize, ZeroLimitLeavesValueUntouched) {
    POINT p = {777, 888};
    EXPECT_FALSE(MinTrackSizePx(0.0f, 0.0f, 96, p));
    EXPECT_EQ(p.x, 777);
    EXPECT_EQ(p.y, 888);
}

// One axis limited, the other not: only the limited axis is written (band included).
TEST(MinTrackSize, WidthOnlyLeavesHeightUntouched) {
    POINT p = {777, 888};
    EXPECT_TRUE(MinTrackSizePx(420.0f, 0.0f, 96, p));
    EXPECT_EQ(p.x, 420 + 16);
    EXPECT_EQ(p.y, 888);
}

TEST(MinTrackSize, HeightOnlyLeavesWidthUntouched) {
    POINT p = {777, 888};
    EXPECT_TRUE(MinTrackSizePx(0.0f, 320.0f, 96, p));
    EXPECT_EQ(p.x, 777);
    EXPECT_EQ(p.y, 320 + 8);    // height gains bottom band only
}

// A zero DPI (never expected, but the field is a plain UINT) falls back to 96 rather
// than collapsing the limit to nothing.
TEST(MinTrackSize, ZeroDpiFallsBackTo96) {
    POINT p = {0, 0};
    EXPECT_TRUE(MinTrackSizePx(420.0f, 320.0f, 0, p));
    EXPECT_EQ(p.x, 420 + 16);
    EXPECT_EQ(p.y, 320 + 8);
}

// Negative input is treated as no limit, not as a negative floor.
TEST(MinTrackSize, NegativeLimitIsIgnored) {
    POINT p = {777, 888};
    EXPECT_FALSE(MinTrackSizePx(-10.0f, -10.0f, 96, p));
    EXPECT_EQ(p.x, 777);
    EXPECT_EQ(p.y, 888);
}

// The width must gain exactly two bands (left + right) and the height exactly one
// (bottom; the top edge has no band). Stated against the helper rather than literals so
// the two cannot drift, and asserted across DPIs so the asymmetry is not an artifact of
// one scale factor. This is the test that fails if someone re-adds the top inset.
TEST(MinTrackSize, AddsTwoBandsToWidthAndOneToHeight) {
    for (UINT dpi : {96u, 120u, 144u, 192u}) {
        POINT p = {0, 0};
        EXPECT_TRUE(MinTrackSizePx(400.0f, 300.0f, dpi, p));
        const float s = static_cast<float>(dpi) / 96.0f;
        const LONG clientW = static_cast<LONG>(400.0f * s + 0.999f);
        const LONG clientH = static_cast<LONG>(300.0f * s + 0.999f);
        EXPECT_EQ(p.x - clientW, 2 * ResizeGripOuterPx(dpi));
        EXPECT_EQ(p.y - clientH, ResizeGripOuterPx(dpi));
    }
}
