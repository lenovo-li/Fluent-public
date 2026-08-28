// SubtreeDirtyTests.cpp — verifies subtree dirty queries and that size-affecting
// setters mark Measure dirty (phase 2b: draw-time auto relayout).
//
// The host calls AnyDirtyInSubtree(Measure) before a frame and re-runs layout if
// any descendant needs re-measuring; ClearDirtySubtree() resets the whole tree
// afterwards. These tests lock that Panel recurses correctly and that changing a
// control's text (which changes its desired size) sets the Measure bit so the
// host will relayout. Pure logic — no HWND / D3D.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/core/Invalidation.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/controls/CheckBox.h"

using namespace fluent;

// A clean tree reports no dirty anywhere.
TEST(SubtreeDirty, CleanTreeHasNoDirty) {
    StackPanel root;
    auto* mid = root.Emplace<StackPanel>();
    mid->Emplace<TestLeaf>();
    root.ClearDirtySubtree();
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Render));
}

// A deeply nested leaf's Measure dirty is visible from the root's subtree query.
TEST(SubtreeDirty, NestedMeasureDirtyVisibleFromRoot) {
    StackPanel root;
    auto* mid = root.Emplace<StackPanel>();
    auto* leaf = mid->Emplace<CheckBox>();
    root.ClearDirtySubtree();

    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
    leaf->SetText(L"changed");  // size-affecting: marks Measure dirty
    EXPECT_TRUE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
}

// ClearDirtySubtree() clears the whole tree, not just the root node.
TEST(SubtreeDirty, ClearResetsEntireTree) {
    StackPanel root;
    auto* leaf = root.Emplace<CheckBox>();
    leaf->SetText(L"dirty");
    EXPECT_TRUE(root.AnyDirtyInSubtree(DirtyFlags::Measure));

    root.ClearDirtySubtree();
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
    EXPECT_FALSE(leaf->AnyDirtyInSubtree(DirtyFlags::Measure));
}

// A Render-only change (not size-affecting) does NOT set Measure, so the host
// repaints without a needless relayout.
TEST(SubtreeDirty, CheckedChangeIsRenderNotMeasure) {
    StackPanel root;
    auto* leaf = root.Emplace<CheckBox>();
    root.ClearDirtySubtree();

    leaf->SetChecked(true);  // visual toggle only
    EXPECT_TRUE(root.AnyDirtyInSubtree(DirtyFlags::Render));
    EXPECT_FALSE(root.AnyDirtyInSubtree(DirtyFlags::Measure));
}
