#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/DockPanel.h"

using namespace fluent;

TEST(DockPanel, EmptyPanelReturnsZeroSize) {
    DockPanel dock;
    dock.Measure(800.0f, 600.0f);
    EXPECT_NEAR(dock.Desired().w, 0.0f, 0.01f);
    EXPECT_NEAR(dock.Desired().h, 0.0f, 0.01f);
}

TEST(DockPanel, SingleChildFillsByDefault) {
    DockPanel dock;
    auto* child = dock.Emplace<TestLeaf>(100.0f, 50.0f);

    dock.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    // Last child fills all available space.
    EXPECT_NEAR(child->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(child->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(child->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(child->Bounds().h, 300.0f, 0.01f);
}

TEST(DockPanel, LeftDockConsumesWidth) {
    DockPanel dock;
    auto* left = dock.Emplace<TestLeaf>(100.0f, 200.0f);
    auto* fill = dock.Emplace<TestLeaf>();

    DockPanel::SetDock(left, Dock::Left);

    dock.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    EXPECT_NEAR(left->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(left->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(left->Bounds().w, 100.0f, 0.01f);
    EXPECT_NEAR(left->Bounds().h, 300.0f, 0.01f);

    EXPECT_NEAR(fill->Bounds().x, 100.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().w, 300.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 300.0f, 0.01f);
}

TEST(DockPanel, TopDockConsumesHeight) {
    DockPanel dock;
    auto* top = dock.Emplace<TestLeaf>(400.0f, 50.0f);
    auto* fill = dock.Emplace<TestLeaf>();

    DockPanel::SetDock(top, Dock::Top);

    dock.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    EXPECT_NEAR(top->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(top->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(top->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(top->Bounds().h, 50.0f, 0.01f);

    EXPECT_NEAR(fill->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().y, 50.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 250.0f, 0.01f);
}

TEST(DockPanel, RightDockConsumesFromRight) {
    DockPanel dock;
    auto* right = dock.Emplace<TestLeaf>(100.0f, 300.0f);
    auto* fill = dock.Emplace<TestLeaf>();

    DockPanel::SetDock(right, Dock::Right);

    dock.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    EXPECT_NEAR(right->Bounds().x, 300.0f, 0.01f);
    EXPECT_NEAR(right->Bounds().y, 0.0f, 0.01f);
    EXPECT_NEAR(right->Bounds().w, 100.0f, 0.01f);
    EXPECT_NEAR(right->Bounds().h, 300.0f, 0.01f);

    EXPECT_NEAR(fill->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().w, 300.0f, 0.01f);
}

TEST(DockPanel, BottomDockConsumesFromBottom) {
    DockPanel dock;
    auto* bottom = dock.Emplace<TestLeaf>(400.0f, 50.0f);
    auto* fill = dock.Emplace<TestLeaf>();

    DockPanel::SetDock(bottom, Dock::Bottom);

    dock.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    EXPECT_NEAR(bottom->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(bottom->Bounds().y, 250.0f, 0.01f);
    EXPECT_NEAR(bottom->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(bottom->Bounds().h, 50.0f, 0.01f);

    EXPECT_NEAR(fill->Bounds().h, 250.0f, 0.01f);
}

TEST(DockPanel, OrderMatters) {
    // Top then Left: top spans full width, left only gets remaining height.
    DockPanel dock1;
    auto* top1 = dock1.Emplace<TestLeaf>(400.0f, 50.0f);
    auto* left1 = dock1.Emplace<TestLeaf>(100.0f, 250.0f);
    auto* fill1 = dock1.Emplace<TestLeaf>();

    DockPanel::SetDock(top1, Dock::Top);
    DockPanel::SetDock(left1, Dock::Left);

    dock1.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    EXPECT_NEAR(top1->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(left1->Bounds().y, 50.0f, 0.01f);
    EXPECT_NEAR(left1->Bounds().h, 250.0f, 0.01f);

    // Left then Top: left spans full height, top only gets remaining width.
    DockPanel dock2;
    auto* left2 = dock2.Emplace<TestLeaf>(100.0f, 300.0f);
    auto* top2 = dock2.Emplace<TestLeaf>(300.0f, 50.0f);
    auto* fill2 = dock2.Emplace<TestLeaf>();

    DockPanel::SetDock(left2, Dock::Left);
    DockPanel::SetDock(top2, Dock::Top);

    dock2.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    EXPECT_NEAR(left2->Bounds().h, 300.0f, 0.01f);
    EXPECT_NEAR(top2->Bounds().x, 100.0f, 0.01f);
    EXPECT_NEAR(top2->Bounds().w, 300.0f, 0.01f);
}

TEST(DockPanel, LastChildFillFalse) {
    DockPanel dock;
    dock.SetLastChildFill(false);

    auto* left = dock.Emplace<TestLeaf>(100.0f, 300.0f);
    auto* last = dock.Emplace<TestLeaf>(50.0f, 300.0f);

    DockPanel::SetDock(left, Dock::Left);
    DockPanel::SetDock(last, Dock::Right);

    dock.UpdateLayout({0.0f, 0.0f, 400.0f, 300.0f});

    // Last child docks to right instead of filling.
    EXPECT_NEAR(last->Bounds().x, 350.0f, 0.01f);
    EXPECT_NEAR(last->Bounds().w, 50.0f, 0.01f);
}

TEST(DockPanel, DefaultDockIsLeft) {
    TestLeaf leaf;
    EXPECT_NEAR(static_cast<int>(DockPanel::GetDock(&leaf)), static_cast<int>(Dock::Left), 0.01f);
}

TEST(DockPanel, NullElementHandledGracefully) {
    DockPanel::SetDock(nullptr, Dock::Top);
    EXPECT_NEAR(static_cast<int>(DockPanel::GetDock(nullptr)), static_cast<int>(Dock::Left), 0.01f);
}
