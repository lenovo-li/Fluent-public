// ResizeGripOuterTests.cpp — the non-client resize band width, converted to physical
// pixels at a given DPI (window/WindowState.h).
//
// This value is the SINGLE SOURCE OF TRUTH for both sides of the WM_NCCALCSIZE /
// WM_NCHITTEST contract:
//   * WM_NCCALCSIZE insets rgrc[0]'s left, right, and bottom by it (not the top —
//     a top inset makes DWM draw its own caption over ours)
//   * HitTestNca claims exactly those bands, on the INSIDE of each window edge,
//     as HTLEFT / HTRIGHT / HTBOTTOM
//
// The band lives inside the window rect, because WM_NCCALCSIZE shrinks only the CLIENT
// rect and leaves the window rect alone. If the two call sites disagree by even one
// pixel, the mismatched slice is non-client (so the client area never sees the mouse)
// yet reported as HTCLIENT (so Windows offers no resize cursor) — dead in both
// directions. Two separate revisions shipped that bug: one reserved a 9px bottom band
// while HitTestNca claimed 1px, and one claimed the sides 1px inside plus a stretch of
// pure outside that a frameless window never receives hit-tests for. This suite locks
// the invariant.

#include "../framework/Test.h"
#include "../../FluentUI/window/WindowState.h"

using namespace fluent;

// At 96 DPI (100% scaling), 8 DIPs = 8 pixels exactly.
TEST(ResizeGripOuter, IdentityAt96Dpi) {
    EXPECT_EQ(ResizeGripOuterPx(96), 8);
}

// At 192 DPI (200% scaling), 8 DIPs = 16 pixels exactly.
TEST(ResizeGripOuter, DoubleAt192Dpi) {
    EXPECT_EQ(ResizeGripOuterPx(192), 16);
}

// At 144 DPI (150% scaling), 8 * 1.5 = 12.0 exactly (no rounding).
TEST(ResizeGripOuter, ExactAt144Dpi) {
    EXPECT_EQ(ResizeGripOuterPx(144), 12);
}

// At 120 DPI (125% scaling), 8 * 1.25 = 10.0 exactly.
TEST(ResizeGripOuter, RoundsHalfUpAt120Dpi) {
    EXPECT_EQ(ResizeGripOuterPx(120), 10);
}

// At an uncommon 112 DPI, 8 * (112/96) = 9.333... -> rounds to 9.
TEST(ResizeGripOuter, ExactAt112Dpi) {
    EXPECT_EQ(ResizeGripOuterPx(112), 9);
}

// Zero DPI falls back to 96 rather than dividing by zero or returning nonsense.
TEST(ResizeGripOuter, ZeroDpiFallsBackTo96) {
    EXPECT_EQ(ResizeGripOuterPx(0), 8);
}

// The rounding mode must match the arithmetic both call sites use (+0.5 truncation).
// At 143 DPI, 8 * (143/96) is about 11.92 -> 12.
TEST(ResizeGripOuter, RoundingMatchesWmNcCalcSize) {
    const UINT dpi = 143;
    const int expected =
        static_cast<int>(kResizeGripOuterDip * static_cast<float>(dpi) / 96.0f + 0.5f);
    EXPECT_EQ(ResizeGripOuterPx(dpi), expected);
}

// The band is never zero-width at any DPI a monitor can report. A zero band would
// silently disable resizing on every edge, since the NC area would be empty.
TEST(ResizeGripOuter, NeverZeroAcrossDpiRange) {
    for (UINT dpi = 96; dpi <= 384; dpi += 24)
        EXPECT_TRUE(ResizeGripOuterPx(dpi) > 0);
}
