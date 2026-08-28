// AnimationRegistryTests.cpp — unit tests for the active-animation set (F2).
// Pure logic (no HWND/D2D): verifies Collect gathers only the elements that
// currently want a tick (recursing panels, skipping static leaves) and that
// Tick advances only the active set, drops finished elements, and reports when
// the set has drained.

#include "../framework/Test.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

namespace {
// A leaf that animates for a fixed number of ticks, then reports done. Records
// how many times it was ticked so tests can prove the registry ticks only the
// active set and never a dropped or static element.
class CountingAnim : public FrameworkElement {
public:
    explicit CountingAnim(int ticksToLive) : remaining_(ticksToLive) {}
    bool WantsAnimationTick() const override { return remaining_ > 0; }
    void OnAnimationTick(float) override { ++ticks_; if (remaining_ > 0) --remaining_; }
    void Render(const DrawingContext&) override {}
    int ticks() const { return ticks_; }
private:
    int remaining_ = 0;
    int ticks_ = 0;
};
} // namespace

// Collect gathers only elements that currently want a tick.
TEST(AnimationRegistry, CollectGathersOnlyAnimating) {
    CountingAnim animating(3);
    CountingAnim idle(0);
    std::vector<UIElement*> roots = {&animating, &idle};

    AnimationRegistry reg;
    reg.Collect(roots);
    EXPECT_EQ(reg.Count(), 1u);
    EXPECT_FALSE(reg.Empty());
}

// Collect recurses into panels, gathering animating descendants.
TEST(AnimationRegistry, CollectRecursesIntoPanels) {
    StackPanel root;
    auto* a = root.Emplace<CountingAnim>(2);   // animating
    root.Emplace<CountingAnim>(0);             // static, should be skipped
    auto* nested = root.Emplace<StackPanel>();
    auto* b = nested->Emplace<CountingAnim>(2); // animating, one level deeper

    std::vector<UIElement*> roots = {&root};
    AnimationRegistry reg;
    reg.Collect(roots);
    EXPECT_EQ(reg.Count(), 2u);
    (void)a; (void)b;
}

// Tick advances every active element once per call.
TEST(AnimationRegistry, TickAdvancesActiveSet) {
    CountingAnim a(5), b(5);
    std::vector<UIElement*> roots = {&a, &b};
    AnimationRegistry reg;
    reg.Collect(roots);

    reg.Tick(0.016f);
    EXPECT_EQ(a.ticks(), 1);
    EXPECT_EQ(b.ticks(), 1);
    reg.Tick(0.016f);
    EXPECT_EQ(a.ticks(), 2);
    EXPECT_EQ(b.ticks(), 2);
}

// A finished element is dropped from the set; Tick returns false once drained.
TEST(AnimationRegistry, DropsFinishedAndReportsDrained) {
    CountingAnim shortLived(1);  // done after one tick
    CountingAnim longLived(3);
    std::vector<UIElement*> roots = {&shortLived, &longLived};
    AnimationRegistry reg;
    reg.Collect(roots);
    EXPECT_EQ(reg.Count(), 2u);

    bool more = reg.Tick(0.016f);   // shortLived finishes, longLived continues
    EXPECT_TRUE(more);
    EXPECT_EQ(reg.Count(), 1u);

    // longLived had 3 ticks of life; it was ticked once above, needs 2 more.
    EXPECT_TRUE(reg.Tick(0.016f));
    EXPECT_FALSE(reg.Tick(0.016f)); // now drained
    EXPECT_TRUE(reg.Empty());
}

// A dropped element is never ticked again (proves we tick the set, not the tree).
TEST(AnimationRegistry, DroppedElementNotTickedAgain) {
    CountingAnim shortLived(1);
    CountingAnim longLived(5);
    std::vector<UIElement*> roots = {&shortLived, &longLived};
    AnimationRegistry reg;
    reg.Collect(roots);

    reg.Tick(0.016f);  // shortLived: 1 tick, now done and dropped
    reg.Tick(0.016f);  // longLived only
    reg.Tick(0.016f);
    EXPECT_EQ(shortLived.ticks(), 1);  // never ticked after being dropped
    EXPECT_EQ(longLived.ticks(), 3);
}

// Re-collecting after a state change picks up an element that started animating
// without any call on the element itself (the RadioButton-sibling case).
TEST(AnimationRegistry, RecollectPicksUpNewlyAnimating) {
    CountingAnim a(2);
    CountingAnim sibling(0);   // not animating yet
    std::vector<UIElement*> roots = {&a, &sibling};
    AnimationRegistry reg;
    reg.Collect(roots);
    EXPECT_EQ(reg.Count(), 1u);

    // Simulate a shared-state change that makes `sibling` want to animate,
    // discovered only by a re-collect (as UpdateAnimationTimer does on a trigger).
    CountingAnim siblingNow(2);
    std::vector<UIElement*> roots2 = {&a, &siblingNow};
    reg.Collect(roots2);
    EXPECT_EQ(reg.Count(), 2u);
}
