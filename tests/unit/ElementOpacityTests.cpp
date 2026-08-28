// ElementOpacityTests.cpp — UIElement/Visual opacity: the property itself, how it
// composes down a tree, and the guarantees the feature promises.
//
// A spy leaf records the opacity of the DrawingContext it was rendered with, which
// is exactly what a real control would multiply its colors by (DrawingContext is
// the single choke point — no control touches the brush directly).

#include "../framework/Test.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/graphics/DrawingContext.h"

using namespace fluent;

namespace {
// Records the context opacity it saw. Real controls never read Opacity(); they draw
// through the typed methods, which apply it. Reading it here is how we observe what
// those methods would have used.
class OpacitySpy : public FrameworkElement {
public:
    bool rendered = false;
    float seenOpacity = -1.0f;
    void Render(const DrawingContext& dc) override {
        rendered = true;
        seenOpacity = dc.Opacity();
    }
};

// Exposes the protected Invalidate/Dirty pair so the dirty-flag assertions can run
// (SetBounds does not set a dirty flag — see DirtyBoundsOverflowTests).
template <class T>
struct Dirtyable : T {
    using T::Invalidate;
};

DrawingContext MakeDc() { return DrawingContext{nullptr, nullptr, 1.0f}; }
} // namespace

// --- The property --------------------------------------------------------

// Default is opaque: an app that never calls SetOpacity is unaffected.
TEST(ElementOpacity, DefaultsToOpaque) {
    OpacitySpy e;
    EXPECT_NEAR(e.Opacity(), 1.0f, 0.0001f);
}

TEST(ElementOpacity, SetOpacityRoundTrips) {
    OpacitySpy e;
    e.SetOpacity(0.5f);
    EXPECT_NEAR(e.Opacity(), 0.5f, 0.0001f);
}

TEST(ElementOpacity, SetOpacityClamps) {
    OpacitySpy e;
    e.SetOpacity(-3.0f);
    EXPECT_NEAR(e.Opacity(), 0.0f, 0.0001f);
    e.SetOpacity(4.0f);
    EXPECT_NEAR(e.Opacity(), 1.0f, 0.0001f);
}

// Opacity is a RENDER-level change: it must dirty the element (so the frame is
// scheduled and a dirty rect reported) but must NOT request a re-measure — the
// element's size and position are unchanged, and a spurious Measure bit would
// collapse the frame into a full-window redraw.
TEST(ElementOpacity, SetOpacityDirtiesRenderNotMeasure) {
    Dirtyable<OpacitySpy> e;
    e.SetBounds({0, 0, 50, 20});
    e.ClearDirtySubtree();

    e.SetOpacity(0.5f);
    EXPECT_TRUE(e.AnyDirtyInSubtree(DirtyFlags::Render));
    EXPECT_FALSE(e.AnyDirtyInSubtree(DirtyFlags::Measure));
}

// A redundant set is a no-op (SetProperty's contract) so it never schedules a frame.
TEST(ElementOpacity, RedundantSetDoesNotDirty) {
    Dirtyable<OpacitySpy> e;
    e.SetOpacity(0.5f);
    e.ClearDirtySubtree();
    e.SetOpacity(0.5f);
    EXPECT_FALSE(e.AnyDirtyInSubtree(DirtyFlags::Render));
}

// Opacity must report a dirty rect, or a faded element would not repaint until
// something else happened to dirty the same region.
TEST(ElementOpacity, SetOpacityReportsDirtyBounds) {
    Dirtyable<OpacitySpy> e;
    e.SetBounds({10, 20, 100, 40});
    e.ClearDirtySubtree();
    e.SetOpacity(0.25f);

    std::vector<RectDip> dirty;
    e.CollectDirtyBounds(dirty);
    EXPECT_EQ(dirty.size(), static_cast<size_t>(1));
    if (dirty.size() == 1) EXPECT_NEAR(dirty[0].y, 20.0f, 0.001f);
}

// --- RenderWithOpacity ---------------------------------------------------

// At full opacity the context passes through untouched — the zero-cost default path.
TEST(ElementOpacity, FullOpacityPassesContextThrough) {
    OpacitySpy e;
    DrawingContext dc = MakeDc();
    e.RenderWithOpacity(dc);
    EXPECT_TRUE(e.rendered);
    EXPECT_NEAR(e.seenOpacity, 1.0f, 0.0001f);
}

// A partial opacity reaches the element as a context multiplier.
TEST(ElementOpacity, PartialOpacityFoldsIntoContext) {
    OpacitySpy e;
    e.SetOpacity(0.4f);
    DrawingContext dc = MakeDc();
    e.RenderWithOpacity(dc);
    EXPECT_TRUE(e.rendered);
    EXPECT_NEAR(e.seenOpacity, 0.4f, 0.0001f);
}

// Zero opacity skips Render entirely: the host has already cleared the dirty
// region, so not drawing IS the element disappearing — and it saves the work.
TEST(ElementOpacity, ZeroOpacitySkipsRender) {
    OpacitySpy e;
    e.SetOpacity(0.0f);
    DrawingContext dc = MakeDc();
    e.RenderWithOpacity(dc);
    EXPECT_FALSE(e.rendered);
}

// An already-faded context multiplies with the element's own opacity rather than
// being overwritten (this is what makes nesting work).
TEST(ElementOpacity, ComposesWithAnAlreadyFadedContext) {
    OpacitySpy e;
    e.SetOpacity(0.5f);
    DrawingContext dc{nullptr, nullptr, 1.0f, nullptr, nullptr, 0.5f};
    e.RenderWithOpacity(dc);
    EXPECT_NEAR(e.seenOpacity, 0.25f, 0.0001f);
}

// --- Propagation through a Panel -----------------------------------------

// A panel's opacity reaches its children — the "set it on a container and the whole
// group fades" behavior.
TEST(ElementOpacity, PanelOpacityReachesChildren) {
    StackPanel panel;
    auto* a = panel.Emplace<OpacitySpy>();
    auto* b = panel.Emplace<OpacitySpy>();
    panel.SetBounds({0, 0, 100, 200});
    a->SetBounds({0, 0, 100, 20});
    b->SetBounds({0, 30, 100, 20});
    panel.SetOpacity(0.5f);

    DrawingContext dc = MakeDc();
    panel.RenderWithOpacity(dc);
    EXPECT_NEAR(a->seenOpacity, 0.5f, 0.0001f);
    EXPECT_NEAR(b->seenOpacity, 0.5f, 0.0001f);
}

// Panel opacity and child opacity multiply.
TEST(ElementOpacity, PanelAndChildOpacityMultiply) {
    StackPanel panel;
    auto* a = panel.Emplace<OpacitySpy>();
    panel.SetBounds({0, 0, 100, 200});
    a->SetBounds({0, 0, 100, 20});
    panel.SetOpacity(0.5f);
    a->SetOpacity(0.5f);

    DrawingContext dc = MakeDc();
    panel.RenderWithOpacity(dc);
    EXPECT_NEAR(a->seenOpacity, 0.25f, 0.0001f);
}

// Fading one child leaves its siblings fully opaque.
TEST(ElementOpacity, SiblingOpacityIsIndependent) {
    StackPanel panel;
    auto* a = panel.Emplace<OpacitySpy>();
    auto* b = panel.Emplace<OpacitySpy>();
    panel.SetBounds({0, 0, 100, 200});
    a->SetBounds({0, 0, 100, 20});
    b->SetBounds({0, 30, 100, 20});
    a->SetOpacity(0.25f);

    DrawingContext dc = MakeDc();
    panel.RenderWithOpacity(dc);
    EXPECT_NEAR(a->seenOpacity, 0.25f, 0.0001f);
    EXPECT_NEAR(b->seenOpacity, 1.0f,  0.0001f);
}

// Nesting is multiplicative through several levels.
TEST(ElementOpacity, NestedPanelsMultiplyThroughLevels) {
    StackPanel outer;
    auto* inner = outer.Emplace<StackPanel>();
    auto* leaf = inner->Emplace<OpacitySpy>();
    outer.SetBounds({0, 0, 100, 200});
    inner->SetBounds({0, 0, 100, 100});
    leaf->SetBounds({0, 0, 100, 20});
    outer.SetOpacity(0.5f);
    inner->SetOpacity(0.5f);
    leaf->SetOpacity(0.5f);

    DrawingContext dc = MakeDc();
    outer.RenderWithOpacity(dc);
    EXPECT_NEAR(leaf->seenOpacity, 0.125f, 0.0001f);
}

// A zero-opacity panel skips its whole subtree in one test, so a hidden group
// costs nothing to "draw".
TEST(ElementOpacity, ZeroOpacityPanelSkipsSubtree) {
    StackPanel panel;
    auto* a = panel.Emplace<OpacitySpy>();
    panel.SetBounds({0, 0, 100, 200});
    a->SetBounds({0, 0, 100, 20});
    panel.SetOpacity(0.0f);

    DrawingContext dc = MakeDc();
    panel.RenderWithOpacity(dc);
    EXPECT_FALSE(a->rendered);
}

// --- EffectiveOpacity (for composited controls) --------------------------
// A composited control paints into its OWN surface, so the opacity folded into the
// host's DrawingContext never reaches it. It must recompose the ancestor chain
// itself and push the product onto its composition visual — EffectiveOpacity is
// that product. Getting this wrong makes a faded panel fail to fade a TreeView.

TEST(ElementOpacity, EffectiveOpacityIsSelfWhenUnparented) {
    OpacitySpy e;
    e.SetOpacity(0.5f);
    EXPECT_NEAR(e.EffectiveOpacity(), 0.5f, 0.0001f);
}

TEST(ElementOpacity, EffectiveOpacityMultipliesAncestors) {
    StackPanel outer;
    auto* inner = outer.Emplace<StackPanel>();
    auto* leaf = inner->Emplace<OpacitySpy>();
    outer.SetOpacity(0.5f);
    inner->SetOpacity(0.5f);
    leaf->SetOpacity(0.5f);
    EXPECT_NEAR(leaf->EffectiveOpacity(), 0.125f, 0.0001f);
}

// The common case stays exactly 1 so a composited control pushes an opaque value
// and the compositor skips any blend work.
TEST(ElementOpacity, EffectiveOpacityIsOneByDefault) {
    StackPanel panel;
    auto* leaf = panel.Emplace<OpacitySpy>();
    EXPECT_NEAR(leaf->EffectiveOpacity(), 1.0f, 0.0001f);
}

// --- Opacity is appearance-only ------------------------------------------

// Layout must not react to opacity: a faded element still occupies its slot (this
// is what distinguishes it from SetVisible(false), and matches WPF/WinUI).
TEST(ElementOpacity, OpacityDoesNotAffectLayout) {
    StackPanel panel;
    auto* a = panel.Emplace<OpacitySpy>();
    auto* b = panel.Emplace<OpacitySpy>();
    a->SetWidth(100); a->SetHeight(20);
    b->SetWidth(100); b->SetHeight(20);

    panel.Measure(200, 200);
    const float opaqueH = panel.Desired().h;

    a->SetOpacity(0.0f);
    panel.Measure(200, 200);
    EXPECT_NEAR(panel.Desired().h, opaqueH, 0.001f);
}

// Hit-testing must not react either: a 0-opacity element is still hit (again,
// SetVisible(false) is the tool for removing something from interaction).
TEST(ElementOpacity, OpacityDoesNotAffectHitTest) {
    StackPanel panel;
    auto* a = panel.Emplace<OpacitySpy>();
    panel.SetBounds({0, 0, 100, 100});
    a->SetBounds({0, 0, 100, 40});
    a->SetOpacity(0.0f);
    EXPECT_TRUE(panel.HitTestDeep(10.0f, 10.0f) == a);
}
