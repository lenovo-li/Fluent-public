// LabelMeasureCacheTests.cpp — component tests proving the two label-measuring
// paths that ran on EVERY Measure now go through the shared ResourceCache.
//
// Why this file exists. Several places built throwaway IDWriteTextLayouts from
// inside Measure, read a float or two off each, and dropped them:
//
//   * ContentControl::MeasureLabelWidth — the shared caption measurement, so this
//     is Button AND CheckBox AND RadioButton, one layout per Measure each.
//   * TabControl::MeasureHeaders — TWO layouts per tab (Normal + SemiBold), and it
//     was called unconditionally by MeasureOverride even though header widths
//     depend only on the header strings.
//   * ToggleSwitch::Measure — a ContentControl that could not use the shared helper
//     because it needs the label HEIGHT as well as the width, so it kept its own
//     build. MeasureLabelSize now returns both from one cached entry.
//   * GroupBox::Measure — a FrameworkElement (no access to the ContentControl
//     helper) measuring a header string fixed at build time, once per frame.
//
// NOT covered here: MenuFlyout::MeasureText, the heaviest site of all (O(items) per
// MeasureLevel). Its measurement lives on a private inner class reached only
// through an open popup window, so it is not headlessly testable — it was fixed by
// inspection and needs the real-hardware check noted in docs/OPTIMIZATIONS.md.
//
// A Measure re-runs whenever the offered constraint changes, and a resize drag
// changes it every frame — MeasureCached cannot short-circuit that. So the cost
// was per-control-per-frame for the whole drag, which is exactly the window where
// frames are already expensive (see project documentation on resizeMs dominating a drag).
//
// These tests assert against ResourceCache::Stats(), which is the only externally
// visible evidence that a DWrite build did or did not happen. Both are written to
// FAIL if the caching is removed: SameLabelMeasuredOnce would see misses == 2, and
// HeaderWidthsNotRebuiltOnReMeasure would see misses > 0 on the second Measure.
//
// Needs a real DirectWrite factory (no D3D / HWND / GPU), so this is a component
// test like TextBoxLayoutCacheTests and self-skips when DWrite is unavailable.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/TabControl.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/graphics/ResourceCache.h"
#include "../../FluentUI/core/UIContext.h"

using namespace fluent;

namespace {
// One shared DWrite factory for the suite (skip if the box has no DirectWrite).
DWriteContext& Ctx() {
    static DWriteContext ctx;
    static bool inited = SUCCEEDED(ctx.Initialize());
    (void)inited;
    return ctx;
}

UIContext MakeCtx(DWriteContext& dw, ResourceCache& cache) {
    UIContext c;
    c.dwrite = &dw;
    c.resourceCache = &cache;
    return c;
}
}  // namespace

// --- ContentControl::MeasureLabelWidth --------------------------------------

// Measuring the same Button twice at the same constraint builds one layout, not
// two. The second Measure is what a resize drag does over and over.
TEST(LabelMeasureCache, SameLabelMeasuredOnce) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    Button b;
    UIContext uictx = MakeCtx(dw, cache);
    b.AttachToContext(uictx);
    b.SetText(L"Save changes");

    cache.ResetFrameStats();
    b.Measure(400.0f, 200.0f);
    const uint32_t afterFirst = cache.Stats().misses;
    EXPECT_TRUE(afterFirst >= 1u);   // the label was actually built once

    // Second Measure at the SAME constraint. Measure() is called directly (not
    // MeasureCached) so the control genuinely re-runs its MeasureOverride — this
    // isolates the layout cache from the Measure-dirty short circuit.
    b.Measure(400.0f, 200.0f);
    EXPECT_EQ(cache.Stats().misses, afterFirst);  // nothing rebuilt
    EXPECT_TRUE(cache.Stats().hits >= 1u);        // served from the cache
}

// Two different controls carrying the SAME caption share one cached layout. This
// is the case a list of identically-labelled rows hits, and it also proves the key
// is the text (plus box), not the control instance.
TEST(LabelMeasureCache, IdenticalCaptionsSharedAcrossControls) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    Button b;
    CheckBox c;
    UIContext uictx = MakeCtx(dw, cache);
    b.AttachToContext(uictx);
    c.AttachToContext(uictx);

    // Same string, same font size (both default to the theme body size), same
    // layout box -> same key.
    b.SetText(L"Enable");
    c.SetText(L"Enable");

    cache.ResetFrameStats();
    b.Measure(300.0f, 100.0f);
    const uint32_t afterButton = cache.Stats().misses;
    EXPECT_TRUE(afterButton >= 1u);
    c.Measure(300.0f, 100.0f);

    // CheckBox offers the label a narrower box than Button (it subtracts the box +
    // gap), so the two keys legitimately differ and a hit is NOT asserted here.
    // What must hold is that repeating both measurements adds no further builds.
    const uint32_t stable = cache.Stats().misses;
    b.Measure(300.0f, 100.0f);
    c.Measure(300.0f, 100.0f);
    EXPECT_EQ(cache.Stats().misses, stable);
    EXPECT_TRUE(cache.Stats().hits >= 2u);  // both served from the cache
}

// Changing the caption must invalidate: the new string is a new key, so it builds.
// Without this, a cache keyed too coarsely would silently keep the old width and
// the button would size to its previous label.
TEST(LabelMeasureCache, ChangedCaptionRebuilds) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    Button b;
    UIContext uictx = MakeCtx(dw, cache);
    b.AttachToContext(uictx);

    b.SetText(L"OK");
    b.Measure(400.0f, 200.0f);
    const float narrow = b.Desired().w;

    b.SetText(L"A considerably longer caption");
    b.Measure(400.0f, 200.0f);
    const float wide = b.Desired().w;

    EXPECT_TRUE(wide > narrow);  // the new text really was measured
}

// A cache-less context (the headless default) must still measure correctly via the
// direct-build fallback. This is the path most of the existing suite runs on, so a
// regression here would be broad — assert it explicitly anyway.
TEST(LabelMeasureCache, NoCacheStillMeasures) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    Button b;
    UIContext uictx;
    uictx.dwrite = &dw;  // no resourceCache
    b.AttachToContext(uictx);
    b.SetText(L"Fallback path");
    b.Measure(400.0f, 200.0f);
    EXPECT_TRUE(b.Desired().w > 0.0f);
}

// --- TabControl::MeasureHeaders ---------------------------------------------

// Re-measuring at a DIFFERENT constraint must not rebuild header layouts. This is
// the resize-drag case specifically: every frame offers a new width, so the old
// unconditional MeasureHeaders rebuilt 2 layouts per tab per frame.
TEST(LabelMeasureCache, HeaderWidthsNotRebuiltOnReMeasure) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    TabControl tc;
    tc.AddTab(L"General", std::make_unique<StackPanel>());
    tc.AddTab(L"Appearance", std::make_unique<StackPanel>());
    tc.AddTab(L"Advanced", std::make_unique<StackPanel>());

    UIContext uictx = MakeCtx(dw, cache);
    tc.AttachToContext(uictx);  // OnAttachedToTree measures for real

    cache.ResetFrameStats();
    tc.Measure(600.0f, 400.0f);
    // Attach already did the measuring, so this Measure must not have built any
    // header layout at all.
    EXPECT_EQ(cache.Stats().misses, 0u);
    // And it must not even have CONSULTED the cache. misses alone cannot prove the
    // gate works: MeasureHeaders routes through the cache, so removing the gate
    // still yields zero misses — every lookup just hits instead. Zero HITS is what
    // says the walk was skipped rather than merely served cheaply. (Verified by
    // mutation: commenting out the gate leaves misses at 0 but drives hits to 6.)
    EXPECT_EQ(cache.Stats().hits, 0u);

    // Now the drag: a new width every frame.
    for (float w = 599.0f; w > 560.0f; w -= 1.0f) tc.Measure(w, 400.0f);
    EXPECT_EQ(cache.Stats().misses, 0u);  // still nothing rebuilt
    EXPECT_EQ(cache.Stats().hits, 0u);    // and still not walking the headers
}

// Adding a tab re-dirties, so the new header IS measured. The gate must not be so
// aggressive that a genuinely new string keeps a stale per-character estimate.
TEST(LabelMeasureCache, AddedTabHeaderGetsMeasured) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    TabControl tc;
    tc.AddTab(L"One", std::make_unique<StackPanel>());
    UIContext uictx = MakeCtx(dw, cache);
    tc.AttachToContext(uictx);
    tc.Measure(600.0f, 400.0f);
    const float oneTabW = tc.Desired().w;

    // A much wider header must widen the control. If the dirty flag were never set
    // by AddTab, the new tab would keep kFallbackCharWidth * length and this would
    // come out at a different (estimate-derived) width.
    cache.ResetFrameStats();
    tc.AddTab(L"A very much longer tab header", std::make_unique<StackPanel>());
    tc.Measure(600.0f, 400.0f);
    EXPECT_TRUE(cache.Stats().misses >= 1u);   // the new string was built
    EXPECT_TRUE(tc.Desired().w > oneTabW);     // and it affected layout
}

// Removing a tab keeps the surviving widths (no rebuild) AND keeps headers_ and
// headerWidths_ the same length. The second half is the real hazard: MeasureOverride
// indexes headerWidths_ by tab index, so a length mismatch is an out-of-range read.
TEST(LabelMeasureCache, RemovedTabKeepsWidthsAligned) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    TabControl tc;
    tc.AddTab(L"First", std::make_unique<StackPanel>());
    tc.AddTab(L"Second", std::make_unique<StackPanel>());
    tc.AddTab(L"Third", std::make_unique<StackPanel>());
    UIContext uictx = MakeCtx(dw, cache);
    tc.AttachToContext(uictx);
    tc.Measure(600.0f, 400.0f);

    cache.ResetFrameStats();
    tc.RemoveTab(1);
    tc.Measure(600.0f, 400.0f);   // must not crash, must not rebuild
    EXPECT_EQ(cache.Stats().misses, 0u);
    EXPECT_EQ(tc.TabCount(), 2);
    EXPECT_TRUE(tc.Desired().w > 0.0f);
}

// Measuring while detached leaves an estimate but stays dirty, so the first
// attached Measure replaces it with real metrics. Getting this backwards freezes
// the per-character estimate in permanently — a control whose tabs are sized by a
// constant rather than by its font.
TEST(LabelMeasureCache, DetachedEstimateReplacedAfterAttach) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    TabControl tc;
    tc.AddTab(L"WWWWWWWW", std::make_unique<StackPanel>());
    tc.Measure(600.0f, 400.0f);          // detached: no DWrite, estimate only
    const float estimated = tc.Desired().w;
    EXPECT_TRUE(estimated > 0.0f);

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);
    UIContext uictx = MakeCtx(dw, cache);
    cache.ResetFrameStats();
    tc.AttachToContext(uictx);
    // The attach must have done real measurement despite the earlier detached call
    // having already populated headerWidths_.
    EXPECT_TRUE(cache.Stats().misses >= 1u);
}

// --- ToggleSwitch::Measure ---------------------------------------------------
// ToggleSwitch derives from ContentControl but did NOT use MeasureLabelWidth: it
// needs the label's HEIGHT too (h = max(TrackH, label.h)), and the shared helper
// only returned a width. So it kept its own CreateTextLayout call and stayed on the
// throwaway path after Button/CheckBox/RadioButton were fixed. It now calls
// MeasureLabelSize, which caches and returns both dimensions from ONE entry.

TEST(LabelMeasureCache, ToggleSwitchLabelMeasuredOnce) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    ToggleSwitch ts;
    UIContext uictx = MakeCtx(dw, cache);
    ts.AttachToContext(uictx);
    ts.SetText(L"Enable notifications");

    cache.ResetFrameStats();
    ts.Measure(400.0f, 200.0f);
    const uint32_t afterFirst = cache.Stats().misses;
    EXPECT_TRUE(afterFirst >= 1u);      // built once, through the cache

    // A resize drag: the offered width changes every frame. ToggleSwitch measures
    // its label at a FIXED 1000 DIP box (a switch label never wraps), so unlike the
    // Button/CheckBox label key this one does not even vary with the constraint —
    // every frame of the drag must be a pure hit.
    for (float w = 399.0f; w > 360.0f; w -= 1.0f) ts.Measure(w, 200.0f);
    EXPECT_EQ(cache.Stats().misses, afterFirst);  // nothing rebuilt across the drag
    EXPECT_TRUE(cache.Stats().hits >= 39u);       // one hit per frame
}

// The height half of the measurement must survive the move to the cache. This is
// the assertion that fails if MeasureLabelSize is wired to return only a width
// (h left at TrackH()) — a two-line-tall label or a CJK string with tall glyphs
// would then be clipped, which is exactly what the old unbounded-height layout
// existed to prevent.
TEST(LabelMeasureCache, ToggleSwitchTallLabelStillGrowsHeight) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);
    UIContext uictx = MakeCtx(dw, cache);

    // No label: height is the bare track.
    ToggleSwitch bare;
    bare.AttachToContext(uictx);
    bare.Measure(400.0f, 200.0f);
    const float trackOnlyH = bare.Desired().h;
    EXPECT_TRUE(trackOnlyH > 0.0f);

    // A label at a much larger font must push the desired height above the track.
    // If the cached path dropped the height, this stays equal to trackOnlyH.
    ToggleSwitch tall;
    tall.AttachToContext(uictx);
    tall.SetText(L"Tall label with descenders: gjpqy");
    tall.SetFontSize(48.0f);
    tall.Measure(400.0f, 200.0f);
    EXPECT_TRUE(tall.Desired().h > trackOnlyH);
    EXPECT_TRUE(tall.Desired().w > bare.Desired().w);
}

TEST(LabelMeasureCache, ToggleSwitchNoCacheStillMeasures) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ToggleSwitch ts;
    UIContext uictx;
    uictx.dwrite = &dw;   // no resourceCache — the direct-build fallback
    ts.AttachToContext(uictx);
    ts.SetText(L"Fallback");
    ts.Measure(400.0f, 200.0f);
    EXPECT_TRUE(ts.Desired().w > 0.0f);
    EXPECT_TRUE(ts.Desired().h > 0.0f);
}

// --- GroupBox::Measure ------------------------------------------------------
// GroupBox is a FrameworkElement, not a ContentControl, so it had no access to the
// shared label helper and carried its own CreateTextLayout for the header. It is
// also a container, so its Measure runs on every frame of a resize drag by
// definition — one throwaway layout per frame for a string fixed at build time.

TEST(LabelMeasureCache, GroupBoxHeaderMeasuredOnce) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    GroupBox gb;
    UIContext uictx = MakeCtx(dw, cache);
    gb.AttachToContext(uictx);
    gb.SetHeader(L"Connection settings");

    cache.ResetFrameStats();
    gb.Measure(400.0f, 300.0f);
    const uint32_t afterFirst = cache.Stats().misses;
    EXPECT_TRUE(afterFirst >= 1u);

    // Same constraint twice: the second must be served.
    gb.Measure(400.0f, 300.0f);
    EXPECT_EQ(cache.Stats().misses, afterFirst);
    EXPECT_TRUE(cache.Stats().hits >= 1u);
}

// The header key includes the offered width (it IS the layout box), so a drag that
// varies the width legitimately rebuilds — but returning to a width already seen
// must hit rather than rebuild. This pins the key's width component: a key that
// ignored availW would report the same header height at every width, and a key that
// hashed something unstable (a pointer, a frame counter) would never hit at all.
TEST(LabelMeasureCache, GroupBoxHeaderRevisitedWidthHits) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    GroupBox gb;
    UIContext uictx = MakeCtx(dw, cache);
    gb.AttachToContext(uictx);
    gb.SetHeader(L"Advanced");

    gb.Measure(400.0f, 300.0f);
    gb.Measure(360.0f, 300.0f);

    cache.ResetFrameStats();
    gb.Measure(400.0f, 300.0f);   // back to a width already built
    gb.Measure(360.0f, 300.0f);
    EXPECT_EQ(cache.Stats().misses, 0u);
    EXPECT_TRUE(cache.Stats().hits >= 2u);
}

TEST(LabelMeasureCache, GroupBoxChangedHeaderRebuilds) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);

    GroupBox gb;
    UIContext uictx = MakeCtx(dw, cache);
    gb.AttachToContext(uictx);
    gb.SetHeader(L"Short");
    gb.Measure(400.0f, 300.0f);

    cache.ResetFrameStats();
    gb.SetHeader(L"A different header string entirely");
    gb.Measure(400.0f, 300.0f);
    EXPECT_TRUE(cache.Stats().misses >= 1u);   // the new string really was built
}

TEST(LabelMeasureCache, GroupBoxNoCacheStillMeasures) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    GroupBox gb;
    UIContext uictx;
    uictx.dwrite = &dw;   // no resourceCache — the direct-build fallback
    gb.AttachToContext(uictx);
    gb.SetHeader(L"Fallback header");
    gb.Measure(400.0f, 300.0f);
    EXPECT_TRUE(gb.Desired().h > 0.0f);
}
