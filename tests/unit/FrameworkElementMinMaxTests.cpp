// FrameworkElementMinMaxTests.cpp — verify Min/Max constraints in Arrange

#include "../framework/Test.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
using namespace fluent;

TEST(FrameworkElement, MinWidthEnforcedInArrange) {
    // A MinWidth larger than the slot makes the element overflow its container.
    // The arranged width must be the Min, not clamped to the slot width.
    // Go through a Panel so Panel::ArrangeChild applies the constraint.
    StackPanel sp;
    Border* b = sp.Emplace<Border>();
    b->SetMinWidth(400.0f);
    sp.Measure(300.0f, 200.0f);
    sp.Arrange({0, 0, 300, 200});
    EXPECT_NEAR(b->Bounds().w, 400.0f, 0.01f);  // actual width is 400, overflowing
}

TEST(FrameworkElement, MaxWidthEnforcedInArrange) {
    // A MaxWidth smaller than the slot caps the arranged width.
    StackPanel sp;
    Border* b = sp.Emplace<Border>();
    b->SetWidth(500.0f);
    b->SetMaxWidth(200.0f);
    sp.Measure(300.0f, 200.0f);
    sp.Arrange({0, 0, 300, 200});
    EXPECT_NEAR(b->Bounds().w, 200.0f, 0.01f);  // MaxWidth wins
}

TEST(FrameworkElement, StretchWithMaxWidth) {
    // HAlign::Stretch normally takes the full slot width. A MaxWidth must cap it.
    StackPanel sp;
    Border* b = sp.Emplace<Border>();
    b->SetMaxWidth(150.0f);
    b->SetHAlign(HAlign::Stretch);
    sp.Measure(300.0f, 200.0f);
    sp.Arrange({0, 0, 300, 200});
    EXPECT_NEAR(b->Bounds().w, 150.0f, 0.01f);  // Stretch capped by Max
}

TEST(FrameworkElement, MinMaxConflictMinWins) {
    // When MinWidth > MaxWidth, Min takes precedence (WPF behavior).
    StackPanel sp;
    Border* b = sp.Emplace<Border>();
    b->SetMinWidth(400.0f);
    b->SetMaxWidth(200.0f);
    sp.Measure(500.0f, 200.0f);
    sp.Arrange({0, 0, 500, 200});
    EXPECT_NEAR(b->Bounds().w, 400.0f, 0.01f);  // Min wins the conflict
}

TEST(FrameworkElement, MinHeightEnforcedInArrange) {
    // Horizontal StackPanel: the child's height comes from the slot, so a
    // MinHeight taller than the panel must still win.
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Horizontal);
    Border* b = sp.Emplace<Border>();
    b->SetMinHeight(150.0f);
    sp.Measure(300.0f, 100.0f);   // slot is 100 tall
    sp.Arrange({0, 0, 300, 100});
    EXPECT_NEAR(b->Bounds().h, 150.0f, 0.01f);  // overflows vertically
}

TEST(FrameworkElement, MaxHeightEnforcedInArrange) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Horizontal);
    Border* b = sp.Emplace<Border>();
    b->SetHeight(200.0f);
    b->SetMaxHeight(80.0f);
    sp.Measure(300.0f, 100.0f);
    sp.Arrange({0, 0, 300, 100});
    EXPECT_NEAR(b->Bounds().h, 80.0f, 0.01f);
}

TEST(FrameworkElement, StretchWithMaxHeight) {
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Horizontal);
    Border* b = sp.Emplace<Border>();
    b->SetMaxHeight(60.0f);
    b->SetVAlign(VAlign::Stretch);
    sp.Measure(300.0f, 100.0f);
    sp.Arrange({0, 0, 300, 100});
    EXPECT_NEAR(b->Bounds().h, 60.0f, 0.01f);
}

TEST(FrameworkElement, MinWidthInStackPanel) {
    // The reported bug, reproduced through a real panel: the Grid column in the
    // demo narrowed to ~267 DIP and the MinWidth-400 bar was arranged at 267.
    // This goes through Panel::ArrangeChild, which is where the clamp was missing.
    StackPanel sp;
    sp.SetOrientation(StackPanel::Orientation::Vertical);
    auto* child = sp.Emplace<Border>();
    child->SetMinWidth(400.0f);
    sp.Measure(267.0f, 500.0f);   // the width from the user's screenshot
    sp.Arrange({0, 0, 267, 500});
    EXPECT_NEAR(child->Bounds().w, 400.0f, 0.01f);  // not 267
}

TEST(FrameworkElement, CenterAlignWithMinWidth) {
    // Center alignment must compute offset from the CLAMPED width, not the slot.
    StackPanel sp;
    Border* b = sp.Emplace<Border>();
    b->SetMinWidth(400.0f);
    b->SetHAlign(HAlign::Center);
    sp.Measure(300.0f, 100.0f);
    sp.Arrange({0, 0, 300, 100});
    // Width is 400, centered in 300 means x = (300 - 400)*0.5 = -50.
    EXPECT_NEAR(b->Bounds().w, 400.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, -50.0f, 0.01f);
}

TEST(FrameworkElement, RightAlignWithMaxWidth) {
    StackPanel sp;
    Border* b = sp.Emplace<Border>();
    b->SetWidth(200.0f);      // Give it a desired size larger than Max
    b->SetMaxWidth(100.0f);
    b->SetHAlign(HAlign::Right);
    sp.Measure(300.0f, 100.0f);
    sp.Arrange({0, 0, 300, 100});
    // Width is 100 (clamped from 200), right-aligned in 300 means x = 300 - 100 = 200.
    EXPECT_NEAR(b->Bounds().w, 100.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 200.0f, 0.01f);
}
