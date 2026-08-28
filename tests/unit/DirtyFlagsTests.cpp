// DirtyFlagsTests.cpp — verifies the DirtyFlags closure and Element accumulation.
//
// Phase 2a introduces layout/render dirty flags. These tests lock the flag
// algebra (Measure implies Arrange+Render; Arrange implies Render) and that an
// element accumulates the closure of what it invalidated while still routing a
// frame request to the root callback. Pure logic — no HWND / D3D.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/core/Invalidation.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

// --- Flag algebra ----------------------------------------------------------

TEST(DirtyFlags, MeasureImpliesArrangeAndRender) {
    DirtyFlags f = ExpandDirty(DirtyFlags::Measure);
    EXPECT_TRUE(Has(f, DirtyFlags::Measure));
    EXPECT_TRUE(Has(f, DirtyFlags::Arrange));
    EXPECT_TRUE(Has(f, DirtyFlags::Render));
}

TEST(DirtyFlags, ArrangeImpliesRenderOnly) {
    DirtyFlags f = ExpandDirty(DirtyFlags::Arrange);
    EXPECT_FALSE(Has(f, DirtyFlags::Measure));
    EXPECT_TRUE(Has(f, DirtyFlags::Arrange));
    EXPECT_TRUE(Has(f, DirtyFlags::Render));
}

TEST(DirtyFlags, RenderStaysRender) {
    DirtyFlags f = ExpandDirty(DirtyFlags::Render);
    EXPECT_FALSE(Has(f, DirtyFlags::Measure));
    EXPECT_FALSE(Has(f, DirtyFlags::Arrange));
    EXPECT_TRUE(Has(f, DirtyFlags::Render));
}

TEST(DirtyFlags, OrAccumulates) {
    DirtyFlags f = DirtyFlags::None;
    f |= DirtyFlags::Render;
    f |= DirtyFlags::Arrange;
    EXPECT_TRUE(Has(f, DirtyFlags::Render));
    EXPECT_TRUE(Has(f, DirtyFlags::Arrange));
    EXPECT_FALSE(Has(f, DirtyFlags::Measure));
    EXPECT_TRUE(Any(f));
}

// --- Element accumulation --------------------------------------------------

namespace {
// Exposes the protected Invalidate* helpers for testing.
class DirtyLeaf : public TestLeaf {
public:
    using TestLeaf::TestLeaf;
    void PokeRender()  { Invalidate(); }
    void PokeMeasure() { InvalidateMeasure(); }
    void PokeArrange() { InvalidateArrange(); }
};

int g_frames = 0;
void CountFrame(void*) { ++g_frames; }
}  // namespace

// A fresh element starts clean.
TEST(DirtyFlags, ElementStartsClean) {
    DirtyLeaf leaf;
    EXPECT_FALSE(Any(leaf.Dirty()));
}

// Invalidate() (legacy) records Render only.
TEST(DirtyFlags, InvalidateRecordsRenderOnly) {
    DirtyLeaf leaf;
    leaf.PokeRender();
    EXPECT_TRUE(Has(leaf.Dirty(), DirtyFlags::Render));
    EXPECT_FALSE(Has(leaf.Dirty(), DirtyFlags::Measure));
    EXPECT_FALSE(Has(leaf.Dirty(), DirtyFlags::Arrange));
}

// InvalidateMeasure() records the full Measure/Arrange/Render closure.
TEST(DirtyFlags, InvalidateMeasureRecordsClosure) {
    DirtyLeaf leaf;
    leaf.PokeMeasure();
    EXPECT_TRUE(Has(leaf.Dirty(), DirtyFlags::Measure));
    EXPECT_TRUE(Has(leaf.Dirty(), DirtyFlags::Arrange));
    EXPECT_TRUE(Has(leaf.Dirty(), DirtyFlags::Render));
}

// ClearDirty() resets the accumulated flags.
TEST(DirtyFlags, ClearDirtyResets) {
    DirtyLeaf leaf;
    leaf.PokeMeasure();
    EXPECT_TRUE(Any(leaf.Dirty()));
    leaf.ClearDirty();
    EXPECT_FALSE(Any(leaf.Dirty()));
}

// A nested element's dirty flags stay local, but the frame request still
// reaches the root callback (parent chain), and the parent is not marked dirty.
TEST(DirtyFlags, DirtyIsLocalButFrameRequestReachesRoot) {
    g_frames = 0;
    StackPanel root;
    root.SetInvalidateCallback(&CountFrame, nullptr);

    auto* leaf = root.Emplace<DirtyLeaf>();
    leaf->PokeMeasure();

    // Leaf accumulated its own closure.
    EXPECT_TRUE(Has(leaf->Dirty(), DirtyFlags::Measure));
    // Parent (root) was only asked to request a frame, not marked measure-dirty.
    EXPECT_FALSE(Has(root.Dirty(), DirtyFlags::Measure));
    // The frame request reached the root's callback exactly once.
    EXPECT_EQ(g_frames, 1);
}
