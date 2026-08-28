// CanvasRightBottomTests.cpp — headless tests for Canvas Right/Bottom edge positioning

#include "../framework/Test.h"
#include "../../FluentUI/layout/Canvas.h"
#include "LayoutTestHelpers.h"

using namespace fluent;

// --- Right edge positioning -----------------------------------------------

TEST(Canvas, RightEdgePositionsFromContentRight) {
    // Canvas 500×300. Child 100×50, Right=20 → x = 500 - 20 - 100 = 380.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetRight(ptr, 20.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.x, 380.0f, 0.01f);
    EXPECT_NEAR(b.y, 0.0f, 0.01f);   // Top defaults to 0
    EXPECT_NEAR(b.w, 100.0f, 0.01f);
    EXPECT_NEAR(b.h, 50.0f, 0.01f);
}

TEST(Canvas, RightTakesPrecedenceOverLeft) {
    // Both Right and Left are set; Right wins, Left is ignored.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetLeft(ptr, 50.0f);
    Canvas::SetRight(ptr, 20.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.x, 380.0f, 0.01f);  // Right wins: 500 - 20 - 100
}

// --- Bottom edge positioning ----------------------------------------------

TEST(Canvas, BottomEdgePositionsFromContentBottom) {
    // Canvas 500×300. Child 100×50, Bottom=15 → y = 300 - 15 - 50 = 235.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetBottom(ptr, 15.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.x, 0.0f, 0.01f);     // Left defaults to 0
    EXPECT_NEAR(b.y, 235.0f, 0.01f);
    EXPECT_NEAR(b.w, 100.0f, 0.01f);
    EXPECT_NEAR(b.h, 50.0f, 0.01f);
}

TEST(Canvas, BottomTakesPrecedenceOverTop) {
    // Both Bottom and Top are set; Bottom wins, Top is ignored.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetTop(ptr, 10.0f);
    Canvas::SetBottom(ptr, 15.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.y, 235.0f, 0.01f);  // Bottom wins: 300 - 15 - 50
}

// --- Combined edge positioning --------------------------------------------

TEST(Canvas, RightAndBottomCanCombine) {
    // Right=10, Bottom=10 → bottom-right corner.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetRight(ptr, 10.0f);
    Canvas::SetBottom(ptr, 10.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.x, 390.0f, 0.01f);  // 500 - 10 - 100
    EXPECT_NEAR(b.y, 240.0f, 0.01f);  // 300 - 10 - 50
}

// --- NaN default (unset) --------------------------------------------------

TEST(Canvas, GetRightReturnsNaNWhenUnset) {
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));

    float right = Canvas::GetRight(ptr);
    EXPECT_TRUE(std::isnan(right));
}

TEST(Canvas, GetBottomReturnsNaNWhenUnset) {
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));

    float bottom = Canvas::GetBottom(ptr);
    EXPECT_TRUE(std::isnan(bottom));
}

TEST(Canvas, UnsetRightFallsBackToLeft) {
    // Right not set (NaN) → use Left.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetLeft(ptr, 50.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.x, 50.0f, 0.01f);  // Left is used
}

TEST(Canvas, UnsetBottomFallsBackToTop) {
    // Bottom not set (NaN) → use Top.
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));
    Canvas::SetTop(ptr, 25.0f);

    canvas.Measure(500.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 500.0f, 300.0f});

    const RectDip& b = ptr->Bounds();
    EXPECT_NEAR(b.y, 25.0f, 0.01f);  // Top is used
}
