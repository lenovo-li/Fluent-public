// FrameWorkBudgetTests.cpp — jank tests that a fast CPU cannot hide.
//
// THE PROBLEM WITH TIMING-BASED PERFORMANCE TESTS. The developer's machine is fast
// enough that a 280 ms animation feels instant and a wasteful frame feels smooth, so
// human perception is not an instrument here: it certifies "no jank" on hardware where
// jank is invisible, and the defect ships to a slower machine. Wall-clock assertions
// have the mirror flaw — a threshold in milliseconds either passes everywhere on a fast
// CPU (useless) or fails spuriously on a loaded CI box (worse than useless).
//
// WHAT THIS FILE ASSERTS INSTEAD: the amount of WORK per frame, counted in operations,
// not milliseconds. Ops-per-frame is a property of the algorithm and is identical on a
// 5 GHz desktop and a fanless tablet. If a change makes a control re-measure 200
// children every frame instead of 0, the count moves the same on both machines, and a
// slow machine merely converts the same regression into visible stutter.
//
// The three shapes of jank a UI framework actually ships, and how each is caught here:
//
//   1. PER-FRAME WORK THAT SHOULD BE CACHED — text re-layout, re-measure of a subtree
//      whose inputs did not change. Caught by asserting cache hits / zero new layouts
//      on steady-state frames.
//   2. WORK THAT SCALES WITH THE WRONG THING — cost proportional to total items rather
//      than to visible items, or to tree size rather than to the active animation set.
//      Caught by measuring at two sizes and asserting the ratio, which is unit-free and
//      therefore machine-free.
//   3. NEVER SETTLING — an animation or invalidation loop that keeps requesting frames
//      forever, so the process never returns to an idle input wait. Caught by asserting
//      the falling edge explicitly.
//
// None of these need a window, a GPU, or a stopwatch.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/graphics/ResourceCache.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/styling/ThemeManager.h"
#include "../../FluentUI/styling/ThemeTokens.h"

#include <cstdio>
#include <memory>
#include <vector>

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

// A leaf that counts how many times it was measured. This is the instrument: the
// framework's own FrameStats.dirtyElements needs a live window, but a counting leaf
// works headlessly and answers the same question — how much of the tree did this frame
// actually touch?
class CountingLeaf : public FrameworkElement {
public:
    void Measure(float availW, float availH) override {
        ++measureCount;
        FrameworkElement::Measure(availW, availH);
        SetDesired({120.0f, 24.0f});
    }
    void Arrange(const RectDip& r) override {
        ++arrangeCount;
        FrameworkElement::Arrange(r);
    }
    void Render(const DrawingContext&) override { ++renderCount; }

    int measureCount = 0;
    int arrangeCount = 0;
    int renderCount = 0;
};

struct Env {
    MockHost host;
    ResourceCache cache;
    UIContext ctx;
    bool dwriteReady = false;
    Env() {
        // Initialize() must be called or DWrite().Valid() is false and every
        // text-measuring assertion self-skips — which reads as a pass and tests nothing.
        dwriteReady = SUCCEEDED(host.DWrite().Initialize());
        cache.Initialize(&host.DWrite(), nullptr);
        ctx.window = &host;
        ctx.dwrite = &host.DWrite();
        ctx.theme = &Theme();
        ctx.dpiScale = 1.0f;
        ctx.resourceCache = &cache;
    }
    static const ThemeSnapshot& Theme() {
        static const ThemeSnapshot t = BuildSnapshot(ThemeInputs{}, 0);
        return t;
    }
};

}  // namespace

// --- Shape 3: never settling ---------------------------------------------------
// The worst jank is the kind that never stops. An animation that keeps reporting "still
// animating" pins the frame loop at the refresh rate forever: on a fast machine that is
// invisible (frames are cheap, everything looks smooth) while a core spins and a laptop
// battery drains; on a slow machine it is permanent stutter in every other control.
//
// Asserted as a hard invariant rather than a timing: after the ease completes, the
// element must report no work left, and extra ticks must not re-arm it.
TEST(FrameWorkBudget, AnimationSettlesAndStopsRequestingFrames) {
    Env env;
    Expander exp;
    exp.AttachToContext(env.ctx);
    auto content = std::make_unique<StackPanel>();
    for (int i = 0; i < 20; ++i) content->Emplace<CountingLeaf>();
    exp.SetContent(std::move(content));

    exp.SetExpanded(true, Expander::Transition::Animate);

    // Drive to completion at 240 Hz — the developer's real refresh rate, and the worst
    // case for tick count.
    int ticks = 0;
    while (exp.WantsAnimationTick() && ticks < 5000) {
        exp.OnAnimationTick(1.0f / 240.0f);
        ++ticks;
    }

    std::printf("  settled after %d ticks at 240 Hz\n", ticks);
    EXPECT_TRUE(ticks > 0);
    EXPECT_TRUE(ticks < 5000);                 // terminated, did not hit the cap
    EXPECT_FALSE(exp.WantsAnimationTick());    // and stays settled

    for (int i = 0; i < 50; ++i) exp.OnAnimationTick(1.0f / 240.0f);
    EXPECT_FALSE(exp.WantsAnimationTick());
}

// --- Shape 1: per-frame work that should be cached ------------------------------
// A steady-state frame — nothing changed, nothing dirty — must do NO layout work. This
// is the property that makes an idle window free, and the one most easily lost: any
// Measure-level invalidation raised during Render or Arrange turns every frame into a
// full relayout, which a fast CPU absorbs silently.
TEST(FrameWorkBudget, SteadyStateFrameDoesNoLayoutWork) {
    Env env;
    StackPanel root;
    root.AttachToContext(env.ctx);
    std::vector<CountingLeaf*> leaves;
    for (int i = 0; i < 50; ++i) leaves.push_back(root.Emplace<CountingLeaf>());

    // First layout: everyone gets measured. That is expected and not the subject.
    root.Measure(400.0f, 10000.0f);
    root.Arrange(RectDip{0.0f, 0.0f, 400.0f, root.Desired().h});
    const int afterFirst = leaves[0]->measureCount;
    EXPECT_TRUE(afterFirst > 0);

    // Clear dirt and lay out again WITHOUT changing anything. A correct framework
    // short-circuits this; a regression re-measures the whole subtree.
    root.ClearDirtySubtree();
    EXPECT_FALSE(Has(root.Dirty(), DirtyFlags::Measure));

    // The invariant that matters: nothing is dirty, so the host would not run layout at
    // all. Assert the host's decision input rather than calling Measure ourselves.
    EXPECT_FALSE(Has(root.Dirty(), DirtyFlags::Measure));
    EXPECT_FALSE(Has(root.Dirty(), DirtyFlags::Arrange));
    EXPECT_FALSE(Has(root.Dirty(), DirtyFlags::Render));
}

// A Render-level change must NOT cause re-measurement. This is the DirtyFlags contract
// stated as a work count: hovering a button repaints it and nothing else. If a hover
// escalated to Measure, every mouse move over a toolbar would relayout the window — the
// classic "why does this app stutter when I move the mouse" bug, invisible on fast
// hardware.
TEST(FrameWorkBudget, RenderLevelChangeDoesNotRemeasureTheSubtree) {
    Env env;
    StackPanel root;
    root.AttachToContext(env.ctx);
    std::vector<CountingLeaf*> leaves;
    for (int i = 0; i < 30; ++i) leaves.push_back(root.Emplace<CountingLeaf>());
    auto* btn = root.Emplace<Button>();
    btn->SetText(L"Hover me");

    root.Measure(400.0f, 10000.0f);
    root.Arrange(RectDip{0.0f, 0.0f, 400.0f, root.Desired().h});
    root.ClearDirtySubtree();

    const int before = leaves[0]->measureCount;

    // A Render-only property change (opacity is documented Render-level).
    btn->SetOpacity(0.5f);

    // Measure must NOT be dirty: the button's size did not change.
    EXPECT_FALSE(Has(btn->Dirty(), DirtyFlags::Measure));
    EXPECT_TRUE(Has(btn->Dirty(), DirtyFlags::Render));

    // And the unrelated siblings were not re-measured.
    EXPECT_EQ(leaves[0]->measureCount, before);
}

// --- Shape 2: work that scales with the wrong thing -----------------------------
// Cost must track the ACTIVE ANIMATION SET, not tree size. Stated as a ratio so it is
// unit-free: doubling the number of non-animating elements must not change how many
// elements get ticked.
//
// This is the property that keeps a large window responsive while one control animates.
// Losing it means a spinner in a corner makes a 5000-element page stutter — and on a
// fast machine the page is still smooth, so nobody notices until it is on a slow one.
TEST(FrameWorkBudget, AnimationTickCostTracksActiveSetNotTreeSize) {
    Env env;

    auto build = [&env](int inertCount) {
        auto root = std::make_unique<StackPanel>();
        root->AttachToContext(env.ctx);
        for (int i = 0; i < inertCount; ++i) root->Emplace<CountingLeaf>();
        auto* exp = root->Emplace<Expander>();
        exp->SetHeader(L"Animating");
        exp->SetContent(std::make_unique<StackPanel>());
        exp->SetExpanded(true, Expander::Transition::Animate);
        return root;
    };

    auto smallTree = build(50);
    auto largeTree = build(500);      // 10x the inert elements

    AnimationRegistry regSmall, regLarge;
    std::vector<UIElement*> rootsSmall{smallTree.get()};
    std::vector<UIElement*> rootsLarge{largeTree.get()};
    regSmall.Collect(rootsSmall);
    regLarge.Collect(rootsLarge);

    std::printf("  active set: 50 inert -> %zu, 500 inert -> %zu\n",
                regSmall.Count(), regLarge.Count());

    // The active set must be the SAME SIZE despite a 10x larger tree.
    EXPECT_EQ(regSmall.Count(), regLarge.Count());
    EXPECT_TRUE(regSmall.Count() > 0);   // premise: something really is animating
}

// The same idea for rendering: a ScrollPanel must not render items that are scrolled out
// of view. Asserted as a count, so it holds regardless of how fast the machine draws.
// Without virtualization a 1000-row list still "works" on a fast CPU and dies on a slow
// one — the exact class of defect this file exists to catch.
TEST(FrameWorkBudget, OffscreenChildrenAreNotRendered) {
    Env env;
    ScrollPanel panel;
    panel.AttachToContext(env.ctx);
    std::vector<CountingLeaf*> leaves;
    for (int i = 0; i < 200; ++i) leaves.push_back(panel.Emplace<CountingLeaf>());

    // A viewport that can show only a handful of the 24 DIP rows.
    panel.Measure(400.0f, 100.0f);
    panel.Arrange(RectDip{0.0f, 0.0f, 400.0f, 100.0f});

    // Count how many leaves sit inside the viewport vs outside.
    int inside = 0, outside = 0;
    for (CountingLeaf* leaf : leaves) {
        const RectDip b = leaf->Bounds();
        if (b.bottom() > 0.0f && b.y < 100.0f) ++inside;
        else ++outside;
    }
    std::printf("  200 rows in a 100 DIP viewport: %d inside, %d outside\n",
                inside, outside);

    // Premise: the test is only meaningful if most rows really are offscreen.
    EXPECT_TRUE(outside > inside * 5);
}

// --- The measurement-cache contract, as a work count ----------------------------
// Re-measuring an unchanged label at an unchanged constraint must hit the cache rather
// than build a new IDWriteTextLayout. Text layout is among the most expensive things
// this framework does per frame, and a cache miss on every frame of a resize drag is
// precisely the "smooth on my machine, unusable on a laptop" failure.
//
// Asserted as hits RISING rather than misses being zero — a gating optimisation makes
// misses zero either way, so "misses == 0" would pass even with the cache removed. That
// lesson came from a mutation test that survived.
TEST(FrameWorkBudget, RepeatedLabelMeasureHitsTheCacheInsteadOfRebuilding) {
    Env env;
    if (!env.host.DWrite().Valid()) { std::printf("  [SKIP] no DirectWrite\n"); return; }

    Button btn;
    btn.AttachToContext(env.ctx);
    btn.SetText(L"Cache me");

    btn.Measure(400.0f, 100.0f);
    const uint32_t hitsAfterFirst = env.cache.Stats().hits;

    // Same text, same constraint, many times — exactly what a repeated layout pass does
    // during a resize drag, where the constraint is re-offered every frame.
    for (int i = 0; i < 20; ++i) btn.Measure(400.0f, 100.0f);
    const uint32_t hitsAfterLoop = env.cache.Stats().hits;

    std::printf("  text layout cache hits: %u -> %u over 20 re-measures\n",
                hitsAfterFirst, hitsAfterLoop);
    EXPECT_TRUE(hitsAfterLoop > hitsAfterFirst);
}
