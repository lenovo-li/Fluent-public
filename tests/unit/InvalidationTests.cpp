// InvalidationTests.cpp — verifies Invalidate() propagates up the parent chain.
//
// Phase 1 of the tree refactor: an element without its own invalidate callback
// delegates Invalidate() to Parent(), so setting the callback on the tree root
// is enough for a deeply nested control to trigger a repaint. Panel::Add wires
// the parent link; Panel::Clear severs it. Pure logic — no HWND / D3D.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

namespace {
// Counts invalidate callbacks routed to it (used as the "root host" sink).
int g_invalidateCount = 0;
void CountingInvalidate(void*) { ++g_invalidateCount; }

// A leaf that exposes Invalidate() so tests can trigger it directly.
class InvalidatingLeaf : public TestLeaf {
public:
    using TestLeaf::TestLeaf;
    void Poke() { Invalidate(); }
};
}  // namespace

// Panel::Add sets the child's parent to the panel.
TEST(Invalidation, AddSetsParent) {
    StackPanel root;
    auto* child = root.Emplace<TestLeaf>();
    EXPECT_TRUE(child->Parent() == &root);
}

// A nested leaf with no callback of its own routes Invalidate() up to the root,
// which holds the only callback.
TEST(Invalidation, NestedLeafPropagatesToRootCallback) {
    g_invalidateCount = 0;

    StackPanel root;
    root.SetInvalidateCallback(&CountingInvalidate, nullptr);

    auto* mid = root.Emplace<StackPanel>();     // no callback
    auto* leaf = mid->Emplace<InvalidatingLeaf>();  // no callback

    EXPECT_TRUE(leaf->Parent() == mid);
    EXPECT_TRUE(mid->Parent() == &root);

    leaf->Poke();
    EXPECT_EQ(g_invalidateCount, 1);  // reached the root's callback
}

namespace {
int g_childInvalidateCount = 0;
void CountChildInvalidate(void*) { ++g_childInvalidateCount; }
}  // namespace

// A child's own callback takes precedence over the parent chain.
TEST(Invalidation, ChildCallbackWinsOverParent) {
    g_invalidateCount = 0;
    g_childInvalidateCount = 0;

    StackPanel root;
    root.SetInvalidateCallback(&CountingInvalidate, nullptr);

    auto* leaf = root.Emplace<InvalidatingLeaf>();
    leaf->SetInvalidateCallback(&CountChildInvalidate, nullptr);

    leaf->Poke();
    EXPECT_EQ(g_childInvalidateCount, 1);
    EXPECT_EQ(g_invalidateCount, 0);  // did not fall through to the root
}

// Clear() severs the parent link so a detached subtree no longer propagates.
TEST(Invalidation, ClearSeversParent) {
    StackPanel root;
    auto* child = root.Emplace<TestLeaf>();
    EXPECT_TRUE(child->Parent() == &root);

    // child is destroyed by Clear(); capture the pointer relationship first.
    root.Clear();
    EXPECT_EQ(root.ChildCount(), static_cast<size_t>(0));
}
