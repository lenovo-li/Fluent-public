// SetVisibleLayoutTests.cpp - verify SetVisible(false) removes elements from layout.
//
// WPF's Visibility.Collapsed semantic: invisible elements do not participate in
// Measure/Arrange and do not occupy space in their parent's layout. This test
// suite verifies that FluentUI's SetVisible(false) honors that contract across
// all Panel types.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/WrapPanel.h"
#include "../../FluentUI/layout/DockPanel.h"
#include "../../FluentUI/layout/Canvas.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Border.h"

using namespace fluent;

// StackPanel: invisible child does not contribute to stack size or offset siblings.
TEST(SetVisibleLayout, StackPanel_InvisibleChild_NotInLayout) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    sp.SetSpacing(10.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetHeight(30.0f);
    auto* b = sp.Emplace<TestLeaf>();
    b->SetHeight(40.0f);
    b->SetVisible(false);
    auto* c = sp.Emplace<TestLeaf>();
    c->SetHeight(20.0f);

    sp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // a at y=0 h=30; b is invisible so skipped; c at y=30+10=40 (no gap for b).
    EXPECT_NEAR(a->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().h, 30.0f, 0.01f);
    EXPECT_NEAR(c->Bounds().y, 40.0f, 0.01f);
    EXPECT_NEAR(c->Bounds().h, 20.0f, 0.01f);
}

// Grid: invisible cell does not contribute to Auto track sizing.
TEST(SetVisibleLayout, Grid_InvisibleCell_DoesNotAffectTracks) {
    Grid grid;
    grid.SetRows({GridLength::Auto(), GridLength::Auto()});
    grid.SetColumns({GridLength::Auto()});

    auto* tall = grid.Emplace<TestLeaf>();
    tall->SetHeight(100.0f);
    grid.SetCell(tall, 0, 0);
    tall->SetVisible(false);

    // NOT named `small`: <rpcndr.h> has `#define small char`, which turns the
    // declaration into `auto* char = ...` and produces a wall of syntax errors.
    auto* shortCell = grid.Emplace<TestLeaf>();
    shortCell->SetHeight(20.0f);
    grid.SetCell(shortCell, 1, 0);

    grid.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // Row 0 collapses to 0, so row 1 starts at the top. If the invisible cell
    // still measured, row 0 would be 100 and this cell would land at y=100.
    EXPECT_NEAR(shortCell->Bounds().y, 0.0f, 0.01f);
}

// WrapPanel: invisible item takes no slot on its line.
TEST(SetVisibleLayout, WrapPanel_InvisibleItem_NotInLine) {
    WrapPanel wp;
    wp.SetOrientation(WrapPanel::Orientation::Horizontal);

    auto* a = wp.Emplace<TestLeaf>();
    a->SetWidth(50.0f);
    a->SetHeight(30.0f);
    auto* b = wp.Emplace<TestLeaf>();
    b->SetWidth(50.0f);
    b->SetHeight(60.0f);
    b->SetVisible(false);
    auto* c = wp.Emplace<TestLeaf>();
    c->SetWidth(50.0f);
    c->SetHeight(30.0f);

    wp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // c packs directly after a instead of leaving a 50 DIP hole for b, and the
    // line's cross size stays 30 (a and c) rather than 60 (b).
    EXPECT_NEAR(a->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c->Bounds().x, 50.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c->Bounds().y, 0.0f, 0.01f);
}

// DockPanel: invisible docked child does not consume edge space.
TEST(SetVisibleLayout, DockPanel_InvisibleChild_DoesNotConsumeDock) {
    DockPanel dp;

    auto* top = dp.Emplace<TestLeaf>();
    top->SetHeight(40.0f);
    DockPanel::SetDock(top, Dock::Top);
    top->SetVisible(false);

    auto* fill = dp.Emplace<TestLeaf>();

    dp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // With the top strip collapsed, the fill child owns the whole panel. If the
    // invisible strip still docked, fill would be at y=40 with h=260.
    EXPECT_NEAR(fill->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 300.0f, 0.01f);
}

// Canvas: invisible child is skipped by Measure, so it keeps a zero desired size.
TEST(SetVisibleLayout, Canvas_InvisibleChild_NotMeasured) {
    Canvas canvas;

    auto* a = canvas.Emplace<TestLeaf>();
    a->SetWidth(50.0f);
    a->SetHeight(50.0f);
    Canvas::SetLeft(a, 10.0f);
    Canvas::SetTop(a, 10.0f);
    a->SetVisible(false);

    canvas.UpdateLayout({0.0f, 0.0f, 200.0f, 200.0f});

    // Never measured, so desired_ stays at its initial zero rather than 50x50.
    EXPECT_NEAR(a->Desired().w, 0.0f, 0.01f);
    EXPECT_NEAR(a->Desired().h, 0.0f, 0.01f);
}

// ScrollPanel: invisible children do not contribute to content height.
TEST(SetVisibleLayout, ScrollPanel_InvisibleChildren_NotInContentHeight) {
    ScrollPanel sp;
    sp.SetSpacing(10.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetHeight(50.0f);
    auto* b = sp.Emplace<TestLeaf>();
    b->SetHeight(100.0f);
    b->SetVisible(false);
    auto* c = sp.Emplace<TestLeaf>();
    c->SetHeight(50.0f);

    sp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // Stack is 50 + 10 + 50; b's 100 DIP plus its spacing is gone, so c sits at
    // 60 rather than 160.
    EXPECT_NEAR(c->Bounds().y, 60.0f, 0.01f);
}

// Border: invisible child leaves the border measuring only its own padding.
TEST(SetVisibleLayout, Border_InvisibleChild_MeasuresEmpty) {
    Border border;
    border.SetPadding(Thickness(10.0f));

    auto* child = border.SetChild(std::make_unique<TestLeaf>());
    child->SetWidth(50.0f);
    child->SetHeight(50.0f);
    child->SetVisible(false);

    border.Measure(1000.0f, 1000.0f);

    // Padding only (20x20). With the child counted it would be 70x70.
    EXPECT_NEAR(border.Desired().w, 20.0f, 0.01f);
    EXPECT_NEAR(border.Desired().h, 20.0f, 0.01f);
}

// === Render and input behavior ===

namespace {
// A leaf that records whether Render was called, without touching the device.
class RenderSpyLeaf : public FrameworkElement {
public:
    bool rendered = false;
    void Render(const DrawingContext&) override { rendered = true; }
};

class VisibilitySpyLeaf : public FrameworkElement {
public:
    int ancestorChanges = 0;
    bool lastEffectiveVisible = true;
    void Render(const DrawingContext&) override {}
    void OnAncestorVisibilityChanged() override {
        ++ancestorChanges;
        lastEffectiveVisible = IsEffectivelyVisible();
    }
};
} // namespace

// Invisible elements are skipped by RenderWithOpacity (the choke point every
// container uses), so they don't paint even though they still have bounds.
TEST(SetVisibleLayout, InvisibleElement_DoesNotRender) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);

    auto* a = sp.Emplace<RenderSpyLeaf>();
    a->SetHeight(50.0f);
    auto* b = sp.Emplace<RenderSpyLeaf>();
    b->SetHeight(50.0f);

    sp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // Both visible: both get bounds, both render.
    EXPECT_NEAR(a->Bounds().h, 50.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().h, 50.0f, 0.01f);

    DrawingContext dc{nullptr, nullptr, 1.0f, nullptr, nullptr};
    sp.Render(dc);
    EXPECT_TRUE(a->rendered);
    EXPECT_TRUE(b->rendered);

    // Now hide b and render again.
    a->rendered = false;
    b->rendered = false;
    b->SetVisible(false);

    sp.Render(dc);
    EXPECT_TRUE(a->rendered);   // a still visible
    EXPECT_FALSE(b->rendered);  // b skipped by RenderWithOpacity
}

// Invisible elements fail HitTest, so they don't receive pointer input.
TEST(SetVisibleLayout, InvisibleElement_FailsHitTest) {
    StackPanel sp;

    auto* a = sp.Emplace<TestLeaf>();
    a->SetWidth(100.0f);
    a->SetHeight(50.0f);

    sp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // Before: visible and hit-testable at (50, 25).
    EXPECT_TRUE(a->HitTest(50.0f, 25.0f));

    a->SetVisible(false);

    // After: same bounds, but HitTest now fails because visible_ is checked.
    EXPECT_FALSE(a->HitTest(50.0f, 25.0f));
}

TEST(SetVisibleLayout, AncestorVisibilityPropagatesThroughBorder) {
    StackPanel root;
    auto border = std::make_unique<Border>();
    Border* borderRaw = border.get();
    auto* leaf = borderRaw->SetChild(std::make_unique<VisibilitySpyLeaf>());
    root.Add(std::move(border));

    root.SetVisible(false);
    EXPECT_EQ(leaf->ancestorChanges, 1);
    EXPECT_FALSE(leaf->lastEffectiveVisible);

    root.SetVisible(true);
    EXPECT_EQ(leaf->ancestorChanges, 2);
    EXPECT_TRUE(leaf->lastEffectiveVisible);
}

TEST(SetVisibleLayout, BorderHonorsChildVisibilityWhenRendering) {
    Border border;
    auto* child = border.SetChild(std::make_unique<RenderSpyLeaf>());
    child->SetBounds({0, 0, 100, 30});
    child->SetVisible(false);
    DrawingContext dc{nullptr, nullptr, 1.0f, nullptr, nullptr};

    border.Render(dc);
    EXPECT_FALSE(child->rendered);
}
