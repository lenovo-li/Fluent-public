// MeasureCacheTests.cpp — unit tests for the Element measure short-circuit cache
// (M1, roadmap §6.2). Pure logic (no HWND/D2D): verifies MeasureCached reuses the
// cached desired size when the constraint is unchanged and nothing is dirty, and
// re-runs the virtual Measure() on a constraint change, an InvalidateMeasure, or a
// size-affecting setter. Panels re-measure when any descendant is dirty.

#include "../framework/Test.h"
#include "../../FluentUI/core/Invalidation.h"
#include "../../FluentUI/diagnostics/LayoutCostProbe.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

namespace {
// A leaf that counts how many times its virtual Measure() actually runs, so a
// test can prove MeasureCached skipped it on a cache hit. Its desired size is a
// fixed constant (the point is the call count, not the geometry).
class CountingLeaf : public FrameworkElement {
public:
    void Measure(float availW, float availH) override {
        UNREFERENCED_PARAMETER(availW);
        UNREFERENCED_PARAMETER(availH);
        ++measures_;
        desired_ = {20.0f, 10.0f};
    }
    void Render(const DrawingContext&) override {}
    int measures() const { return measures_; }

    // Expose the protected invalidators for the test.
    void PokeMeasure() { InvalidateMeasure(); }
    void PokeRender()  { Invalidate(); }
private:
    int measures_ = 0;
};
} // namespace

// A hit (same constraint, not dirty) reuses the cache and skips Measure().
TEST(MeasureCache, ReusesOnUnchangedConstraint) {
    CountingLeaf leaf;
    leaf.MeasureCached(100.0f, 50.0f);
    EXPECT_EQ(leaf.measures(), 1);
    leaf.MeasureCached(100.0f, 50.0f);   // same constraint, clean -> cache hit
    EXPECT_EQ(leaf.measures(), 1);
    // Desired is still correct after a cache hit.
    EXPECT_NEAR(leaf.Desired().w, 20.0f, 1e-6f);
    EXPECT_NEAR(leaf.Desired().h, 10.0f, 1e-6f);
}

// A changed constraint misses the cache and re-measures.
TEST(MeasureCache, RemeasuresOnConstraintChange) {
    CountingLeaf leaf;
    leaf.MeasureCached(100.0f, 50.0f);
    EXPECT_EQ(leaf.measures(), 1);
    leaf.MeasureCached(120.0f, 50.0f);   // width changed -> miss
    EXPECT_EQ(leaf.measures(), 2);
    leaf.MeasureCached(120.0f, 60.0f);   // height changed -> miss
    EXPECT_EQ(leaf.measures(), 3);
}

// InvalidateMeasure drops the cache so the next MeasureCached re-runs Measure().
TEST(MeasureCache, InvalidateMeasureForcesRemeasure) {
    CountingLeaf leaf;
    leaf.MeasureCached(100.0f, 50.0f);
    EXPECT_EQ(leaf.measures(), 1);
    leaf.PokeMeasure();                  // e.g. text/font changed
    leaf.MeasureCached(100.0f, 50.0f);   // same constraint but dirty -> miss
    EXPECT_EQ(leaf.measures(), 2);
}

// A Render-only invalidation does NOT drop the measure cache.
TEST(MeasureCache, RenderInvalidationKeepsCache) {
    CountingLeaf leaf;
    leaf.MeasureCached(100.0f, 50.0f);
    EXPECT_EQ(leaf.measures(), 1);
    leaf.PokeRender();                   // pixels only, size unchanged
    leaf.MeasureCached(100.0f, 50.0f);   // still clean for Measure -> hit
    EXPECT_EQ(leaf.measures(), 1);
}

// A size-affecting setter (SetWidth) invalidates Measure and drops the cache.
TEST(MeasureCache, SizeSetterDropsCache) {
    CountingLeaf leaf;
    leaf.MeasureCached(100.0f, 50.0f);
    EXPECT_EQ(leaf.measures(), 1);
    leaf.SetWidth(40.0f);                // explicit size change -> InvalidateMeasure
    leaf.MeasureCached(100.0f, 50.0f);
    EXPECT_EQ(leaf.measures(), 2);
}

// A panel reuses its cached size when the whole subtree is clean, skipping the
// children's Measure entirely.
TEST(MeasureCache, PanelReusesWhenSubtreeClean) {
    StackPanel root;
    auto* a = root.Emplace<CountingLeaf>();
    auto* b = root.Emplace<CountingLeaf>();

    root.MeasureCached(200.0f, 100.0f);
    EXPECT_EQ(a->measures(), 1);
    EXPECT_EQ(b->measures(), 1);

    root.MeasureCached(200.0f, 100.0f);  // subtree clean, same constraint -> hit
    EXPECT_EQ(a->measures(), 1);         // children not re-measured
    EXPECT_EQ(b->measures(), 1);
}

// A dirty descendant forces the containing panel to re-run MeasureOverride (its
// NeedsRemeasure sees the subtree), but only the dirty child is actually
// re-measured — the clean sibling is a cache hit. This is the whole point of M1:
// one changed control does not re-measure its clean neighbors.
TEST(MeasureCache, PanelRemeasuresDirtyChildOnlySkipsCleanSibling) {
    StackPanel root;
    auto* a = root.Emplace<CountingLeaf>();
    auto* b = root.Emplace<CountingLeaf>();

    root.MeasureCached(200.0f, 100.0f);
    EXPECT_EQ(a->measures(), 1);
    EXPECT_EQ(b->measures(), 1);

    a->PokeMeasure();                    // one child changed its desired size
    root.MeasureCached(200.0f, 100.0f);  // panel re-runs MeasureOverride
    EXPECT_EQ(a->measures(), 2);         // dirty child re-measured
    EXPECT_EQ(b->measures(), 1);         // clean sibling skipped (cache hit)
}

// The LayoutCostProbe counters must partition the traversal the same way the cache
// does: every MeasureCached call lands in exactly one of measured/hit. This is the
// property the resize trace's `measured=N hits=M` line is read against — if the two
// counters could double-count or miss, a diagnosed hit rate would be fiction.
//
// The probe is a process-global (it is read inside Measure, so it cannot afford
// indirection), hence the explicit enable/reset here and the disable at the end: a
// test that left it armed would make every later test in the binary pay for counting
// and would leak counts across cases.
TEST(MeasureCache, ProbeCountsPartitionCallsAndHits) {
    LayoutCostProbe::SetEnabled(true);
    LayoutCostProbe::Reset();

    StackPanel root;
    auto* a = root.Emplace<CountingLeaf>();
    auto* b = root.Emplace<CountingLeaf>();

    // First pass: panel + two leaves all measure for real. The panel itself is
    // measured directly (not through MeasureCached) by this call, so only the two
    // children are counted.
    root.MeasureCached(200.0f, 100.0f);
    const int firstCalls = LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls);
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits), 0);
    EXPECT_TRUE(firstCalls >= 2);   // both leaves, plus the root's own cached entry

    LayoutCostProbe::Reset();
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls), 0);
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits), 0);

    // Second pass with nothing dirty: the root short-circuits, so the children are
    // never even visited. One hit, no real measures — the number that matters is
    // that calls stayed at zero.
    root.MeasureCached(200.0f, 100.0f);
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls), 0);
    EXPECT_TRUE(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits) >= 1);

    // One dirty child: the panel re-runs, the dirty child measures, the clean
    // sibling is a hit. Both counters move, and neither counts the same visit twice.
    LayoutCostProbe::Reset();
    a->PokeMeasure();
    root.MeasureCached(200.0f, 100.0f);
    EXPECT_TRUE(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls) >= 1);
    EXPECT_TRUE(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits) >= 1);

    LayoutCostProbe::Reset();
    LayoutCostProbe::SetEnabled(false);
}

// Counting must be free when the probe is off — that is the property that lets the
// Bump calls stay in MeasureCached unconditionally. Off means the counters do not
// move at all (not merely that nothing is printed).
TEST(MeasureCache, ProbeDisabledRecordsNothing) {
    LayoutCostProbe::SetEnabled(false);
    LayoutCostProbe::Reset();

    StackPanel root;
    root.Emplace<CountingLeaf>();
    root.MeasureCached(200.0f, 100.0f);

    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls), 0);
    EXPECT_EQ(LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits), 0);
}
