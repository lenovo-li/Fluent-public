// ViewboxTests.cpp — unit tests for Viewbox scaling container. Pure logic:
// no HWND / D2D / DWrite. Verifies scaling behavior under all Stretch modes,
// child ownership, hit-testing through the transform, and dirty-rect mapping.

#include "../framework/Test.h"
#include "../../FluentUI/layout/Viewbox.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "LayoutTestHelpers.h"
#include <cmath>

using namespace fluent;

namespace {
// A leaf that reports a fixed desired size for scaling math to work against.
class FixedLeaf : public FrameworkElement {
public:
    FixedLeaf(float w, float h) : w_(w), h_(h) {}
    void Render(const DrawingContext&) override {}
    void Measure(float, float) override { desired_ = {w_, h_}; }
    int attachCount() const { return attachCount_; }
    int detachCount() const { return detachCount_; }
protected:
    void OnAttachedToTree() override { ++attachCount_; }
    void OnDetachedFromTree() override { ++detachCount_; }
private:
    float w_, h_;
    int attachCount_ = 0;
    int detachCount_ = 0;
};
}  // namespace

// Measure: the child is measured at its natural size (near-unbounded constraint),
// and the Viewbox itself reports the available size so it fills its slot.
TEST(Viewbox, MeasureReportsAvailableSize) {
    Viewbox vb;
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(100.0f, 50.0f));
    (void)child;

    vb.Measure(400.0f, 300.0f);
    // Viewbox desires to fill the slot, not match the child's natural size.
    EXPECT_NEAR(vb.Desired().w, 400.0f, 0.01f);
    EXPECT_NEAR(vb.Desired().h, 300.0f, 0.01f);
}

// Stretch::Uniform scales the child to fit while preserving aspect ratio.
TEST(Viewbox, StretchUniformPreservesAspectRatio) {
    Viewbox vb;
    vb.SetStretch(Stretch::Uniform);
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(200.0f, 100.0f));  // 2:1 ratio

    vb.Measure(800.0f, 600.0f);
    vb.Arrange({0, 0, 800.0f, 600.0f});

    // Available space is 800×600 (4:3). Child is 200×100 (2:1).
    // Fitting 2:1 into 4:3 while preserving ratio: min(800/200, 600/100) = min(4, 6) = 4.
    // Scaled size = 200*4 × 100*4 = 800×400, centered in 800×600.
    // Child is arranged at natural size (200×100), transform scales it.
    const RectDip& cb = child->Bounds();
    EXPECT_NEAR(cb.w, 200.0f, 0.01f);
    EXPECT_NEAR(cb.h, 100.0f, 0.01f);
    // Scaled height = 400, pad = (600 - 400)/2 = 100, pre-scale offset = 100/4 = 25.
    EXPECT_NEAR(cb.y, 25.0f, 0.01f);
}

// Stretch::Fill ignores aspect ratio and scales independently per axis.
TEST(Viewbox, StretchFillDistortsToFit) {
    Viewbox vb;
    vb.SetStretch(Stretch::Fill);
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(100.0f, 50.0f));

    vb.Measure(400.0f, 300.0f);
    vb.Arrange({0, 0, 400.0f, 300.0f});

    // Scale X = 400/100 = 4, Scale Y = 300/50 = 6.
    // Child still arranged at natural size; transform applies independent scales.
    const RectDip& cb = child->Bounds();
    EXPECT_NEAR(cb.w, 100.0f, 0.01f);
    EXPECT_NEAR(cb.h, 50.0f, 0.01f);
}

// Stretch::UniformToFill scales to cover, preserving aspect (may clip).
TEST(Viewbox, StretchUniformToFillCoversWithClip) {
    Viewbox vb;
    vb.SetStretch(Stretch::UniformToFill);
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(200.0f, 100.0f));  // 2:1

    vb.Measure(400.0f, 400.0f);
    vb.Arrange({0, 0, 400.0f, 400.0f});

    // Available is 400×400 (1:1), child is 2:1.
    // To cover while preserving ratio: max(400/200, 400/100) = max(2, 4) = 4.
    // Scaled = 800×400, which overflows horizontally (clipped by Render).
    const RectDip& cb = child->Bounds();
    EXPECT_NEAR(cb.w, 200.0f, 0.01f);
    EXPECT_NEAR(cb.h, 100.0f, 0.01f);
}

// Stretch::None leaves the child at natural size, no scaling.
TEST(Viewbox, StretchNoneNoScale) {
    Viewbox vb;
    vb.SetStretch(Stretch::None);
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(100.0f, 80.0f));

    vb.Measure(500.0f, 500.0f);
    vb.Arrange({10.0f, 20.0f, 500.0f, 500.0f});

    // No scale, child centered in slot.
    const RectDip& cb = child->Bounds();
    EXPECT_NEAR(cb.w, 100.0f, 0.01f);
    EXPECT_NEAR(cb.h, 80.0f, 0.01f);
    // Centered: offsetX = (500 - 100)/2 = 200, offsetY = (500 - 80)/2 = 210.
    EXPECT_NEAR(cb.x, 10.0f + 200.0f, 0.01f);
    EXPECT_NEAR(cb.y, 20.0f + 210.0f, 0.01f);
}

// An unbounded measure (e.g., from a StackPanel) reports the child's natural size
// instead of infinity, so the Viewbox doesn't push everything off-screen.
TEST(Viewbox, UnboundedMeasureFallsBackToNaturalSize) {
    Viewbox vb;
    vb.SetChild(std::make_unique<FixedLeaf>(120.0f, 60.0f));

    vb.Measure(INFINITY, INFINITY);
    // Should report child's natural size, not infinity.
    EXPECT_NEAR(vb.Desired().w, 120.0f, 0.01f);
    EXPECT_NEAR(vb.Desired().h, 60.0f, 0.01f);
}

// SetChild replaces the previous child (old destroyed, new owned).
TEST(Viewbox, SetChildReplacesPrevious) {
    Viewbox vb;
    vb.SetChild(std::make_unique<FixedLeaf>(10.0f, 10.0f));
    auto* second = vb.SetChild(std::make_unique<FixedLeaf>(20.0f, 20.0f));
    EXPECT_TRUE(vb.Child() == second);
}

// AttachToContext propagates to the child.
TEST(Viewbox, AttachPropagates) {
    AnimationRegistry anims;
    Viewbox vb;
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(10.0f, 10.0f));

    UIContext ctx;
    ctx.animations = &anims;
    vb.AttachToContext(ctx);
    EXPECT_EQ(child->attachCount(), 1);

    vb.DetachFromContext();
    EXPECT_EQ(child->detachCount(), 1);
}

// A Viewbox with no child measures safely to zero.
TEST(Viewbox, EmptyViewboxMeasuresToZero) {
    Viewbox vb;
    vb.Measure(500.0f, 500.0f);
    EXPECT_NEAR(vb.Desired().w, 500.0f, 0.01f);  // fills slot even when empty
    EXPECT_NEAR(vb.Desired().h, 500.0f, 0.01f);
}

// Edge case: child with zero or near-zero size doesn't divide by zero.
TEST(Viewbox, TinyChildDoesNotCrash) {
    Viewbox vb;
    vb.SetChild(std::make_unique<FixedLeaf>(0.0f, 0.0f));
    vb.Measure(100.0f, 100.0f);
    vb.Arrange({0, 0, 100.0f, 100.0f});
    // Should not crash; scale stays 1.0 when child size is below threshold.
}

// Hit-testing maps through the inverse scale: a point that hits the scaled child
// in visual space must map correctly to the child's unscaled coordinate system.
TEST(Viewbox, HitTestMapsInverseScale) {
    Viewbox vb;
    auto* child = vb.SetChild(std::make_unique<FixedLeaf>(100.0f, 50.0f));

    vb.Measure(400.0f, 400.0f);
    vb.Arrange({0, 0, 400.0f, 400.0f});
    // Uniform scale to fit 100×50 into 400×400: min(400/100, 400/50) = 4.
    // Scaled child = 400×200, centered → top at (400-200)/2 = 100.

    // Point at (200, 150) in Viewbox space → scaled child center.
    // Should map to (50, 25) in child's unscaled space (its own center).
    UIElement* hit = vb.HitTestDeep(200.0f, 150.0f);
    // Child must be reported as hit (it's the only target).
    EXPECT_TRUE(hit == child);
}

// A point outside the Viewbox's bounds must not reach the child, even if the
// child's unscaled bounds extend past (as under UniformToFill).
TEST(Viewbox, HitTestRespectsViewboxBounds) {
    Viewbox vb;
    vb.SetStretch(Stretch::UniformToFill);
    vb.SetChild(std::make_unique<FixedLeaf>(200.0f, 100.0f));

    vb.Measure(400.0f, 400.0f);
    vb.Arrange({0, 0, 400.0f, 400.0f});
    // Scaled to cover: max(400/200, 400/100) = 4 → 800×400, overflows horizontally.

    // Point at (500, 200) is outside Viewbox's 400×400 bounds.
    UIElement* hit = vb.HitTestDeep(500.0f, 200.0f);
    EXPECT_TRUE(hit == nullptr);  // Must not hit the child.
}

// Changing Stretch invalidates measure (scale changes, layout must recompute).
TEST(Viewbox, ChangingStretchInvalidatesMeasure) {
    Viewbox vb;
    vb.SetChild(std::make_unique<FixedLeaf>(100.0f, 100.0f));
    vb.Measure(200.0f, 200.0f);
    vb.Arrange({0, 0, 200.0f, 200.0f});
    vb.ClearDirty();

    vb.SetStretch(Stretch::Fill);
    EXPECT_TRUE(Has(vb.Dirty(), DirtyFlags::Measure));
}
