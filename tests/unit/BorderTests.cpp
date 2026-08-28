// BorderTests.cpp — unit tests for the Border single-child decorator (WP-02
// Step 3). Pure logic: no HWND / D2D / DWrite. Verifies the layout contract
// (measure adds inset, arrange insets the child), the single-child ownership +
// replacement, and that attach/detach propagates to the child.

#include "../framework/Test.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "LayoutTestHelpers.h"

using namespace fluent;

namespace {
// A leaf that reports a fixed desired size and records attach/detach.
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

// Measure: desired size = child desired + border thickness + padding on each edge.
TEST(Border, MeasureAddsBorderAndPadding) {
    Border b;
    b.SetBorderThickness(2.0f);
    b.SetPadding(Thickness(10.0f, 5.0f));  // h=10 each side, v=5 each side
    auto* child = b.SetChild(std::make_unique<FixedLeaf>(100.0f, 40.0f));
    (void)child;

    b.Measure(1000.0f, 1000.0f);
    // inset = thickness(2) + padding: horizontal = 2*2 + 10*2 = 24; vertical = 2*2 + 5*2 = 14
    EXPECT_NEAR(b.Desired().w, 100.0f + 24.0f, 0.01f);
    EXPECT_NEAR(b.Desired().h, 40.0f + 14.0f, 0.01f);
}

// Arrange: the child is placed inside the interior (border box minus inset).
TEST(Border, ArrangeInsetsChild) {
    Border b;
    b.SetBorderThickness(1.0f);
    b.SetPadding(Thickness(8.0f));
    auto* child = b.SetChild(std::make_unique<FixedLeaf>(50.0f, 50.0f));

    b.Measure(1000.0f, 1000.0f);
    b.Arrange({100.0f, 200.0f, 300.0f, 150.0f});
    // inset per edge = 1 + 8 = 9
    const RectDip& cb = child->Bounds();
    EXPECT_NEAR(cb.x, 100.0f + 9.0f, 0.01f);
    EXPECT_NEAR(cb.y, 200.0f + 9.0f, 0.01f);
    EXPECT_NEAR(cb.w, 300.0f - 18.0f, 0.01f);
    EXPECT_NEAR(cb.h, 150.0f - 18.0f, 0.01f);
}

// A Border with no child measures to just its inset (no crash, no child).
TEST(Border, EmptyBorderMeasuresToInset) {
    Border b;
    b.SetBorderThickness(3.0f);
    b.SetPadding(Thickness(4.0f));
    b.Measure(500.0f, 500.0f);
    EXPECT_NEAR(b.Desired().w, (3.0f + 4.0f) * 2.0f, 0.01f);
    EXPECT_NEAR(b.Desired().h, (3.0f + 4.0f) * 2.0f, 0.01f);
}

// SetChild replaces the previous child (old destroyed, new owned).
TEST(Border, SetChildReplacesPrevious) {
    Border b;
    b.SetChild(std::make_unique<FixedLeaf>(10.0f, 10.0f));
    auto* second = b.SetChild(std::make_unique<FixedLeaf>(20.0f, 20.0f));
    EXPECT_TRUE(b.Child() == second);
    b.Measure(1000.0f, 1000.0f);
    EXPECT_NEAR(b.Desired().w, 20.0f, 0.01f);  // second child's size
}

// Attaching the Border attaches its child; detaching detaches it.
TEST(Border, AttachPropagatesToChild) {
    AnimationRegistry anims;
    Border b;
    auto* child = b.SetChild(std::make_unique<FixedLeaf>(10.0f, 10.0f));

    UIContext ctx;
    ctx.animations = &anims;
    b.AttachToContext(ctx);
    EXPECT_TRUE(b.IsAttached());
    EXPECT_TRUE(child->IsAttached());
    EXPECT_EQ(child->attachCount(), 1);

    b.DetachFromContext();
    EXPECT_FALSE(child->IsAttached());
    EXPECT_EQ(child->detachCount(), 1);
}

// A child set into an already-attached Border is attached immediately.
TEST(Border, LateChildAttachesImmediately) {
    AnimationRegistry anims;
    Border b;
    UIContext ctx;
    ctx.animations = &anims;
    b.AttachToContext(ctx);

    auto* child = b.SetChild(std::make_unique<FixedLeaf>(10.0f, 10.0f));
    EXPECT_TRUE(child->IsAttached());
    EXPECT_EQ(child->attachCount(), 1);
}

// HitTestDeep: Border with no context menu stays transparent (child hit or null).
TEST(Border, HitTestDeep_NoContextMenu_Transparent) {
    Border b;
    b.SetBorderThickness(0.0f);
    b.SetPadding(Thickness{0});
    auto* child = b.SetChild(std::make_unique<FixedLeaf>(50.0f, 50.0f));
    b.Measure(100.0f, 100.0f);
    b.Arrange({0, 0, 50, 50});
    child->Arrange({0, 0, 50, 50});  // child arranged by Border, but explicitly for test

    // Point inside child bounds → child hit.
    EXPECT_EQ(b.HitTestDeep(25, 25), child);
    // Point outside child → null (Border is decorative, not a target).
    EXPECT_EQ(b.HitTestDeep(75, 75), nullptr);
}

// HitTestDeep: Border WITH context menu becomes a fallback hit target.
TEST(Border, HitTestDeep_WithContextMenu_BecomesTarget) {
    Border b;
    b.SetBorderThickness(0.0f);
    b.SetPadding(Thickness{10});  // 10 DIP padding on all sides
    auto* child = b.SetChild(std::make_unique<FixedLeaf>(50.0f, 50.0f));
    b.Measure(100.0f, 100.0f);
    b.Arrange({0, 0, 100, 100});  // Border is 100×100, child slot is 80×80 (inset by padding)

    // Dummy context menu (real MenuFlyout would need a window; raw pointer is enough here).
    MenuFlyout menu;
    b.SetContextMenuRef(&menu);

    // Point in padding area (top-left corner at 5,5) → Border itself.
    EXPECT_EQ(b.HitTestDeep(5, 5), &b);
    // Point inside child area → child (child is at 10,10 with size 80×80 due to stretch).
    EXPECT_EQ(b.HitTestDeep(50, 50), child);
    // Point outside bounds → null.
    EXPECT_EQ(b.HitTestDeep(150, 150), nullptr);
}
