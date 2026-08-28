// StackPanelTests.cpp — locks the current StackPanel measure/arrange behavior.
//
// These tests capture how StackPanel behaves TODAY (before the tree refactor),
// so later phases have a regression net. They exercise pure layout math only:
// no HWND, no D3D, no rendering.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

// Vertical stack: two fixed-height children pinned to the top stack in order
// with spacing between them.
TEST(StackPanel, Vertical_FixedChildren_StackWithSpacing) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    sp.SetSpacing(10.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetHeight(30.0f);
    a->SetVAlign(VAlign::Top);
    auto* b = sp.Emplace<TestLeaf>();
    b->SetHeight(40.0f);
    b->SetVAlign(VAlign::Top);

    sp.UpdateLayout({0.0f, 0.0f, 200.0f, 300.0f});

    // a at y=0 h=30; b at y=30+10 spacing = 40, h=40.
    EXPECT_NEAR(a->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().h, 30.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().y, 40.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().h, 40.0f, 0.01f);
}

// Vertical stack with a default (Stretch + auto-height) child: it gets its
// natural desired height, NOT the leftover main-axis space. FluentUI has no
// "star" size on FrameworkElement — Width/Height are explicit or kAuto — so
// treating Stretch+auto as a star made EVERY child a star by default. In an
// auto-sized parent that collapsed children to zero height. Match WPF:
// StackPanel children always take their desired size on the main axis; fill
// layouts belong in Grid with a Star track.
TEST(StackPanel, Vertical_StretchChild_TakesLeftover) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    sp.SetSpacing(0.0f);

    auto* fixed = sp.Emplace<TestLeaf>();
    fixed->SetHeight(50.0f);
    fixed->SetVAlign(VAlign::Top);
    // Default VAlign::Stretch + auto height, but with a natural height set via
    // SetHeight so the leaf reports something other than "fill whatever the
    // panel offers" (the bare TestLeaf default mirrors its availability).
    auto* fill = sp.Emplace<TestLeaf>();
    fill->SetHeight(30.0f);

    sp.UpdateLayout({0.0f, 0.0f, 100.0f, 200.0f});

    // fixed=50 at top; fill takes its natural 30 DIP, not the 150 leftover.
    // Without the star semantics the leftover space is simply left empty.
    EXPECT_NEAR(fixed->Bounds().h, 50.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().y, 50.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 30.0f, 0.01f);
}

// Two default children each get their natural height, stacked — they do NOT
// split the leftover space. See above for rationale.
TEST(StackPanel, Vertical_TwoStretch_SplitEqually) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    sp.SetSpacing(0.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetHeight(30.0f);
    auto* b = sp.Emplace<TestLeaf>();
    b->SetHeight(40.0f);

    sp.UpdateLayout({0.0f, 0.0f, 100.0f, 200.0f});

    // Each child keeps its natural height; no 100/100 split.
    EXPECT_NEAR(a->Bounds().h, 30.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().h, 40.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().y, 30.0f, 0.01f);
}

// Horizontal stack: children flow left-to-right; cross axis (height) fills.
TEST(StackPanel, Horizontal_FixedWidths_FlowRight) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Horizontal);
    sp.SetSpacing(16.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetWidth(60.0f);
    a->SetHAlign(HAlign::Left);
    auto* b = sp.Emplace<TestLeaf>();
    b->SetWidth(80.0f);
    b->SetHAlign(HAlign::Left);

    sp.UpdateLayout({0.0f, 0.0f, 400.0f, 40.0f});

    EXPECT_NEAR(a->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().w, 60.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 76.0f, 0.01f);  // 60 + 16 spacing
    EXPECT_NEAR(b->Bounds().w, 80.0f, 0.01f);
}

// Margin is honored: a child's margin offsets its position and shrinks its box.
TEST(StackPanel, Vertical_Margin_OffsetsChild) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    sp.SetSpacing(0.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetHeight(30.0f);
    a->SetVAlign(VAlign::Top);
    a->SetMargin(Thickness(8.0f, 4.0f, 8.0f, 4.0f));

    sp.UpdateLayout({0.0f, 0.0f, 100.0f, 200.0f});

    // x offset by left margin; stretch horizontally within (100 - 16) = 84.
    EXPECT_NEAR(a->Bounds().x, 8.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().y, 4.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().w, 84.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().h, 30.0f, 0.01f);
}

// Desired size of a vertical stack = sum of fixed heights + spacing (main),
// max child width (cross).
TEST(StackPanel, Vertical_DesiredSize) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    sp.SetSpacing(10.0f);

    auto* a = sp.Emplace<TestLeaf>();
    a->SetHeight(30.0f); a->SetWidth(50.0f);
    a->SetVAlign(VAlign::Top); a->SetHAlign(HAlign::Left);
    auto* b = sp.Emplace<TestLeaf>();
    b->SetHeight(40.0f); b->SetWidth(70.0f);
    b->SetVAlign(VAlign::Top); b->SetHAlign(HAlign::Left);

    sp.Measure(200.0f, 300.0f);

    EXPECT_NEAR(sp.Desired().h, 80.0f, 0.01f);  // 30 + 40 + 10 spacing
    EXPECT_NEAR(sp.Desired().w, 70.0f, 0.01f);  // max(50, 70)
}
