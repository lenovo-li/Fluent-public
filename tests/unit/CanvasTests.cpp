#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/Canvas.h"

using namespace fluent;

TEST(Canvas, MeasureReturnsZeroWhenNoSizeSet) {
    Canvas canvas;
    canvas.Measure(800.0f, 600.0f);
    EXPECT_NEAR(canvas.Desired().w, 0.0f, 0.01f);
    EXPECT_NEAR(canvas.Desired().h, 0.0f, 0.01f);
}

TEST(Canvas, ChildMeasuredWithInfiniteConstraint) {
    Canvas canvas;
    auto* leaf = canvas.Emplace<TestLeaf>(50.0f, 30.0f);

    canvas.Measure(800.0f, 600.0f);

    // Child measured with inf constraint, reports its explicit size.
    EXPECT_NEAR(leaf->Desired().w, 50.0f, 0.01f);
    EXPECT_NEAR(leaf->Desired().h, 30.0f, 0.01f);
}

TEST(Canvas, ArrangeAtAttachedPosition) {
    Canvas canvas;
    auto* leaf = canvas.Emplace<TestLeaf>(50.0f, 30.0f);

    Canvas::SetLeft(leaf, 100.0f);
    Canvas::SetTop(leaf, 200.0f);

    canvas.UpdateLayout({0.0f, 0.0f, 800.0f, 600.0f});

    // Child arranged at (100, 200).
    EXPECT_NEAR(leaf->Bounds().x, 100.0f, 0.01f);
    EXPECT_NEAR(leaf->Bounds().y, 200.0f, 0.01f);
    EXPECT_NEAR(leaf->Bounds().w, 50.0f, 0.01f);
    EXPECT_NEAR(leaf->Bounds().h, 30.0f, 0.01f);
}

TEST(Canvas, DefaultPositionIsZero) {
    TestLeaf leaf;
    EXPECT_NEAR(Canvas::GetLeft(&leaf), 0.0f, 0.01f);
    EXPECT_NEAR(Canvas::GetTop(&leaf), 0.0f, 0.01f);
}

TEST(Canvas, SetPositionPersistsAcrossInstances) {
    TestLeaf leaf;
    Canvas::SetLeft(&leaf, 100.0f);
    Canvas::SetTop(&leaf, 200.0f);

    // Property is static — creating another Canvas doesn't clear it.
    Canvas canvas1, canvas2;
    EXPECT_NEAR(Canvas::GetLeft(&leaf), 100.0f, 0.01f);
    EXPECT_NEAR(Canvas::GetTop(&leaf), 200.0f, 0.01f);
}

TEST(Canvas, MultipleChildren) {
    Canvas canvas;
    auto* a = canvas.Emplace<TestLeaf>(20.0f, 10.0f);
    auto* b = canvas.Emplace<TestLeaf>(30.0f, 15.0f);
    auto* c = canvas.Emplace<TestLeaf>(40.0f, 25.0f);

    Canvas::SetLeft(a, 10.0f);
    Canvas::SetTop(a, 20.0f);
    Canvas::SetLeft(b, 50.0f);
    Canvas::SetTop(b, 60.0f);
    Canvas::SetLeft(c, 100.0f);
    Canvas::SetTop(c, 150.0f);

    canvas.UpdateLayout({0.0f, 0.0f, 800.0f, 600.0f});

    EXPECT_NEAR(a->Bounds().x, 10.0f, 0.01f);
    EXPECT_NEAR(a->Bounds().y, 20.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 50.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().y, 60.0f, 0.01f);
    EXPECT_NEAR(c->Bounds().x, 100.0f, 0.01f);
    EXPECT_NEAR(c->Bounds().y, 150.0f, 0.01f);
}

TEST(Canvas, NullElementHandledGracefully) {
    Canvas::SetLeft(nullptr, 50.0f);
    Canvas::SetTop(nullptr, 30.0f);
    EXPECT_NEAR(Canvas::GetLeft(nullptr), 0.0f, 0.01f);
    EXPECT_NEAR(Canvas::GetTop(nullptr), 0.0f, 0.01f);
}
