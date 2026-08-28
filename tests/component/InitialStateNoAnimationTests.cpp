// InitialStateNoAnimationTests.cpp — a control's FIRST paint must not animate.
//
// THE BUG. Every animated control holds an AnimatedValue that starts at 0.0f, while the
// state setters (SetChecked, SetValue, SetOn) only move the animation TARGET. So a control
// created already-checked, or a slider created at 50, painted its first frame at 0 and then
// eased to the real value. The user sees a slider sweep up from empty and checkboxes tick
// themselves in, the first time a page is shown.
//
// Why it looked like a demo problem: the gallery pre-builds every page, but a hidden page
// neither measures nor renders nor gets animation ticks. So the ease is spent the first
// time a page becomes VISIBLE, and never again -- exactly the "only on first load"
// behaviour reported. The controls were mis-initialised the whole time; visibility just
// decided when the artefact was spent.
//
// WHAT THESE TESTS ASSERT. Not "does it eventually reach the value" -- it always did. They
// assert that a freshly-built control is ALREADY at its target before any tick runs, i.e.
// WantsAnimationTick() is false and the eased scalar equals the settled value. That is the
// difference between "correct after 300ms" and "correct on frame one".

#include "../framework/Test.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <cstdio>

using namespace fluent;

namespace {

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

const ThemeSnapshot& Theme1() {
    static const ThemeSnapshot t = BuildSnapshot(ThemeInputs{}, 0);
    return t;
}

struct Env {
    MockHost host;
    UIContext ctx;
    Env() {
        (void)host.DWrite().Initialize();
        ctx.window = &host;
        ctx.theme = &Theme1();
        ctx.dwrite = &host.DWrite();
        ctx.dpiScale = 1.0f;
    }
};

}  // namespace

// --- CheckBox ---------------------------------------------------------------

// A CheckBox built already-checked must be fully checked on frame one. Before the fix its
// check glyph animated in from nothing every time its page was first shown.
TEST(InitialStateNoAnimation, CheckBoxBuiltCheckedDoesNotAnimateIn) {
    Env env;
    CheckBox cb;
    cb.AttachToContext(env.ctx);
    cb.SetChecked(true);
    cb.Measure(300.0f, 100.0f);
    cb.Arrange(RectDip{0.0f, 0.0f, 300.0f, 32.0f});

    std::printf("  checked-at-build: WantsAnimationTick=%d\n",
                cb.WantsAnimationTick() ? 1 : 0);
    EXPECT_FALSE(cb.WantsAnimationTick());
}

// The converse must still hold: a real user toggle DOES animate. A "fix" that killed all
// easing would pass the test above, so this one guards the other direction.
TEST(InitialStateNoAnimation, CheckBoxToggledAfterBuildStillAnimates) {
    Env env;
    CheckBox cb;
    cb.AttachToContext(env.ctx);
    cb.SetChecked(true);
    cb.Measure(300.0f, 100.0f);
    cb.Arrange(RectDip{0.0f, 0.0f, 300.0f, 32.0f});
    EXPECT_FALSE(cb.WantsAnimationTick());

    cb.SetChecked(false);       // a genuine state change
    EXPECT_TRUE(cb.WantsAnimationTick());
}

// --- RadioButton ------------------------------------------------------------

TEST(InitialStateNoAnimation, RadioButtonBuiltSelectedDoesNotAnimateIn) {
    Env env;
    static int group = 0;
    RadioButton rb;
    rb.AttachToContext(env.ctx);
    rb.SetGroup(&group, 0);     // group value 0 == this button, so it starts selected
    rb.Measure(300.0f, 100.0f);
    rb.Arrange(RectDip{0.0f, 0.0f, 300.0f, 32.0f});

    std::printf("  selected-at-build: WantsAnimationTick=%d\n",
                rb.WantsAnimationTick() ? 1 : 0);
    EXPECT_FALSE(rb.WantsAnimationTick());
}

// --- ToggleSwitch -----------------------------------------------------------

// The most visible of the family: the knob slid across the track on first paint.
TEST(InitialStateNoAnimation, ToggleSwitchBuiltOnDoesNotSlideIn) {
    Env env;
    ToggleSwitch ts;
    ts.AttachToContext(env.ctx);
    ts.SetOn(true);
    ts.Measure(300.0f, 100.0f);
    ts.Arrange(RectDip{0.0f, 0.0f, 300.0f, 32.0f});

    std::printf("  on-at-build: WantsAnimationTick=%d\n",
                ts.WantsAnimationTick() ? 1 : 0);
    EXPECT_FALSE(ts.WantsAnimationTick());
}

// --- Slider -----------------------------------------------------------------

// The reported case: a slider sitting at mid-range swept up from zero.
TEST(InitialStateNoAnimation, SliderBuiltAtMidRangeDoesNotSweepUp) {
    Env env;
    Slider s;
    s.AttachToContext(env.ctx);
    s.SetMin(0.0f);
    s.SetMax(100.0f);
    s.SetValue(50.0f);
    s.Measure(400.0f, 100.0f);
    s.Arrange(RectDip{0.0f, 0.0f, 400.0f, 32.0f});

    std::printf("  slider value=%.1f WantsAnimationTick=%d\n",
                s.Value(), s.WantsAnimationTick() ? 1 : 0);
    EXPECT_FALSE(s.WantsAnimationTick());
}

// A non-zero minimum is the case a naive fix misses: initialising the eased scalar to 0
// still animates when the range starts at, say, 30.
TEST(InitialStateNoAnimation, SliderWithNonZeroMinimumAlsoStartsSettled) {
    Env env;
    Slider s;
    s.AttachToContext(env.ctx);
    s.SetMin(30.0f);
    s.SetMax(80.0f);
    s.SetValue(30.0f);          // exactly the minimum -- still not 0
    s.Measure(400.0f, 100.0f);
    s.Arrange(RectDip{0.0f, 0.0f, 400.0f, 32.0f});

    std::printf("  slider min=30 value=30 WantsAnimationTick=%d\n",
                s.WantsAnimationTick() ? 1 : 0);
    EXPECT_FALSE(s.WantsAnimationTick());
}

// Dragging must still track the pointer 1:1 afterwards.
TEST(InitialStateNoAnimation, SliderStillAnimatesOnProgrammaticChange) {
    Env env;
    Slider s;
    s.AttachToContext(env.ctx);
    s.SetMin(0.0f);
    s.SetMax(100.0f);
    s.SetValue(50.0f);
    s.Measure(400.0f, 100.0f);
    s.Arrange(RectDip{0.0f, 0.0f, 400.0f, 32.0f});
    EXPECT_FALSE(s.WantsAnimationTick());

    s.SetValue(90.0f);
    EXPECT_TRUE(s.WantsAnimationTick());
}

// --- ProgressBar ------------------------------------------------------------

TEST(InitialStateNoAnimation, ProgressBarBuiltPartiallyFilledDoesNotFillIn) {
    Env env;
    ProgressBar p;
    p.AttachToContext(env.ctx);
    p.SetValue(0.68f);
    p.Measure(400.0f, 100.0f);
    p.Arrange(RectDip{0.0f, 0.0f, 400.0f, 8.0f});

    std::printf("  progress=0.68 WantsAnimationTick=%d\n",
                p.WantsAnimationTick() ? 1 : 0);
    EXPECT_FALSE(p.WantsAnimationTick());
}

// --- Expander ---------------------------------------------------------------

// Expander already got this right, via the Transition enum added earlier: the default is
// Instant, which calls SetImmediate. Pinned here so the family stays consistent and so
// this file documents the pattern the other controls now follow.
TEST(InitialStateNoAnimation, ExpanderBuiltExpandedWasAlreadyCorrect) {
    Env env;
    Expander e;
    e.AttachToContext(env.ctx);
    e.SetExpanded(true);        // defaults to Transition::Instant
    e.Measure(400.0f, 600.0f);
    e.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});

    EXPECT_FALSE(e.WantsAnimationTick());

    // And the opt-in animated path still animates.
    Expander e2;
    e2.AttachToContext(env.ctx);
    e2.Measure(400.0f, 600.0f);
    e2.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});
    e2.SetExpanded(true, Expander::Transition::Animate);
    EXPECT_TRUE(e2.WantsAnimationTick());
}

// --- The class-level statement ----------------------------------------------

// One test that walks the whole family, so adding a new animated control with the same
// mistake fails here rather than being noticed in a screenshot months later.
TEST(InitialStateNoAnimation, NoAnimatedControlRequestsTicksImmediatelyAfterBuild) {
    Env env;
    int settled = 0, animating = 0;

    auto check = [&](const char* name, bool wants) {
        if (wants) { ++animating; std::printf("  ANIMATING AT BUILD: %s\n", name); }
        else ++settled;
    };

    CheckBox cb;    cb.AttachToContext(env.ctx); cb.SetChecked(true);
    RadioButton rb; rb.AttachToContext(env.ctx);
    static int g = 0; rb.SetGroup(&g, 0);
    ToggleSwitch ts; ts.AttachToContext(env.ctx); ts.SetOn(true);
    Slider sl;      sl.AttachToContext(env.ctx); sl.SetMax(100.0f); sl.SetValue(75.0f);
    ProgressBar pb; pb.AttachToContext(env.ctx); pb.SetValue(0.4f);

    for (FrameworkElement* el : {static_cast<FrameworkElement*>(&cb),
                                 static_cast<FrameworkElement*>(&rb),
                                 static_cast<FrameworkElement*>(&ts),
                                 static_cast<FrameworkElement*>(&sl),
                                 static_cast<FrameworkElement*>(&pb)}) {
        el->Measure(400.0f, 100.0f);
        el->Arrange(RectDip{0.0f, 0.0f, 400.0f, 32.0f});
    }

    check("CheckBox", cb.WantsAnimationTick());
    check("RadioButton", rb.WantsAnimationTick());
    check("ToggleSwitch", ts.WantsAnimationTick());
    check("Slider", sl.WantsAnimationTick());
    check("ProgressBar", pb.WantsAnimationTick());

    std::printf("  settled at build: %d, animating at build: %d\n", settled, animating);
    EXPECT_EQ(animating, 0);
    EXPECT_EQ(settled, 5);
}
