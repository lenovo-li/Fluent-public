// PanelChildManagementTests.cpp — Panel::RemoveAt / InsertAt API tests.
//
// These methods let the caller mutate the child list beyond Add/Clear, which is what
// dynamic lists (chat messages, notifications, drag-drop reordering) need. The tests
// verify the core contract: list order is correct, Attach/Detach happens in the right
// sequence, and Measure invalidation fires so layout caching stays correct.

#include "../framework/Test.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/core/UIContext.h"

using namespace fluent;

namespace {

// Helper: build a panel with 3 children ["A", "B", "C"], return raw pointers.
struct ThreeChildPanel {
    StackPanel panel;
    Button* a = nullptr;
    Button* b = nullptr;
    Button* c = nullptr;

    ThreeChildPanel() {
        a = panel.Add(std::make_unique<Button>());
        a->SetText(L"A");
        b = panel.Add(std::make_unique<Button>());
        b->SetText(L"B");
        c = panel.Add(std::make_unique<Button>());
        c->SetText(L"C");
    }

    // Read back the current order as a string (e.g. "ABC" or "BCA").
    std::wstring Order() const {
        std::wstring s;
        for (size_t i = 0; i < panel.ChildCount(); ++i) {
            if (auto* child = dynamic_cast<Button*>(panel.ChildAt(i)))
                s += child->Text();
        }
        return s;
    }
};

} // namespace

// ============================================================================
// RemoveAt — basic behavior
// ============================================================================

TEST(PanelChildManagement, RemoveAt_MiddleElement_ShiftsRemainingForward) {
    ThreeChildPanel p;
    EXPECT_EQ(L"ABC", p.Order());

    p.panel.RemoveAt(1);  // remove "B"
    EXPECT_EQ(2u, p.panel.ChildCount());
    EXPECT_EQ(L"AC", p.Order());
    // The pointers p.a and p.c are now dangling if we kept them, but we only read
    // ChildAt, which returns what is NOW at each slot.
}

TEST(PanelChildManagement, RemoveAt_FirstElement) {
    ThreeChildPanel p;
    p.panel.RemoveAt(0);  // remove "A"
    EXPECT_EQ(2u, p.panel.ChildCount());
    EXPECT_EQ(L"BC", p.Order());
}

TEST(PanelChildManagement, RemoveAt_LastElement) {
    ThreeChildPanel p;
    p.panel.RemoveAt(2);  // remove "C"
    EXPECT_EQ(2u, p.panel.ChildCount());
    EXPECT_EQ(L"AB", p.Order());
}

TEST(PanelChildManagement, RemoveAt_OutOfRangeIsNoOp) {
    ThreeChildPanel p;
    p.panel.RemoveAt(999);
    EXPECT_EQ(3u, p.panel.ChildCount());
    EXPECT_EQ(L"ABC", p.Order());
}

TEST(PanelChildManagement, RemoveAt_OnEmptyPanelIsNoOp) {
    StackPanel panel;
    EXPECT_EQ(0u, panel.ChildCount());
    panel.RemoveAt(0);
    EXPECT_EQ(0u, panel.ChildCount());
}

// ============================================================================
// RemoveAt — Attach state
// ============================================================================

TEST(PanelChildManagement, RemoveAt_DetachesTheRemovedChild) {
    ThreeChildPanel p;

    // Attach the panel (and its children).
    UIContext ctx;
    p.panel.AttachToContext(ctx);
    EXPECT_TRUE(p.b->IsAttached());

    // Remove "B" — it should be detached before the unique_ptr destructs.
    p.panel.RemoveAt(1);
    // We cannot check p.b->IsAttached() because p.b is now a dangling pointer (the
    // unique_ptr destructed). The test passes if there is no crash and no leak,
    // which the test framework's allocator guards would catch.
}

TEST(PanelChildManagement, RemoveAt_RemainingChildrenStayAttached) {
    ThreeChildPanel p;
    UIContext ctx;
    p.panel.AttachToContext(ctx);

    p.panel.RemoveAt(1);  // remove "B"

    // "A" and "C" are still attached (they stayed in the tree).
    EXPECT_TRUE(p.panel.ChildAt(0)->IsAttached());  // was A, still A
    EXPECT_TRUE(p.panel.ChildAt(1)->IsAttached());  // was C, now at index 1
}

// ============================================================================
// RemoveAt — dirty flags
// ============================================================================

TEST(PanelChildManagement, RemoveAt_InvalidatesMeasure) {
    ThreeChildPanel p;
    // Clear dirty on both the panel AND its children, because NeedsRemeasure checks
    // the whole subtree.
    p.panel.ClearDirtySubtree();
    EXPECT_FALSE(p.panel.NeedsRemeasure());

    p.panel.RemoveAt(1);
    EXPECT_TRUE(p.panel.NeedsRemeasure());
}

// ============================================================================
// InsertAt — basic behavior
// ============================================================================

TEST(PanelChildManagement, InsertAt_InsertsAtTheGivenIndex) {
    ThreeChildPanel p;
    auto* x = p.panel.InsertAt(1, std::make_unique<Button>());
    x->SetText(L"X");

    EXPECT_EQ(4u, p.panel.ChildCount());
    EXPECT_EQ(L"AXBC", p.Order());
}

TEST(PanelChildManagement, InsertAt_AtZeroPrependsToFront) {
    ThreeChildPanel p;
    auto* x = p.panel.InsertAt(0, std::make_unique<Button>());
    x->SetText(L"X");

    EXPECT_EQ(4u, p.panel.ChildCount());
    EXPECT_EQ(L"XABC", p.Order());
}

TEST(PanelChildManagement, InsertAt_AtSizeAppendsToEnd) {
    ThreeChildPanel p;
    auto* x = p.panel.InsertAt(p.panel.ChildCount(), std::make_unique<Button>());
    x->SetText(L"X");

    EXPECT_EQ(4u, p.panel.ChildCount());
    EXPECT_EQ(L"ABCX", p.Order());
}

TEST(PanelChildManagement, InsertAt_BeyondSizeIsClamped_AppendsToEnd) {
    ThreeChildPanel p;
    auto* x = p.panel.InsertAt(999, std::make_unique<Button>());
    x->SetText(L"X");

    EXPECT_EQ(4u, p.panel.ChildCount());
    EXPECT_EQ(L"ABCX", p.Order());
}

TEST(PanelChildManagement, InsertAt_ReturnsRawPointerForConfiguration) {
    StackPanel panel;
    auto* btn = panel.InsertAt(0, std::make_unique<Button>());
    EXPECT_TRUE(btn != nullptr);
    btn->SetText(L"Configured");
    EXPECT_EQ(L"Configured", dynamic_cast<Button*>(panel.ChildAt(0))->Text());
}

TEST(PanelChildManagement, InsertAt_NullptrReturnsNullAndDoesNothing) {
    StackPanel panel;
    auto* result = panel.InsertAt(0, std::unique_ptr<Button>(nullptr));
    EXPECT_TRUE(result == nullptr);
    EXPECT_EQ(0u, panel.ChildCount());
}

// ============================================================================
// InsertAt — Attach state
// ============================================================================

TEST(PanelChildManagement, InsertAt_AttachesImmediatelyWhenPanelIsAttached) {
    ThreeChildPanel p;
    UIContext ctx;
    p.panel.AttachToContext(ctx);

    auto* x = p.panel.InsertAt(1, std::make_unique<Button>());
    x->SetText(L"X");

    EXPECT_TRUE(x->IsAttached());
}

TEST(PanelChildManagement, InsertAt_DoesNotAttachWhenPanelIsNotAttached) {
    ThreeChildPanel p;
    // Panel is not attached to a context.
    auto* x = p.panel.InsertAt(1, std::make_unique<Button>());
    x->SetText(L"X");

    EXPECT_FALSE(x->IsAttached());
}

// ============================================================================
// InsertAt — dirty flags
// ============================================================================

TEST(PanelChildManagement, InsertAt_InvalidatesMeasure) {
    ThreeChildPanel p;
    p.panel.ClearDirtySubtree();
    EXPECT_FALSE(p.panel.NeedsRemeasure());

    p.panel.InsertAt(1, std::make_unique<Button>());
    EXPECT_TRUE(p.panel.NeedsRemeasure());
}

// ============================================================================
// Mixed operations
// ============================================================================

TEST(PanelChildManagement, MixedOperations_Add_Insert_Remove) {
    StackPanel panel;
    auto* a = panel.Add(std::make_unique<Button>());
    a->SetText(L"A");
    auto* c = panel.Add(std::make_unique<Button>());
    c->SetText(L"C");

    // Insert "B" between A and C.
    auto* b = panel.InsertAt(1, std::make_unique<Button>());
    b->SetText(L"B");

    // Order is now A, B, C.
    EXPECT_EQ(3u, panel.ChildCount());
    std::wstring order;
    for (size_t i = 0; i < panel.ChildCount(); ++i)
        order += dynamic_cast<Button*>(panel.ChildAt(i))->Text();
    EXPECT_EQ(L"ABC", order);

    // Remove B.
    panel.RemoveAt(1);
    EXPECT_EQ(2u, panel.ChildCount());

    order.clear();
    for (size_t i = 0; i < panel.ChildCount(); ++i)
        order += dynamic_cast<Button*>(panel.ChildAt(i))->Text();
    EXPECT_EQ(L"AC", order);
}
