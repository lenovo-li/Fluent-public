// GroupBoxTests.cpp — headless tests for GroupBox header/box geometry and the
// single-child decorator contract.
//
// The layout GroupBox produces, and which these tests pin:
//
//     y=0     +-- header text ----------------+   <- HeaderRect
//             |                               |
//     y=hh    .   (kHeaderGapDip)             .
//     y=band  +-------------------------------+   <- BoxRect, framed
//             |  inset                        |
//             |     +---- child ----+         |
//             |     |               |         |
//             +-------------------------------+
//
// The header sits ABOVE the box rather than interrupting the top border (WPF's
// look). That is a deliberate choice documented in GroupBox.h: erasing a gap in the
// border needs an opaque colour to erase with, and this framework composites over
// Mica. The consequence a test can see is that BoxRect().y > bounds_.y whenever
// there is a header — asserted below so the decision is not silently reverted.

#include "../framework/Test.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/core/UIContext.h"
#include <memory>

using namespace fluent;

namespace {

// A leaf with a fixed desired size, so a GroupBox's own desired size is a pure
// function of the child size + inset and needs no DWrite.
class FixedLeaf : public FrameworkElement {
public:
    FixedLeaf(float w, float h) : w_(w), h_(h) {}
    void Measure(float, float) override { SetDesired({w_, h_}); }
    void Arrange(const RectDip& r) override { SetBounds(r); }
    void Render(const DrawingContext&) override {}
private:
    float w_, h_;
};

}  // namespace

// --- Ownership ------------------------------------------------------------

TEST(GroupBox, SetChildTakesOwnershipAndParents) {
    GroupBox gb;
    auto child = std::make_unique<FixedLeaf>(50.0f, 30.0f);
    auto* raw = gb.SetChild(std::move(child));

    EXPECT_TRUE(raw != nullptr);
    EXPECT_EQ(gb.Child(), raw);
    EXPECT_EQ(raw->Parent(), &gb);
}

TEST(GroupBox, SetChildReplacesPrevious) {
    GroupBox gb;
    gb.SetChild(std::make_unique<FixedLeaf>(50.0f, 30.0f));
    auto* second = gb.SetChild(std::make_unique<FixedLeaf>(60.0f, 40.0f));

    EXPECT_EQ(gb.Child(), second);
    EXPECT_EQ(second->Parent(), &gb);
}

// --- Header presence changes the geometry ---------------------------------

// No header: the box fills the whole control, exactly like a Border would.
TEST(GroupBox, EmptyHeaderCollapsesTheHeaderBand) {
    GroupBox gb;
    gb.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});

    EXPECT_TRUE(gb.Header().empty());
    const RectDip hr = gb.HeaderRect();
    EXPECT_EQ(hr.w, 0.0f);
    EXPECT_EQ(hr.h, 0.0f);

    const RectDip box = gb.BoxRect();
    EXPECT_EQ(box.y, 0.0f);       // box starts at the top — no band consumed
    EXPECT_EQ(box.h, 100.0f);     // and takes the full height
}

// With a header, the box is pushed down. THIS is the assertion that pins the
// "header above the box" decision — under WPF's interrupted-border look the box
// would still start at bounds_.y.
TEST(GroupBox, HeaderPushesTheBoxDown) {
    GroupBox gb;
    gb.SetHeader(L"Settings");
    gb.Measure(200.0f, 100.0f);   // Measure computes the header height
    gb.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});

    const RectDip hr = gb.HeaderRect();
    EXPECT_EQ(hr.x, 0.0f);
    EXPECT_EQ(hr.y, 0.0f);
    EXPECT_EQ(hr.w, 200.0f);
    EXPECT_TRUE(hr.h > 0.0f);

    const RectDip box = gb.BoxRect();
    EXPECT_TRUE(box.y > 0.0f);              // pushed below the header
    EXPECT_TRUE(box.y >= hr.bottom());      // and clear of the header text
    EXPECT_TRUE(box.h < 100.0f);            // so it is shorter than the control
    // The two regions together account for the whole control height.
    EXPECT_NEAR(box.y + box.h, 100.0f, 0.01f);
}

TEST(GroupBox, HeaderAndBoxFollowBoundsOrigin) {
    GroupBox gb;
    gb.SetHeader(L"Group");
    gb.Measure(300.0f, 150.0f);
    gb.Arrange(RectDip{10.0f, 20.0f, 300.0f, 150.0f});

    EXPECT_EQ(gb.HeaderRect().x, 10.0f);
    EXPECT_EQ(gb.HeaderRect().y, 20.0f);
    EXPECT_EQ(gb.BoxRect().x, 10.0f);
    EXPECT_TRUE(gb.BoxRect().y > 20.0f);
}

// --- Measure composes header + child + inset ------------------------------

TEST(GroupBox, MeasureAddsHeaderBandAndInsetToChildSize) {
    GroupBox gb;
    gb.SetBorderThickness(1.0f);
    gb.SetPadding(Thickness{10.0f, 10.0f, 10.0f, 10.0f});
    gb.SetChild(std::make_unique<FixedLeaf>(50.0f, 30.0f));

    // No header first: desired = child + (border + padding) on each edge.
    gb.Measure(500.0f, 500.0f);
    const SizeDip noHeader = gb.Desired();
    EXPECT_NEAR(noHeader.w, 50.0f + (1.0f + 10.0f) * 2.0f, 0.01f);
    EXPECT_NEAR(noHeader.h, 30.0f + (1.0f + 10.0f) * 2.0f, 0.01f);

    // Adding a header makes it taller by the band, and no wider.
    gb.SetHeader(L"Header");
    gb.Measure(500.0f, 500.0f);
    const SizeDip withHeader = gb.Desired();
    EXPECT_NEAR(withHeader.w, noHeader.w, 0.01f);
    EXPECT_TRUE(withHeader.h > noHeader.h);
}

TEST(GroupBox, ExplicitSizeOverridesMeasuredSize) {
    GroupBox gb;
    gb.SetChild(std::make_unique<FixedLeaf>(50.0f, 30.0f));
    gb.SetWidth(400.0f);
    gb.SetHeight(200.0f);
    gb.Measure(1000.0f, 1000.0f);

    EXPECT_NEAR(gb.Desired().w, 400.0f, 0.01f);
    EXPECT_NEAR(gb.Desired().h, 200.0f, 0.01f);
}

// --- Arrange insets the child inside the box ------------------------------

TEST(GroupBox, ArrangeInsetsChildInsideTheBox) {
    GroupBox gb;
    gb.SetBorderThickness(1.0f);
    gb.SetPadding(Thickness{10.0f, 10.0f, 10.0f, 10.0f});
    gb.SetHeader(L"Header");
    auto* child = gb.SetChild(std::make_unique<FixedLeaf>(50.0f, 30.0f));

    gb.Measure(200.0f, 150.0f);
    gb.Arrange(RectDip{0.0f, 0.0f, 200.0f, 150.0f});

    const RectDip box = gb.BoxRect();
    const RectDip cb = child->Bounds();
    // Child sits inside the box by border + padding on the left/top.
    EXPECT_NEAR(cb.x, box.x + 11.0f, 0.01f);
    EXPECT_NEAR(cb.y, box.y + 11.0f, 0.01f);
    // And never spills past the box on the right/bottom.
    EXPECT_TRUE(cb.right() <= box.right() + 0.01f);
    EXPECT_TRUE(cb.bottom() <= box.bottom() + 0.01f);
}

// --- Hit-testing: the frame is decorative --------------------------------

// A bare GroupBox must be transparent to hit-testing, exactly like Border: it is a
// decorative frame, and swallowing clicks would block whatever sits behind it.
TEST(GroupBox, DecorativeFrameIsTransparentToHitTesting) {
    GroupBox gb;
    gb.SetHeader(L"Header");
    gb.Measure(200.0f, 100.0f);
    gb.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});

    // A point inside the frame but with no child under it hits nothing.
    EXPECT_EQ(gb.HitTestDeep(100.0f, 60.0f), nullptr);
}

TEST(GroupBox, HitTestReachesTheChild) {
    GroupBox gb;
    gb.SetHeader(L"Header");
    auto* child = gb.SetChild(std::make_unique<Button>());
    gb.Measure(200.0f, 150.0f);
    gb.Arrange(RectDip{0.0f, 0.0f, 200.0f, 150.0f});

    const RectDip cb = child->Bounds();
    // Skip when the child got no area (no DWrite to measure the Button's label).
    if (cb.w <= 0.0f || cb.h <= 0.0f) return;
    UIElement* hit = gb.HitTestDeep(cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f);
    EXPECT_EQ(hit, static_cast<UIElement*>(child));
}

// --- Dirty propagation ---------------------------------------------------

TEST(GroupBox, ChildDirtyMakesTheGroupBoxNeedRemeasure) {
    GroupBox gb;
    auto* child = gb.SetChild(std::make_unique<FixedLeaf>(50.0f, 30.0f));
    gb.Measure(200.0f, 100.0f);
    gb.ClearDirtySubtree();
    EXPECT_FALSE(gb.NeedsRemeasure());

    // Invalidate via a property change rather than calling InvalidateDirty, which is
    // protected. SetWidth is public and marks Measure dirty.
    child->SetWidth(60.0f);
    EXPECT_TRUE(gb.NeedsRemeasure());
}
