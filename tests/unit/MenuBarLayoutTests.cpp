// MenuBarLayoutTests.cpp — MenuBar title clamping when the bar is too narrow
// (WP-07 §S4 follow-up).
//
// MenuBar::Measure takes min(availW, sum-of-title-widths), so a window too narrow for
// every title leaves the bar shorter than its content — while Render laid titles out
// from bounds_.x at their full widths, unaware of the right edge. The trailing
// highlight then painted OUTSIDE bounds_, and since the dirty rect is bounds_ those
// pixels were never cleared: the highlight survived as residue after the pointer left.
//
// VisibleTitleRect is the pure clamp that fixes it, shared by Render (what to paint),
// ItemAt (what is clickable) and ItemScreenRect (where the dropdown anchors) so the
// three cannot disagree about which titles exist.

#include "../framework/Test.h"
#include "../../FluentUI/controls/MenuBar.h"

using namespace fluent;

namespace {
// A 150 DIP wide bar at x=24, the demo's geometry.
const RectDip kBar{24.0f, 88.0f, 150.0f, 28.0f};   // right edge = 174
} // namespace

// A title well inside the bar is returned unchanged.
TEST(MenuBarLayout, TitleFullyInsideIsUnchanged) {
    RectDip r = MenuBar::VisibleTitleRect(24.0f, 50.0f, kBar);
    EXPECT_TRUE(!r.isEmpty());
    EXPECT_NEAR(r.x, 24.0f, 0.001f);
    EXPECT_NEAR(r.w, 50.0f, 0.001f);
    // Vertical extent always comes from the bar.
    EXPECT_NEAR(r.y, 88.0f, 0.001f);
    EXPECT_NEAR(r.h, 28.0f, 0.001f);
}

// A title straddling the right edge is cut at it — this is the case that used to
// paint outside bounds_ and leave residue.
TEST(MenuBarLayout, TitleStraddlingRightEdgeIsClamped) {
    // Starts at 150, is 50 wide → would reach 200, but the bar ends at 174.
    RectDip r = MenuBar::VisibleTitleRect(150.0f, 50.0f, kBar);
    EXPECT_TRUE(!r.isEmpty());
    EXPECT_NEAR(r.x, 150.0f, 0.001f);
    EXPECT_NEAR(r.right(), 174.0f, 0.001f);   // clamped to the bar, not 200
    EXPECT_NEAR(r.w, 24.0f, 0.001f);
}

// A title starting beyond the right edge is entirely gone (Render breaks here).
// The rect must be CLEANLY empty, not a negative-width one: isEmpty() would accept
// either, but callers read .w — ItemScreenRect feeds it to the dropdown anchor.
TEST(MenuBarLayout, TitleStartingPastRightEdgeIsEmpty) {
    RectDip r = MenuBar::VisibleTitleRect(180.0f, 50.0f, kBar);
    EXPECT_TRUE(r.isEmpty());
    EXPECT_NEAR(r.w, 0.0f, 0.001f);   // not -6
    EXPECT_NEAR(r.h, 0.0f, 0.001f);
}

// Starting exactly ON the right edge is also empty (a zero-width slice is nothing).
TEST(MenuBarLayout, TitleStartingExactlyAtRightEdgeIsEmpty) {
    RectDip r = MenuBar::VisibleTitleRect(174.0f, 50.0f, kBar);
    EXPECT_TRUE(r.isEmpty());
    EXPECT_NEAR(r.w, 0.0f, 0.001f);
}

// A degenerate title width yields nothing rather than a negative rect.
TEST(MenuBarLayout, ZeroWidthTitleIsEmpty) {
    EXPECT_TRUE(MenuBar::VisibleTitleRect(24.0f, 0.0f, kBar).isEmpty());
}

// The clamp never reaches outside the bar, for any start position — the invariant
// that actually prevents the residue.
TEST(MenuBarLayout, ClampedRectNeverExceedsTheBar) {
    for (float x = 24.0f; x < 220.0f; x += 7.0f) {
        RectDip r = MenuBar::VisibleTitleRect(x, 50.0f, kBar);
        if (r.isEmpty()) continue;
        EXPECT_TRUE(r.right() <= kBar.right() + 0.001f);
        EXPECT_TRUE(r.x >= kBar.x - 0.001f);
    }
}

// A bar wide enough for its content changes nothing: the common case must not be
// affected by the clamp.
TEST(MenuBarLayout, WideBarLeavesEveryTitleIntact) {
    const RectDip wide{24.0f, 88.0f, 800.0f, 28.0f};
    float x = wide.x;
    for (int i = 0; i < 3; ++i) {
        RectDip r = MenuBar::VisibleTitleRect(x, 50.0f, wide);
        EXPECT_TRUE(!r.isEmpty());
        EXPECT_NEAR(r.w, 50.0f, 0.001f);   // untouched
        x += 50.0f;
    }
}
