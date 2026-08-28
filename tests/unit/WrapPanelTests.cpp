// WrapPanelTests.cpp

#include "../../FluentUI/layout/WrapPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../framework/Test.h"
#include <limits>

using namespace fluent;

TEST(WrapPanel, SingleRowNoWrap) {
    // Three children narrow enough to fit in one line.
    WrapPanel wp;
    auto* c1 = wp.Emplace<Border>();
    auto* c2 = wp.Emplace<Border>();
    auto* c3 = wp.Emplace<Border>();
    c1->SetWidth(50.0f); c1->SetHeight(20.0f);
    c2->SetWidth(60.0f); c2->SetHeight(25.0f);
    c3->SetWidth(40.0f); c3->SetHeight(15.0f);

    wp.Measure(200.0f, 100.0f);
    wp.Arrange({0, 0, 200, 100});

    // All in one row, heights differ, panel height is tallest child.
    EXPECT_NEAR(c1->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().x, 50.0f, 0.01f);
    EXPECT_NEAR(c3->Bounds().x, 110.0f, 0.01f);
    EXPECT_NEAR(c1->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c3->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(wp.Desired().h, 25.0f, 0.01f);  // tallest child
}

TEST(WrapPanel, TwoRowsWrap) {
    // Horizontal WrapPanel, available 120 DIP. Three children: 50, 80, 70.
    // First row: 50 + 80 = 130 > 120 → wrap → first row is just 50.
    // Second row: 80.
    // Third row: 70.
    WrapPanel wp;
    auto* c1 = wp.Emplace<Border>();
    auto* c2 = wp.Emplace<Border>();
    auto* c3 = wp.Emplace<Border>();
    c1->SetWidth(50.0f); c1->SetHeight(20.0f);
    c2->SetWidth(80.0f); c2->SetHeight(30.0f);
    c3->SetWidth(70.0f); c3->SetHeight(25.0f);

    wp.Measure(120.0f, 200.0f);
    wp.Arrange({0, 0, 120, 200});

    EXPECT_NEAR(c1->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c1->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().y, 20.0f, 0.01f);  // below c1's line
    EXPECT_NEAR(c3->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c3->Bounds().y, 50.0f, 0.01f);  // below c2's line
}

TEST(WrapPanel, ItemWidthUniform) {
    // ItemWidth = 80 makes all children 80 DIP wide regardless of content.
    WrapPanel wp;
    wp.SetItemWidth(80.0f);
    auto* c1 = wp.Emplace<Border>();
    auto* c2 = wp.Emplace<Border>();
    c1->SetWidth(30.0f); c1->SetHeight(20.0f);  // natural 30, forced to 80
    c2->SetWidth(50.0f); c2->SetHeight(25.0f);  // natural 50, forced to 80

    wp.Measure(200.0f, 100.0f);
    wp.Arrange({0, 0, 200, 100});

    EXPECT_NEAR(c1->Bounds().w, 80.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().w, 80.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().x, 80.0f, 0.01f);  // starts after c1's 80 DIP slot
}

TEST(WrapPanel, VerticalOrientation) {
    // Vertical WrapPanel wraps into columns instead of rows.
    WrapPanel wp;
    wp.SetOrientation(WrapPanel::Orientation::Vertical);
    auto* c1 = wp.Emplace<Border>();
    auto* c2 = wp.Emplace<Border>();
    auto* c3 = wp.Emplace<Border>();
    c1->SetWidth(20.0f); c1->SetHeight(50.0f);
    c2->SetWidth(25.0f); c2->SetHeight(80.0f);
    c3->SetWidth(30.0f); c3->SetHeight(40.0f);

    wp.Measure(200.0f, 120.0f);  // available height 120
    wp.Arrange({0, 0, 200, 120});

    // c1 and c2 total 130 > 120 → wrap → c1 alone in first column.
    EXPECT_NEAR(c1->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c1->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().x, 20.0f, 0.01f);  // widest in first column is c1(20)
    EXPECT_NEAR(c2->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c3->Bounds().x, 20.0f, 0.01f);  // same column as c2
    EXPECT_NEAR(c3->Bounds().y, 80.0f, 0.01f);
}

TEST(WrapPanel, OversizedChildGetsOwnLine) {
    // A child wider than the available space gets a line to itself rather than
    // being dropped — matching WPF, where an oversized item overflows.
    WrapPanel wp;
    auto* c1 = wp.Emplace<Border>();
    auto* c2 = wp.Emplace<Border>();
    c1->SetWidth(150.0f); c1->SetHeight(20.0f);  // wider than available 100
    c2->SetWidth(50.0f); c2->SetHeight(25.0f);

    wp.Measure(100.0f, 200.0f);
    wp.Arrange({0, 0, 100, 200});

    // c1 alone on first line (overflows), c2 on second line.
    EXPECT_NEAR(c1->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c1->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c1->Bounds().w, 150.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().y, 20.0f, 0.01f);
}

TEST(WrapPanel, UnboundedDoesNotWrap) {
    // When available main extent is infinite, no wrapping occurs.
    WrapPanel wp;
    auto* c1 = wp.Emplace<Border>();
    auto* c2 = wp.Emplace<Border>();
    auto* c3 = wp.Emplace<Border>();
    c1->SetWidth(50.0f); c1->SetHeight(20.0f);
    c2->SetWidth(80.0f); c2->SetHeight(30.0f);
    c3->SetWidth(70.0f); c3->SetHeight(25.0f);

    wp.Measure(std::numeric_limits<float>::infinity(), 100.0f);
    wp.Arrange({0, 0, 10000, 100});

    // All in one row.
    EXPECT_NEAR(c1->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c3->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(c2->Bounds().x, 50.0f, 0.01f);
    EXPECT_NEAR(c3->Bounds().x, 130.0f, 0.01f);
}

