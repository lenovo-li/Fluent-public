// SetPropertyTests.cpp — unit tests for the unified SetProperty helper and the
// per-property invalidation mapping it drives (roadmap §7.1 / §7.2).
//
// SetProperty is protected on Element; a small test peer exposes it and the
// dirty state so the tests can assert both the change-detection contract and
// that each flag lands where §7.2 says it should. Pure logic: no window/GPU.

#include "../framework/Test.h"
#include "../../FluentUI/core/FrameworkElement.h"
#include "LayoutTestHelpers.h"

using namespace fluent;

namespace {

// Exposes SetProperty + a couple of storage fields for direct testing, and the
// accumulated dirty flags (public Dirty() already exists on Element).
class PropPeer : public FrameworkElement {
public:
    void Render(const DrawingContext&) override {}

    bool SetInt(int v, DirtyFlags f) { return SetProperty(intProp_, v, f); }
    bool SetFlt(float v, DirtyFlags f) { return SetProperty(floatProp_, v, f); }
    int Int() const { return intProp_; }

private:
    int intProp_ = 0;
    float floatProp_ = 0.0f;
};

}  // namespace

// A change returns true, stores the value, and records the requested flags
// (expanded to their closure: Measure implies Arrange + Render).
TEST(SetProperty, ChangeStoresValueAndInvalidatesClosure) {
    PropPeer p;
    p.ClearDirty();
    bool changed = p.SetInt(5, DirtyFlags::Measure);
    EXPECT_TRUE(changed);
    EXPECT_EQ(p.Int(), 5);
    EXPECT_TRUE(Has(p.Dirty(), DirtyFlags::Measure));
    EXPECT_TRUE(Has(p.Dirty(), DirtyFlags::Arrange));  // closure
    EXPECT_TRUE(Has(p.Dirty(), DirtyFlags::Render));   // closure
}

// Setting the same value is a no-op: returns false and schedules NO frame
// (the whole point of §7.1 — a redundant set never dirties anything).
TEST(SetProperty, RedundantSetIsNoOp) {
    PropPeer p;
    p.SetInt(7, DirtyFlags::Measure);
    p.ClearDirty();  // clear the dirty from the first (real) set

    bool changed = p.SetInt(7, DirtyFlags::Measure);  // same value
    EXPECT_FALSE(changed);
    EXPECT_FALSE(Has(p.Dirty(), DirtyFlags::Measure));
    EXPECT_FALSE(Has(p.Dirty(), DirtyFlags::Render));
}

// A Render-only property does NOT dirty Measure or Arrange (§7.2: a color/pixel
// change must not trigger layout).
TEST(SetProperty, RenderOnlyDoesNotDirtyLayout) {
    PropPeer p;
    p.ClearDirty();
    p.SetFlt(1.0f, DirtyFlags::Render);
    EXPECT_TRUE(Has(p.Dirty(), DirtyFlags::Render));
    EXPECT_FALSE(Has(p.Dirty(), DirtyFlags::Measure));
    EXPECT_FALSE(Has(p.Dirty(), DirtyFlags::Arrange));
}

// The real base-class setters use SetProperty: changing Width dirties Measure
// (and its closure); re-setting the same Width is a no-op.
TEST(SetProperty, WidthSetterDirtiesMeasureThenShortCircuits) {
    TestLeaf leaf;
    leaf.ClearDirty();
    leaf.SetWidth(120.0f);
    EXPECT_TRUE(Has(leaf.Dirty(), DirtyFlags::Measure));

    leaf.ClearDirty();
    leaf.SetWidth(120.0f);  // unchanged
    EXPECT_FALSE(Has(leaf.Dirty(), DirtyFlags::Measure));
}

// Alignment is Measure in this codebase (it affects the panel's arrangement math
// via the measured slot); verify SetHAlign short-circuits on an unchanged value.
TEST(SetProperty, HAlignShortCircuitsOnUnchanged) {
    TestLeaf leaf;
    leaf.SetHAlign(HAlign::Center);
    leaf.ClearDirty();
    leaf.SetHAlign(HAlign::Center);  // unchanged
    EXPECT_FALSE(Has(leaf.Dirty(), DirtyFlags::Measure));
    leaf.SetHAlign(HAlign::Left);    // changed
    EXPECT_TRUE(Has(leaf.Dirty(), DirtyFlags::Measure));
}
