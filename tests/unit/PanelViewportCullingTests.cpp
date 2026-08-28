// PanelViewportCullingTests.cpp — Panel::Render viewport culling (WP-07 §S3).
//
// Panel::Render skips a child whose bounds don't intersect the panel's own bounds
// (StackPanel overflow) OR the DrawingContext's clip hint (partial redraw). A spy
// leaf records whether its Render was called; the DrawingContext is built with
// null dc/brush because the spy never issues a draw — only the ClipHint matters.

#include "../framework/Test.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/graphics/DrawingContext.h"

using namespace fluent;

namespace {
// A leaf that records whether Render was called, without touching the device.
class RenderSpyLeaf : public FrameworkElement {
public:
    bool rendered = false;
    void Render(const DrawingContext&) override { rendered = true; }
    // Expose the protected render-invalidation for the dirty-bounds test.
    void Touch() { Invalidate(); }
};

// A spy that paints `pad` DIPs outside its layout bounds, the way a control with a
// focus ring does.
class OverflowingSpyLeaf : public RenderSpyLeaf {
public:
    float pad = 0.0f;
    float VisualOverflowDip() const override { return pad; }
};

// A DrawingContext with a specific clip hint and no real device (the spy never
// draws, so null dc/brush is safe).
DrawingContext MakeDc(const RectDip& hint) {
    return DrawingContext{nullptr, nullptr, 1.0f, nullptr, &hint};
}
} // namespace

// A child fully inside both the panel and the clip hint is rendered.
TEST(PanelCulling, VisibleChildIsRendered) {
    StackPanel panel;
    auto* a = panel.Emplace<RenderSpyLeaf>();
    panel.SetBounds({0, 0, 100, 100});
    a->SetBounds({0, 0, 100, 20});

    RectDip hint{0, 0, 100, 100};
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_TRUE(a->rendered);
}

// A child positioned beyond the panel's own bounds (StackPanel overflow) is
// culled even when the clip hint is unbounded.
TEST(PanelCulling, ChildBelowPanelBoundsIsCulled) {
    StackPanel panel;
    auto* a = panel.Emplace<RenderSpyLeaf>();
    auto* b = panel.Emplace<RenderSpyLeaf>();
    panel.SetBounds({0, 0, 100, 100});
    a->SetBounds({0, 0, 100, 20});      // inside
    b->SetBounds({0, 500, 100, 20});    // below the panel's 100-tall bounds

    RectDip hint{-1e9f, -1e9f, 2e9f, 2e9f};  // unbounded
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_TRUE(a->rendered);
    EXPECT_FALSE(b->rendered);
}

// A child inside the panel but outside the dirty clip hint is culled (partial
// redraw scenario).
TEST(PanelCulling, ChildOutsideClipHintIsCulled) {
    StackPanel panel;
    auto* top = panel.Emplace<RenderSpyLeaf>();
    auto* bottom = panel.Emplace<RenderSpyLeaf>();
    panel.SetBounds({0, 0, 100, 200});
    top->SetBounds({0, 0, 100, 20});       // inside the 30-tall dirty hint
    bottom->SetBounds({0, 150, 100, 20});  // inside panel, below the dirty hint

    RectDip hint{0, 0, 100, 30};  // only the top 30 DIPs are dirty
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_TRUE(top->rendered);
    EXPECT_FALSE(bottom->rendered);
}

// A child straddling the clip-hint edge still renders (intersection, not
// containment).
TEST(PanelCulling, ChildStraddlingHintEdgeRenders) {
    StackPanel panel;
    auto* c = panel.Emplace<RenderSpyLeaf>();
    panel.SetBounds({0, 0, 100, 200});
    c->SetBounds({0, 20, 100, 40});   // [20,60): straddles a hint ending at 40

    RectDip hint{0, 0, 100, 40};
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_TRUE(c->rendered);
}

// ---------------------------------------------------------------------------
// Culling uses VISUAL bounds, not layout bounds
// ---------------------------------------------------------------------------
// The host clears the WHOLE dirty region before repainting it. A child whose focus
// ring reaches into that region while its layout bounds sit outside it would have the
// ring wiped by that clear and then be skipped by the cull — the ring's outer edge
// would survive as residue. So the cull must test the child's painted extent.

TEST(PanelCulling, ChildWhoseOverflowTouchesHintRenders) {
    StackPanel panel;
    auto* c = panel.Emplace<OverflowingSpyLeaf>();
    panel.SetBounds({0, 0, 100, 200});
    c->SetBounds({0, 50, 100, 20});   // bounds [50,70)
    c->pad = 5.0f;                    // paints [45,75)

    // A hint ending at 48: it misses the child's BOUNDS but covers part of the band
    // its focus ring paints into.
    RectDip hint{0, 0, 100, 48};
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_TRUE(c->rendered);
}

// The cull still works: a child whose visual bounds (pad included) clear the hint
// entirely is skipped, so the partial-redraw win is not thrown away.
TEST(PanelCulling, ChildWhoseOverflowMissesHintIsCulled) {
    StackPanel panel;
    auto* c = panel.Emplace<OverflowingSpyLeaf>();
    panel.SetBounds({0, 0, 100, 200});
    c->SetBounds({0, 50, 100, 20});
    c->pad = 5.0f;                    // paints [45,75)

    RectDip hint{0, 0, 100, 40};      // ends well before 45
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_FALSE(c->rendered);
}

// A zero-overflow child behaves exactly as before (no behaviour change for the
// controls that paint strictly inside their bounds).
TEST(PanelCulling, ZeroOverflowChildCullsOnBoundsAsBefore) {
    StackPanel panel;
    auto* c = panel.Emplace<OverflowingSpyLeaf>();
    panel.SetBounds({0, 0, 100, 200});
    c->SetBounds({0, 50, 100, 20});
    c->pad = 0.0f;

    RectDip hint{0, 0, 100, 48};      // misses [50,70) with no pad to bridge it
    DrawingContext dc = MakeDc(hint);
    panel.Render(dc);
    EXPECT_FALSE(c->rendered);
}

// The dirty rect a child reports and the rect the cull tests come from the SAME
// virtual, so they cannot drift apart.
TEST(PanelCulling, DirtyBoundsMatchTheCullRect) {
    OverflowingSpyLeaf leaf;
    leaf.SetBounds({10, 20, 100, 40});
    leaf.pad = 4.75f;
    leaf.Touch();

    std::vector<RectDip> dirty;
    leaf.CollectDirtyBounds(dirty);
    EXPECT_EQ(dirty.size(), static_cast<size_t>(1));
    if (dirty.size() == 1) {
        RectDip vb = leaf.VisualBounds();
        EXPECT_NEAR(dirty[0].x, vb.x, 0.001f);
        EXPECT_NEAR(dirty[0].y, vb.y, 0.001f);
        EXPECT_NEAR(dirty[0].w, vb.w, 0.001f);
        EXPECT_NEAR(dirty[0].h, vb.h, 0.001f);
        EXPECT_NEAR(dirty[0].x, 10.0f - 4.75f, 0.001f);
    }
}

// The dirty-bounds collector reports only children whose own flags are set.
TEST(PanelCulling, CollectDirtyBoundsReportsDirtyChildOnly) {
    StackPanel panel;
    auto* a = panel.Emplace<RenderSpyLeaf>();
    auto* b = panel.Emplace<RenderSpyLeaf>();
    panel.SetBounds({0, 0, 100, 200});
    a->SetBounds({0, 0, 100, 20});
    b->SetBounds({0, 40, 100, 20});
    // Clear any dirt from construction/SetBounds so the test controls the state.
    panel.ClearDirtySubtree();

    // Dirty only `b` (Render-level).
    b->Touch();

    std::vector<RectDip> dirty;
    panel.CollectDirtyBounds(dirty);
    // Exactly one dirty rect, matching b's bounds.
    EXPECT_EQ(dirty.size(), static_cast<size_t>(1));
    if (dirty.size() == 1) {
        EXPECT_NEAR(dirty[0].y, 40.0f, 0.001f);
        EXPECT_NEAR(dirty[0].h, 20.0f, 0.001f);
    }
}
