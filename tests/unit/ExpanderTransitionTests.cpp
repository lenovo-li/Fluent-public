// ExpanderTransitionTests.cpp — the caller chooses whether a reveal animates.
//
// BACKGROUND. Expander shipped with a full reveal ease (reveal_, kRevealTau,
// OnAnimationTick, WantsAnimationTick) that never ran: SetExpanded called
// reveal_.SetImmediate(...), which JUMPS the value to its target, so
// WantsAnimationTick() was already false on the line after any state change. The
// animation was unreachable dead code, and a pixel test that tried to observe a
// mid-reveal frame silently skipped every run instead of failing.
//
// Rather than switch every existing caller to an animation they never asked for, the
// transition is now explicit:
//
//   SetExpanded(bool, Transition)          — per call; defaults to Instant
//   SetUserToggleTransition(Transition)    — what a header click/Space/Enter does
//
// Instant is the default on both, so this change is behaviour-preserving for every
// existing call site; opting into motion is a deliberate act.
//
// WHAT THESE TESTS PIN. That Instant really does snap (nothing left to tick), that
// Animate really does leave work for the frame loop and converges, that the two are
// wired to the right entry points, and that "collapsed means detached" — a
// load-bearing invariant this control already had — survives the animated path.
//
// All headless: the ease is driven by calling OnAnimationTick directly, which is what
// AnimationRegistry does each frame. No window, no device.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <cstdio>
#include <memory>

using namespace fluent;

namespace {

// Same minimal host as ExpanderTests: enough to count as attached, everything else a
// null stub. The DWriteContext is left uninitialised, so Render bails at its
// `if (!Dwrite())` guard — fine here, because every assertion below is about state and
// layout numbers rather than pixels.
class MockHost : public WindowServices {
public:
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host) {
    UIContext c;
    c.window = &host;
    c.dpiScale = 1.0f;
    return c;
}

// Build an attached, collapsed Expander with real content.
struct Fixture {
    MockHost host;
    Expander exp;
    Fixture() {
        auto content = std::make_unique<StackPanel>();
        content->Add(std::make_unique<Button>());
        exp.SetContent(std::move(content));
        exp.AttachToContext(MakeCtx(host));
    }
};

// Advance the ease the way the frame loop does, bounded so a stuck animation fails the
// test by timing out rather than hanging it. Returns the WALL-CLOCK seconds simulated,
// not a tick count.
//
// Seconds rather than ticks, because a tick count is meaningless without a refresh rate
// and this project must not assume 60 Hz: on a 240 Hz display the same ease is driven
// with dt = 1/240, so it takes 4x as many ticks to cover the same real time. Any
// assertion phrased in ticks would pass at 60 Hz and fail at 240 while the animation
// felt identical. dt is a parameter for exactly that reason — the tests below run the
// ease at several refresh rates and require the DURATION to match.
//
// This mirrors the production path: FrameScheduler::ComputeDt derives dt from the QPC
// clock (real elapsed time), so easing speed is already refresh-rate independent in the
// app. kIntervalMs = 16 is only the arm-time seed and the pacing fallback.
double RunEaseSeconds(Expander& e, float dtSec, double maxSeconds = 5.0) {
    double elapsed = 0.0;
    while (e.WantsAnimationTick() && elapsed < maxSeconds) {
        e.OnAnimationTick(dtSec);
        elapsed += dtSec;
    }
    return elapsed;
}

// Tick counts still matter for one thing: how many Measure-level relayouts the ease
// costs. Kept separate from duration so the two are never conflated.
int RunEaseTicks(Expander& e, float dtSec, int maxTicks = 5000) {
    int ticks = 0;
    while (e.WantsAnimationTick() && ticks < maxTicks) {
        e.OnAnimationTick(dtSec);
        ++ticks;
    }
    return ticks;
}

// Refresh rates this project actually has to work on. 240 is the developer's own panel.
constexpr float kDt60  = 1.0f / 60.0f;
constexpr float kDt120 = 1.0f / 120.0f;
constexpr float kDt240 = 1.0f / 240.0f;

}  // namespace

// --- Instant: the default, and the historical behaviour ------------------------

// The default argument must be Instant. This is the compatibility assertion: every
// pre-existing SetExpanded(bool) call site keeps snapping, so adding the animation
// cannot have changed how the shipped demo looks.
TEST(ExpanderTransition, DefaultSetExpandedIsInstant) {
    Fixture f;
    f.exp.SetExpanded(true);            // no Transition argument
    // Nothing left to animate: reveal already reached its target.
    EXPECT_FALSE(f.exp.WantsAnimationTick());
    EXPECT_TRUE(f.exp.IsExpanded());
    EXPECT_TRUE(f.exp.IsContentAttached());
}

// Explicit Instant behaves identically to the default, in both directions.
TEST(ExpanderTransition, ExplicitInstantSnapsBothWays) {
    Fixture f;

    f.exp.SetExpanded(true, Expander::Transition::Instant);
    EXPECT_FALSE(f.exp.WantsAnimationTick());

    f.exp.SetExpanded(false, Expander::Transition::Instant);
    EXPECT_FALSE(f.exp.WantsAnimationTick());
    EXPECT_FALSE(f.exp.IsExpanded());
    EXPECT_FALSE(f.exp.IsContentAttached());
}

// --- Animate: the capability that was previously unreachable -------------------

// The core of the feature: an animated expand leaves the element wanting ticks, so the
// frame loop drives the ease. Before this change WantsAnimationTick() was false here
// and the mid-reveal state could not be reached at all.
TEST(ExpanderTransition, AnimatedExpandLeavesWorkForTheFrameLoop) {
    Fixture f;
    f.exp.SetExpanded(true, Expander::Transition::Animate);

    EXPECT_TRUE(f.exp.IsExpanded());          // state is immediate...
    EXPECT_TRUE(f.exp.WantsAnimationTick());  // ...the reveal is not
}

// And it converges rather than ticking forever. AnimatedValue::Approach snaps once it
// is within its epsilon, so a correct ease terminates; a target that never settles
// would spin the frame loop permanently and destroy the "idle costs zero" property.
TEST(ExpanderTransition, AnimatedExpandConvergesAndThenStops) {
    Fixture f;
    f.exp.SetExpanded(true, Expander::Transition::Animate);

    const double secs = RunEaseSeconds(f.exp, kDt60);
    EXPECT_TRUE(secs > 0.0);                   // it really did animate
    EXPECT_TRUE(secs < 5.0);                   // and it terminated (did not hit the cap)
    EXPECT_FALSE(f.exp.WantsAnimationTick());  // settled: no residual ticking
}

// A mid-ease frame genuinely exists and is observable. This is what the pixel test
// could not reach before, and it is the whole point of the interface: the reveal
// passes through intermediate states instead of jumping.
//
// Observed through desired height rather than reveal_ (which is private): the content's
// contribution is reveal * contentDesiredHeight, so a partial reveal means a height
// strictly between the header alone and the fully-expanded total.
TEST(ExpanderTransition, AnimatedExpandPassesThroughIntermediateHeights) {
    Fixture f;

    // Collapsed reference: header only.
    f.exp.Measure(400.0f, 1000.0f);
    const float collapsedH = f.exp.Desired().h;

    // Fully expanded reference, via Instant.
    f.exp.SetExpanded(true, Expander::Transition::Instant);
    f.exp.Measure(400.0f, 1000.0f);
    const float expandedH = f.exp.Desired().h;
    EXPECT_TRUE(expandedH > collapsedH);       // premise: content adds height

    // Now animate from collapsed and sample one frame in.
    f.exp.SetExpanded(false, Expander::Transition::Instant);
    f.exp.SetExpanded(true, Expander::Transition::Animate);
    f.exp.OnAnimationTick(0.016f);
    f.exp.Measure(400.0f, 1000.0f);
    const float midH = f.exp.Desired().h;

    // Strictly between the two extremes: the reveal is partway.
    EXPECT_TRUE(midH > collapsedH);
    EXPECT_TRUE(midH < expandedH);

    // And finishing the ease lands exactly on the expanded height.
    RunEaseSeconds(f.exp, kDt60);
    f.exp.Measure(400.0f, 1000.0f);
    EXPECT_NEAR(f.exp.Desired().h, expandedH, 0.01f);
}

// The first frame of an animated expand must already measure the content. reveal_ is
// still 0.0 at that moment (Animate does not seed it), and the Measure/Arrange guards
// used to test reveal alone — so the content was not measured on that frame and the
// ease had no height to grow into. The guards now test `expanded_ || reveal_ > 0`.
TEST(ExpanderTransition, FirstAnimatedFrameAlreadyMeasuresContent) {
    Fixture f;
    f.exp.SetExpanded(true, Expander::Transition::Animate);

    // Measure BEFORE any tick: reveal_ is 0, but expanded_ is true.
    f.exp.Measure(400.0f, 1000.0f);
    f.exp.Arrange(RectDip{0.0f, 0.0f, 400.0f, f.exp.Desired().h});

    // The content was measured, so it has a real desired size rather than zero.
    EXPECT_TRUE(f.exp.Content() != nullptr);
    EXPECT_TRUE(f.exp.Content()->Desired().h > 0.0f);
}

// An animated collapse also animates, and — importantly — still detaches the content
// immediately. "Collapsed means detached" is load-bearing (it is what stops every
// previously-opened section staying live), so the animated path must not weaken it.
// The visible consequence, documented at the detach site, is that a collapsing
// Expander wipes a blank band rather than showing content sliding away.
TEST(ExpanderTransition, AnimatedCollapseStillDetachesContentImmediately) {
    Fixture f;
    f.exp.SetExpanded(true, Expander::Transition::Instant);
    EXPECT_TRUE(f.exp.IsContentAttached());

    f.exp.SetExpanded(false, Expander::Transition::Animate);
    EXPECT_FALSE(f.exp.IsExpanded());
    EXPECT_FALSE(f.exp.IsContentAttached());   // detached on the spot, mid-ease
    EXPECT_TRUE(f.exp.WantsAnimationTick());   // but the band is still shrinking

    RunEaseSeconds(f.exp, kDt60);
    EXPECT_FALSE(f.exp.WantsAnimationTick());
    EXPECT_FALSE(f.exp.IsContentAttached());
}

// --- The non-negotiable: idle costs zero ---------------------------------------
// "Idle costs zero" is a hard constraint in this codebase, and an animation is the
// classic way to break it: an ease whose WantsAnimationTick() never goes false keeps
// the frame loop awake forever, burning a core on a window nobody is touching. The
// symptom is invisible locally (everything still looks right) and it destroys the
// INFINITE input wait for the whole application.
//
// So this asserts the falling edge explicitly, in both directions, and that a second
// pass over a settled Expander stays settled — i.e. nothing re-arms the tick.
TEST(ExpanderTransition, ReturnsToZeroCostIdleAfterAnimating) {
    Fixture f;

    // Expand with animation, run it out.
    f.exp.SetExpanded(true, Expander::Transition::Animate);
    EXPECT_TRUE(f.exp.WantsAnimationTick());
    RunEaseSeconds(f.exp, kDt60);
    EXPECT_FALSE(f.exp.WantsAnimationTick());

    // Extra ticks on a settled element must not re-arm it. If OnAnimationTick had any
    // residual motion (a target that keeps moving, a value that overshoots and
    // oscillates), this is where it would show up.
    for (int i = 0; i < 10; ++i) f.exp.OnAnimationTick(0.016f);
    EXPECT_FALSE(f.exp.WantsAnimationTick());

    // Same on the way back down.
    f.exp.SetExpanded(false, Expander::Transition::Animate);
    EXPECT_TRUE(f.exp.WantsAnimationTick());
    RunEaseSeconds(f.exp, kDt60);
    EXPECT_FALSE(f.exp.WantsAnimationTick());
    for (int i = 0; i < 10; ++i) f.exp.OnAnimationTick(0.016f);
    EXPECT_FALSE(f.exp.WantsAnimationTick());
}

// The ease must terminate in a BOUNDED number of frames, not merely converge
// asymptotically. AnimatedValue::Approach snaps once within its epsilon, which is what
// makes this finite; without that snap an exponential approach never exactly reaches its
// target and the element would tick forever at ever-shrinking deltas — and because this
// control's tick is Measure-level, each of those wasted frames re-lays out the content.
//
// The bound is expressed against the theme's own motion vocabulary rather than a
// hand-picked number: MotionTokens defines fastMs 90 / normalMs 150 / slowMs 300, so a
// disclosure animation finishing within slowMs plus a frame of slack is "prompt" by this
// framework's own definition. kRevealTau stays free to be retuned inside that envelope
// without editing this test.
//
// MEASURED HISTORY, worth keeping because the first version of this test failed and the
// failure was the useful part:
//   * ~432 ms originally — OnAnimationTick passed snapEps 0.001, ten times tighter than
//     every other control's default, so the last 0.9% of the reveal held the element in
//     the active set for ~14 extra Measure-level frames.
//   * ~300 ms after dropping to the default 0.01.
// The remaining duration is kRevealTau's shape, not waste.
//
// Asserted in SECONDS, at three refresh rates. An earlier version counted ticks and
// converted at a hard-coded 60 Hz, which is simply wrong on a 120 or 240 Hz panel: the
// same ease is driven with a smaller dt and therefore takes proportionally more ticks.
TEST(ExpanderTransition, EaseDurationIsBoundedAtEveryRefreshRate) {
    struct Case { const char* name; float dt; };
    const Case cases[] = {
        {"60 Hz",  kDt60},
        {"120 Hz", kDt120},
        {"240 Hz", kDt240},
    };

    for (const Case& c : cases) {
        Fixture f;
        f.exp.SetExpanded(true, Expander::Transition::Animate);
        const double secs = RunEaseSeconds(f.exp, c.dt);
        std::printf("  %s: reveal settled in %.0f ms\n", c.name, secs * 1000.0);

        EXPECT_TRUE(secs > c.dt);                    // really eased, not a snap
        EXPECT_TRUE(secs * 1000.0 <= 300.0 + 17.0);  // within slowMs + a frame of slack
        EXPECT_FALSE(f.exp.WantsAnimationTick());
    }
}

// THE PROPERTY THAT MATTERS ON A HIGH-REFRESH DISPLAY: the ease takes the same WALL-CLOCK
// time at 60, 120 and 240 Hz. A higher refresh rate must buy smoother motion (more
// intermediate frames), never faster motion.
//
// This is what a dt-per-frame animation gets right and a dt-per-tick-count animation gets
// wrong. If OnAnimationTick ever moved reveal_ by a fixed step instead of using dtSec, the
// reveal would run 4x faster on this 240 Hz machine than on a 60 Hz one and feel broken.
// FrameScheduler::ComputeDt already derives dt from the QPC clock for exactly this reason;
// this test pins the control's half of that contract.
TEST(ExpanderTransition, EaseDurationIsIndependentOfRefreshRate) {
    Fixture a, b, c;
    a.exp.SetExpanded(true, Expander::Transition::Animate);
    b.exp.SetExpanded(true, Expander::Transition::Animate);
    c.exp.SetExpanded(true, Expander::Transition::Animate);

    const double s60  = RunEaseSeconds(a.exp, kDt60);
    const double s120 = RunEaseSeconds(b.exp, kDt120);
    const double s240 = RunEaseSeconds(c.exp, kDt240);

    std::printf("  60Hz=%.1f ms  120Hz=%.1f ms  240Hz=%.1f ms\n",
                s60 * 1000.0, s120 * 1000.0, s240 * 1000.0);

    // Within one 60 Hz frame of each other. Exact equality is not achievable: the ease
    // stops on whichever tick first lands inside the snap epsilon, and the tick grid
    // differs per refresh rate, so the finish can only be resolved to +/- one tick.
    const double tol = kDt60;
    EXPECT_NEAR(s120, s60, tol);
    EXPECT_NEAR(s240, s60, tol);
}

// And the flip side, stated separately because it is the reason high refresh is worth
// having: the same duration is covered by proportionally MORE frames. Those frames are
// each a Measure-level relayout, which is why the per-tick cost measured by the
// FluentUIBench "ExpanderReveal" scene has to stay small — at 240 Hz there are 4x as many
// of them inside the same ~300 ms.
TEST(ExpanderTransition, HigherRefreshRateYieldsMoreIntermediateFrames) {
    Fixture a, b;
    a.exp.SetExpanded(true, Expander::Transition::Animate);
    b.exp.SetExpanded(true, Expander::Transition::Animate);

    const int t60  = RunEaseTicks(a.exp, kDt60);
    const int t240 = RunEaseTicks(b.exp, kDt240);
    std::printf("  ticks: 60Hz=%d  240Hz=%d (ratio %.2f)\n",
                t60, t240, static_cast<double>(t240) / t60);

    EXPECT_TRUE(t240 > t60 * 3);   // ~4x, allowing for snap-tick granularity
}

// A redundant set must stay silent whichever transition is requested — the early-out
// is before any reveal handling, so asking to animate to the state you are already in
// must not start an ease (which would tick the frame loop for nothing).
TEST(ExpanderTransition, RedundantSetStartsNoAnimation) {
    Fixture f;
    f.exp.SetExpanded(true, Expander::Transition::Instant);
    EXPECT_FALSE(f.exp.WantsAnimationTick());

    f.exp.SetExpanded(true, Expander::Transition::Animate);   // already expanded
    EXPECT_FALSE(f.exp.WantsAnimationTick());
}

// --- The user-gesture transition ----------------------------------------------

// Default for a header click is Instant, matching the programmatic default: enabling
// motion is opt-in, so upgrading the library cannot introduce an animation nobody
// asked for.
TEST(ExpanderTransition, UserToggleDefaultsToInstant) {
    Fixture f;
    EXPECT_TRUE(f.exp.UserToggleTransition() == Expander::Transition::Instant);
}

// Setting it to Animate makes the keyboard/pointer path animate. Driven through
// OnKeyDownRouted rather than a private helper, so this exercises the real gesture
// route a user takes.
TEST(ExpanderTransition, UserToggleHonoursAnimateWhenOptedIn) {
    Fixture f;
    f.exp.SetUserToggleTransition(Expander::Transition::Animate);

    KeyEventArgs e{};
    e.vk = VK_SPACE;
    f.exp.OnKeyDownRouted(e);

    EXPECT_TRUE(e.handled);
    EXPECT_TRUE(f.exp.IsExpanded());
    EXPECT_TRUE(f.exp.WantsAnimationTick());   // the gesture animated
}

// ...and left at the default it snaps, through the same route.
TEST(ExpanderTransition, UserToggleSnapsWhenLeftAtDefault) {
    Fixture f;

    KeyEventArgs e{};
    e.vk = VK_SPACE;
    f.exp.OnKeyDownRouted(e);

    EXPECT_TRUE(f.exp.IsExpanded());
    EXPECT_FALSE(f.exp.WantsAnimationTick());
}

// The gesture setting must not leak into programmatic calls: they carry their own
// argument. A control configured for animated clicks still snaps when app code asks
// for Instant, which is what lets a page restore saved state without a burst of
// animation on load.
TEST(ExpanderTransition, GestureSettingDoesNotAffectProgrammaticCalls) {
    Fixture f;
    f.exp.SetUserToggleTransition(Expander::Transition::Animate);

    f.exp.SetExpanded(true, Expander::Transition::Instant);
    EXPECT_FALSE(f.exp.WantsAnimationTick());
}
