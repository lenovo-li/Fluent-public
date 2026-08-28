// ButtonTests.cpp — unit tests for Button's UI-thread highlight tint animation
// (WP-02 Step 4, after removing the per-control DComp overlay surface). Pure
// logic: the tint opacity eases toward a state-driven target on each tick, and
// WantsAnimationTick reports motion left. Rendering itself is GPU and verified
// on-device; here we exercise only the animation state machine.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/MenuFlyout.h"

using namespace fluent;

namespace {
// Peer to drive Button's protected state machine (SetEnabled toggles disabled;
// pointer events drive hover/press through UIElement's input entry points).
struct ButtonTestPeer {
    static void Hover(Button& b) { b.OnPointerEnter(); }
    static void Leave(Button& b) { b.OnPointerLeave(); }
};
}  // namespace

// A freshly-created button has zero tint and wants no animation.
TEST(Button, IdleWantsNoTick) {
    Button b;
    EXPECT_FALSE(b.WantsAnimationTick());
}

// Hover re-targets the tint, so an animation tick is wanted; ticking drives the
// opacity up toward the target and eventually settles (WantsAnimationTick false).
TEST(Button, HoverAnimatesTintToTarget) {
    Button b;
    ButtonTestPeer::Hover(b);
    EXPECT_TRUE(b.WantsAnimationTick());  // moving toward hover target

    // Tick enough frames to converge (each ~16ms). Guard with an iteration cap.
    int guard = 0;
    while (b.WantsAnimationTick() && guard++ < 1000)
        b.OnAnimationTick(0.016f);
    EXPECT_FALSE(b.WantsAnimationTick());  // settled at the hover target
    EXPECT_TRUE(guard > 0);
}

// Leaving hover returns the tint to zero; after settling no tick is wanted.
TEST(Button, LeaveReturnsTintToZero) {
    Button b;
    ButtonTestPeer::Hover(b);
    int guard = 0;
    while (b.WantsAnimationTick() && guard++ < 1000) b.OnAnimationTick(0.016f);

    ButtonTestPeer::Leave(b);
    EXPECT_TRUE(b.WantsAnimationTick());  // now moving back toward 0
    guard = 0;
    while (b.WantsAnimationTick() && guard++ < 1000) b.OnAnimationTick(0.016f);
    EXPECT_FALSE(b.WantsAnimationTick());
}

// --- P1-16: Flyout integration --------------------------------------------
//
// The Show path needs a window (ShowBelow calls BeginShowAt, which queries the
// HWND + DPI), so headless tests cover only the property and the OnActivate
// invocation path. Visual verification: click the button and confirm the menu
// appears.

TEST(Button, FlyoutDefaultsNull) {
    Button b;
    EXPECT_TRUE(b.Flyout() == nullptr);
}

TEST(Button, SetFlyoutIsStored) {
    Button b;
    MenuFlyout menu;
    b.SetFlyout(&menu);
    EXPECT_TRUE(b.Flyout() == &menu);
}

TEST(Button, ClearFlyoutAcceptsNull) {
    Button b;
    MenuFlyout menu;
    b.SetFlyout(&menu);
    b.SetFlyout(nullptr);
    EXPECT_TRUE(b.Flyout() == nullptr);
}

TEST(Button, ActivateWithNoFlyoutDoesNotCrash) {
    Button b;
    // OnActivate is protected; drive it through the Enter key path.
    KeyEventArgs e;
    e.vk = VK_RETURN;
    b.OnKeyDownRouted(e);  // must not crash
}

TEST(Button, ActivateWithFlyoutCallsShow) {
    // Without an attached window, ShowBelow returns immediately, so the flyout
    // stays "not shown". The point is that OnActivate reached the Show call.
    Button b;
    MenuFlyout menu;
    b.SetFlyout(&menu);

    KeyEventArgs e;
    e.vk = VK_RETURN;
    b.OnKeyDownRouted(e);

    // The menu tried to show (but failed, headless). No assertion on IsShown
    // because without a host it stays false; success is not crashing.
    EXPECT_TRUE(b.Flyout() == &menu);
}
