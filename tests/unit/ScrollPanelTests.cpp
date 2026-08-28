// ScrollPanelTests.cpp
#include "../framework/Test.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include <cmath>

using namespace fluent;

TEST(ScrollPanel, EmptyPanel) {
    ScrollPanel panel;
    panel.Measure(400.0f, 300.0f);
    EXPECT_EQ(panel.Desired().h, 0.0f);
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);
}

TEST(ScrollPanel, SingleChild) {
    ScrollPanel panel;
    auto* btn = panel.Emplace<Button>();
    btn->SetText(L"Test");
    btn->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    panel.Arrange({0, 0, 400.0f, 300.0f});  // Required for viewport bounds
    EXPECT_EQ(panel.Desired().h, 40.0f);
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);  // content fits viewport
}

TEST(ScrollPanel, MultipleChildren_NoSpacing) {
    ScrollPanel panel;
    for (int i = 0; i < 5; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetText(L"Button " + std::to_wstring(i));
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange({0, 0, 400.0f, 300.0f});  // Required for viewport bounds
    EXPECT_EQ(panel.Desired().h, 200.0f);  // 5 * 40
    EXPECT_EQ(panel.MaxVerticalOffset(), 0.0f);  // fits in 300
}

TEST(ScrollPanel, MultipleChildren_WithSpacing) {
    ScrollPanel panel;
    panel.SetSpacing(8.0f);
    for (int i = 0; i < 5; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    // 5 children * 40 + 4 gaps * 8 = 200 + 32 = 232
    EXPECT_EQ(panel.Desired().h, 232.0f);
}

TEST(ScrollPanel, ContentTallerThanViewport) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Content height = 10 * 40 = 400, viewport = 300, max offset = 100
    EXPECT_EQ(panel.MaxVerticalOffset(), 100.0f);
}

TEST(ScrollPanel, ScrollOffset_Clamping) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Try to scroll past the end.
    panel.SetVerticalOffset(200.0f);
    EXPECT_EQ(panel.VerticalOffset(), 100.0f);  // clamped to max

    // Try to scroll before the start.
    panel.SetVerticalOffset(-50.0f);
    EXPECT_EQ(panel.VerticalOffset(), 0.0f);
}

TEST(ScrollPanel, KeyboardNavigation_UpDown) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Down key starts a smooth scroll: it retargets rather than moving the offset
    // outright, so the animation has to be ticked before the offset changes.
    KeyEventArgs down;
    down.vk = VK_DOWN;
    panel.OnKeyDownRouted(down);
    EXPECT_TRUE(down.handled);
    EXPECT_TRUE(panel.WantsAnimationTick());
    for (int i = 0; i < 60 && panel.WantsAnimationTick(); ++i)
        panel.OnAnimationTick(0.016f);
    EXPECT_TRUE(panel.VerticalOffset() > 0.0f);

    // Home key scrolls to top.
    KeyEventArgs home;
    home.vk = VK_HOME;
    panel.OnKeyDownRouted(home);
    EXPECT_TRUE(home.handled);
    EXPECT_EQ(panel.VerticalOffset(), 0.0f);

    // End key scrolls to bottom.
    KeyEventArgs end;
    end.vk = VK_END;
    panel.OnKeyDownRouted(end);
    EXPECT_TRUE(end.handled);
    EXPECT_EQ(panel.VerticalOffset(), panel.MaxVerticalOffset());
}

TEST(ScrollPanel, KeyboardNavigation_PageUpDown) {
    ScrollPanel panel;
    for (int i = 0; i < 30; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // PageDown scrolls roughly one viewport height.
    KeyEventArgs pgdn;
    pgdn.vk = VK_NEXT;
    panel.OnKeyDownRouted(pgdn);
    EXPECT_TRUE(pgdn.handled);
    for (int i = 0; i < 60 && panel.WantsAnimationTick(); ++i)
        panel.OnAnimationTick(0.016f);
    float afterPageDown = panel.VerticalOffset();
    EXPECT_TRUE(afterPageDown > 200.0f && afterPageDown < 300.0f);

    // PageUp scrolls back.
    KeyEventArgs pgup;
    pgup.vk = VK_PRIOR;
    panel.OnKeyDownRouted(pgup);
    EXPECT_TRUE(pgup.handled);
    for (int i = 0; i < 60 && panel.WantsAnimationTick(); ++i)
        panel.OnAnimationTick(0.016f);
    EXPECT_TRUE(panel.VerticalOffset() < 50.0f);
}

TEST(ScrollPanel, WheelScroll) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Wheel down (negative delta) scrolls content down.
    PointerEventArgs wheelDown;
    wheelDown.wheelDelta = -120;
    panel.OnPointerWheelChanged(wheelDown);
    EXPECT_TRUE(wheelDown.handled);
    // Target offset should be positive (scroll_.AnimateBy sets target, Tick moves toward it).
    // We can't verify the immediate offset without ticking, but we can check it didn't crash.
}

TEST(ScrollPanel, ChildArrangement) {
    ScrollPanel panel;
    panel.SetSpacing(10.0f);

    auto* btn1 = panel.Emplace<Button>();
    btn1->SetHeight(50.0f);
    auto* btn2 = panel.Emplace<Button>();
    btn2->SetHeight(60.0f);
    auto* btn3 = panel.Emplace<Button>();
    btn3->SetHeight(70.0f);

    panel.Measure(400.0f, 500.0f);
    panel.Arrange(RectDip{100, 200, 400, 500});

    // Children should be arranged vertically with spacing:
    // btn1: y = 200, h = 50
    // btn2: y = 260 (200 + 50 + 10), h = 60
    // btn3: y = 330 (260 + 60 + 10), h = 70
    EXPECT_EQ(btn1->Bounds().y, 200.0f);
    EXPECT_EQ(btn1->Bounds().h, 50.0f);
    EXPECT_EQ(btn2->Bounds().y, 260.0f);
    EXPECT_EQ(btn2->Bounds().h, 60.0f);
    EXPECT_EQ(btn3->Bounds().y, 330.0f);
    EXPECT_EQ(btn3->Bounds().h, 70.0f);
}

TEST(ScrollPanel, NeedsAnimationTick_WhenScrolling) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        panel.Emplace<Button>()->SetHeight(40.0f);
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    // Initially no tick needed.
    EXPECT_FALSE(panel.WantsAnimationTick());

    // After starting a scroll, tick is needed.
    PointerEventArgs wheel;
    wheel.wheelDelta = -120;
    panel.OnPointerWheelChanged(wheel);
    EXPECT_TRUE(panel.WantsAnimationTick());
}

TEST(ScrollPanel, MeasureWithUnboundedHeight) {
    ScrollPanel panel;
    for (int i = 0; i < 5; ++i) {
        panel.Emplace<Button>()->SetHeight(40.0f);
    }

    // Measure with unbounded height (parent doesn't constrain).
    panel.Measure(400.0f, std::numeric_limits<float>::infinity());

    // Should report full content height.
    EXPECT_EQ(panel.Desired().h, 200.0f);
}

TEST(ScrollPanel, ResizeShrinks_OffsetClamped) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        panel.Emplace<Button>()->SetHeight(40.0f);
    }

    // Initial: 800 content in 300 viewport, max offset = 500.
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    panel.SetVerticalOffset(500.0f);
    EXPECT_EQ(panel.VerticalOffset(), 500.0f);

    // Resize viewport to 700 (content still 800), max offset now = 100.
    panel.Arrange(RectDip{0, 0, 400, 700});
    EXPECT_EQ(panel.VerticalOffset(), 100.0f);  // clamped
}

TEST(ScrollPanel, SpacingChange_InvalidatesMeasure) {
    ScrollPanel panel;
    panel.Emplace<Button>()->SetHeight(40.0f);
    panel.Emplace<Button>()->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    EXPECT_EQ(panel.Desired().h, 80.0f);

    panel.SetSpacing(10.0f);
    panel.Measure(400.0f, 300.0f);
    EXPECT_EQ(panel.Desired().h, 90.0f);  // 40 + 10 + 40
}

TEST(ScrollPanel, EmptyPanel_KeyboardDoesNothing) {
    ScrollPanel panel;
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});

    KeyEventArgs down;
    down.vk = VK_DOWN;
    panel.OnKeyDownRouted(down);
    EXPECT_FALSE(down.handled);  // nothing to scroll
}

// Regression: the scroll offset must live in the children's BOUNDS, not in a
// render-time transform. A D2D transform moves only what D2D draws; a
// compositor-backed child (TextArea, TreeView) is positioned from its bounds by a
// DComp visual and would stay nailed in place while everything else scrolled —
// which is exactly what the demo showed. Asserting on bounds also covers
// hit-testing and dirty-rect reporting, since both read the same bounds.
TEST(ScrollPanel, ScrollOffsetMovesChildBounds) {
    ScrollPanel panel;
    Button* first = nullptr;
    Button* second = nullptr;
    for (int i = 0; i < 10; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
        if (i == 0) first = btn;
        if (i == 1) second = btn;
    }

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 100, 400, 300});  // panel at y=100

    // Unscrolled: children stack from the panel's top edge.
    EXPECT_EQ(first->Bounds().y, 100.0f);
    EXPECT_EQ(second->Bounds().y, 140.0f);

    // Scrolling down by 100 must shift every child UP by 100 in its own bounds.
    panel.SetVerticalOffset(100.0f);
    panel.Arrange(RectDip{0, 100, 400, 300});
    EXPECT_EQ(panel.VerticalOffset(), 100.0f);
    EXPECT_EQ(first->Bounds().y, 0.0f);
    EXPECT_EQ(second->Bounds().y, 40.0f);

    // And the pointer position that now sits over `second` must hit it, with no
    // separate compensation step: bounds are already screen-space.
    UIElement* hit = panel.HitTestDeep(200.0f, 150.0f);
    EXPECT_NE(hit, nullptr);
}

// Moving the offset must request an Arrange. Without it the children keep the
// bounds they were arranged at, so a repaint-only invalidation redraws everything
// exactly where it already was and the view appears frozen.
TEST(ScrollPanel, OffsetChangeRequestsArrange) {
    ScrollPanel panel;
    for (int i = 0; i < 10; ++i)
        panel.Emplace<Button>()->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    panel.ClearDirtySubtree();
    EXPECT_FALSE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));

    panel.SetVerticalOffset(80.0f);
    EXPECT_TRUE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));
}

// Regression: AnimateBy sets TARGET, not offset. SyncScrollArrange compared the
// live offset against arrangedOffset_, which hadn't moved yet on the frame the
// wheel notch arrived, so it returned early and never invalidated. The scrollbar
// still painted its new position (it reads offset each frame), producing "only the
// rail scrolls, the content is frozen" until a resize forced OnLayout. The fix:
// compare TargetOffset() instead, which equals target_ when animating.
TEST(ScrollPanel, WheelScrollInvalidatesArrangeImmediately) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i)
        panel.Emplace<Button>()->SetHeight(40.0f);

    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    panel.ClearDirtySubtree();
    EXPECT_FALSE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));

    // Wheel event with negative delta (scroll down)
    PointerEventArgs wheelEvent;
    wheelEvent.wheelDelta = -120;
    wheelEvent.position = {200.0f, 150.0f};
    panel.OnPointerWheelChanged(wheelEvent);

    // Must mark Arrange dirty immediately, even though the tween target has moved
    // but the live offset has not yet advanced (that happens on the first tick).
    EXPECT_TRUE(panel.AnyDirtyInSubtree(DirtyFlags::Arrange));
    EXPECT_TRUE(wheelEvent.handled);
}

// Regression: scrollbar hover strip (16 DIP) must start drag, not only the
// visible thumb (3 DIP idle / 7 DIP hovered). Before fix, the rail would light
// up but clicks would miss unless the pointer landed inside the narrow thumb.
TEST(ScrollPanel, Scrollbar_DragStartsInHoverStrip) {
    ScrollPanel panel;
    for (int i = 0; i < 20; ++i) {
        auto* btn = panel.Emplace<Button>();
        btn->SetHeight(40.0f);
    }
    panel.Measure(400.0f, 300.0f);
    panel.Arrange(RectDip{0, 0, 400, 300});
    // Content overflows, scrollbar is present.
    EXPECT_TRUE(panel.MaxVerticalOffset() > 0.0f);

    // Simulate pointer pressed in the hover strip (x=390 is in the rightmost
    // 16 DIP). This must start a drag even though the visible thumb is only
    // 2-7 DIP wide.
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position = {390.0f, 150.0f};
    panel.OnPointerPressed(e);
    EXPECT_TRUE(e.handled);  // drag started
}
