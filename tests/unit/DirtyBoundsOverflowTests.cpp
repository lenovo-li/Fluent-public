// DirtyBoundsOverflowTests.cpp — controls that paint OUTSIDE their layout bounds
// must report an inflated dirty rect (WP-07 §S4).
//
// Why this file exists: the partial-redraw region is exactly this frame's union of
// CollectDirtyBounds rects. Nothing widens it — the planner used to union in last
// frame's rect, a swap-chain-era leftover, and that is gone. So a control whose
// painted visual exceeds bounds_ (focus ring, a stroke centered on the bounds edge,
// an oversized drag thumb) has to declare it, or two things go wrong: the overflow is
// clipped on the frame it is drawn, and its pixels are never cleared on the frame it
// stops being drawn — visible residue.
//
// The overflow is declared once per control via VisualOverflowDip(), and TWO things
// read it: CollectDirtyBounds (so the redraw region covers the overflow) and
// Panel::Render's cull (so a control whose overflow lands in the dirty region is not
// skipped — the clear would have wiped it). PanelViewportCullingTests covers the cull
// side; this file checks each control's declared overflow against the geometry its
// Render actually paints.
//
// All headless: the overflow is pure geometry off bounds_ + the dirty flags.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/Hyperlink.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/styling/FocusVisual.h"

using namespace fluent;

namespace {
// SetBounds deliberately does NOT set dirty flags (it only fires OnBoundsChanged), so
// a test must dirty the control explicitly. Invalidate() is protected on Visual;
// this exposes it, the same trick PanelViewportCullingTests uses for its spy leaf.
template <class T>
struct Dirtyable : public T {
    using T::Invalidate;
};

// Place a control and mark it dirty — the state in which CollectDirtyBounds reports.
template <class T>
void PlaceAndDirty(Dirtyable<T>& c, const RectDip& bounds) {
    c.SetBounds(bounds);
    c.ClearDirtySubtree();   // drop whatever construction left behind
    c.Invalidate();          // Render-level dirt, as a repaint would set
}

// The single rect a freshly dirtied control reports. Returns an empty rect when the
// control reported nothing (which is itself a failure for these controls).
RectDip SoleDirtyRect(UIElement& e) {
    std::vector<RectDip> out;
    e.CollectDirtyBounds(out);
    if (out.size() != 1) return RectDip{};
    return out[0];
}

// Does `outer` fully contain `inner`?
bool Contains(const RectDip& outer, const RectDip& inner) {
    return outer.x <= inner.x && outer.y <= inner.y &&
           outer.right() >= inner.right() && outer.bottom() >= inner.bottom();
}

// The rect actually touched by a stroked focus ring: the ring rect plus the outward
// half of the centered stroke.
RectDip PaintedRingRect(const RectDip& content, const FocusRingSpec& spec) {
    return content.inflated(spec.inset + spec.strokeWidth * 0.5f);
}
} // namespace

// ---------------------------------------------------------------------------
// RectDip::inflated — the shared helper the overrides are built on
// ---------------------------------------------------------------------------

TEST(DirtyBoundsOverflow, InflatedGrowsAllFourSides) {
    RectDip r{10.0f, 20.0f, 30.0f, 40.0f};
    RectDip g = r.inflated(2.5f);
    EXPECT_NEAR(g.x, 7.5f, 0.001f);
    EXPECT_NEAR(g.y, 17.5f, 0.001f);
    EXPECT_NEAR(g.right(), 42.5f, 0.001f);   // 40 + 2.5
    EXPECT_NEAR(g.bottom(), 62.5f, 0.001f);  // 60 + 2.5
}

// ---------------------------------------------------------------------------
// Per-control: reported rect ⊇ painted focus ring
// ---------------------------------------------------------------------------

TEST(DirtyBoundsOverflow, ButtonCoversItsFocusRing) {
    Dirtyable<Button> b;
    PlaceAndDirty(b, {100.0f, 50.0f, 80.0f, 30.0f});
    RectDip dirty = SoleDirtyRect(b);
    EXPECT_TRUE(!dirty.isEmpty());
    // Button rings bounds_ with the default spec.
    EXPECT_TRUE(Contains(dirty, PaintedRingRect(b.Bounds(), FocusRingSpec{})));
    // ... and is strictly larger than bounds_ (the whole point).
    EXPECT_TRUE(dirty.x < b.Bounds().x);
}

TEST(DirtyBoundsOverflow, CheckBoxCoversRingAroundItsBox) {
    Dirtyable<CheckBox> c;
    // Height 20 == the box edge, the worst case: the ring overhangs top AND bottom.
    PlaceAndDirty(c, {40.0f, 10.0f, 120.0f, 20.0f});
    RectDip dirty = SoleDirtyRect(c);
    EXPECT_TRUE(!dirty.isEmpty());
    // The box is 20x20 at the left edge, vertically centered.
    RectDip box{40.0f, 10.0f, 20.0f, 20.0f};
    FocusRingSpec spec;
    spec.inset = 3.0f;   // CheckBox::kFocusRingInset
    EXPECT_TRUE(Contains(dirty, PaintedRingRect(box, spec)));
}

TEST(DirtyBoundsOverflow, ComboBoxCoversItsFocusRing) {
    Dirtyable<ComboBox> cb;
    PlaceAndDirty(cb, {20.0f, 30.0f, 140.0f, 28.0f});
    RectDip dirty = SoleDirtyRect(cb);
    EXPECT_TRUE(!dirty.isEmpty());
    EXPECT_TRUE(Contains(dirty, PaintedRingRect(cb.Bounds(), FocusRingSpec{})));
}

TEST(DirtyBoundsOverflow, RadioButtonCoversItsFocusCircle) {
    Dirtyable<RadioButton> r;
    // Height == the 20 DIP ring glyph: the focus circle overhangs top and bottom.
    PlaceAndDirty(r, {40.0f, 10.0f, 120.0f, 20.0f});
    RectDip dirty = SoleDirtyRect(r);
    EXPECT_TRUE(!dirty.isEmpty());
    // Circle centered on the glyph (left edge, vertically centered), radius
    // outerR(10) + gap(3), stroke 1.5 → bounding box of the painted ring.
    const float cx = 40.0f + 10.0f, cy = 10.0f + 10.0f;
    const float rad = 10.0f + 3.0f + 0.75f;
    RectDip painted{cx - rad, cy - rad, rad * 2.0f, rad * 2.0f};
    EXPECT_TRUE(Contains(dirty, painted));
}

TEST(DirtyBoundsOverflow, ToggleSwitchCoversItsFocusRing) {
    Dirtyable<ToggleSwitch> t;
    // Height == the 20 DIP track: the ring overhangs top and bottom.
    PlaceAndDirty(t, {40.0f, 10.0f, 120.0f, 20.0f});
    RectDip dirty = SoleDirtyRect(t);
    EXPECT_TRUE(!dirty.isEmpty());
    // Track 40x20 at the left edge, vertically centered; ring gap 3, stroke 1.5.
    RectDip track{40.0f, 10.0f, 40.0f, 20.0f};
    EXPECT_TRUE(Contains(dirty, track.inflated(3.0f + 0.75f)));
}

// The text editors stroke their border ON the bounds edge, so half of it (0.75 DIP
// when focused) lands outside — a smaller overflow than a focus ring, but the same
// residue if unreported.
TEST(DirtyBoundsOverflow, TextBoxCoversItsBorderStrokeOuterHalf) {
    Dirtyable<TextBox> tb;
    PlaceAndDirty(tb, {10.0f, 10.0f, 200.0f, 32.0f});
    RectDip dirty = SoleDirtyRect(tb);
    EXPECT_TRUE(!dirty.isEmpty());
    EXPECT_TRUE(Contains(dirty,
                         tb.Bounds().inflated(TextEditBase::kBorderStrokeMax * 0.5f)));
}

// Slider's thumb grows while dragging and carries its own ring — the pre-existing
// override, re-pinned here so the whole overflow contract lives in one file.
TEST(DirtyBoundsOverflow, SliderCoversItsEnlargedThumb) {
    Dirtyable<Slider> s;
    PlaceAndDirty(s, {20.0f, 100.0f, 200.0f, 12.0f});  // demo height: thumb exceeds it
    RectDip dirty = SoleDirtyRect(s);
    EXPECT_TRUE(!dirty.isEmpty());
    // Thumb at the track's left end, radius kThumbR(8)*1.15 plus a 3.5 ring.
    EXPECT_TRUE(dirty.x < s.Bounds().x - 12.0f);
    EXPECT_TRUE(dirty.y < s.Bounds().y - 12.0f);
}

// ---------------------------------------------------------------------------
// The gate: a clean control reports nothing at all
// ---------------------------------------------------------------------------
// The inflation must not turn a NOT-dirty control into a dirty rect — that would
// repaint the world every frame and silently undo the partial-redraw win.

TEST(DirtyBoundsOverflow, CleanControlsReportNothing) {
    Button b;
    CheckBox c;
    ToggleSwitch t;
    RadioButton r;
    TextBox tb;
    b.SetBounds({0.0f, 0.0f, 80.0f, 30.0f});
    c.SetBounds({0.0f, 40.0f, 80.0f, 20.0f});
    t.SetBounds({0.0f, 70.0f, 80.0f, 20.0f});
    r.SetBounds({0.0f, 100.0f, 80.0f, 20.0f});
    tb.SetBounds({0.0f, 130.0f, 80.0f, 32.0f});
    b.ClearDirtySubtree();
    c.ClearDirtySubtree();
    t.ClearDirtySubtree();
    r.ClearDirtySubtree();
    tb.ClearDirtySubtree();

    std::vector<RectDip> out;
    b.CollectDirtyBounds(out);
    c.CollectDirtyBounds(out);
    t.CollectDirtyBounds(out);
    r.CollectDirtyBounds(out);
    tb.CollectDirtyBounds(out);
    EXPECT_EQ(out.size(), static_cast<size_t>(0));
}

// --- Hyperlink / Expander: two controls that painted outside and declared nothing --
// Both were found by auditing every control that references IsFocused() against the
// list that overrides VisualOverflowDip(). The four other undeclared ones (ListBox,
// TreeView, TabControl, DatePicker) are genuinely safe: each insets its ring INWARD by
// exactly half its stroke, so the outer edge lands on the bounds edge. These two build
// the ring OUTWARD from the bounds edge instead.

// Hyperlink strokes its ring at `bounds_ - 2.0` by hand (custom alpha, so it does not
// go through DrawFocusRing) with a 1.0 centered stroke: 2.5 DIP outside.
TEST(DirtyBoundsOverflow, HyperlinkCoversItsFocusRing) {
    Dirtyable<Hyperlink> h;
    PlaceAndDirty(h, {60.0f, 40.0f, 100.0f, 18.0f});
    RectDip dirty = SoleDirtyRect(h);
    EXPECT_TRUE(!dirty.isEmpty());

    // The ring's own literals, not FocusRingSpec's defaults: Hyperlink does not use
    // the shared spec, and the test must describe what Render actually paints.
    FocusRingSpec asDrawn;
    asDrawn.inset = 2.0f;
    asDrawn.strokeWidth = 1.0f;
    EXPECT_TRUE(Contains(dirty, PaintedRingRect(h.Bounds(), asDrawn)));

    // Strictly larger on every side — a declaration of 0 would still "contain" a
    // zero-inflation rect, so the containment check alone cannot catch the bug.
    EXPECT_TRUE(dirty.x < h.Bounds().x);
    EXPECT_TRUE(dirty.y < h.Bounds().y);
    EXPECT_TRUE(dirty.right() > h.Bounds().right());
    EXPECT_TRUE(dirty.bottom() > h.Bounds().bottom());
}

// Unconditional, not gated on focus: the frame on which focus LEAVES still has to
// clear the pixels the ring occupied on the previous frame, and IsFocused() is
// already false by then. A `IsFocused() ? pad : 0` implementation passes a
// focused-state test and still leaves residue, so this is the assertion that
// distinguishes them.
TEST(DirtyBoundsOverflow, HyperlinkOverflowIsNotGatedOnFocus) {
    Dirtyable<Hyperlink> h;
    PlaceAndDirty(h, {0.0f, 0.0f, 90.0f, 18.0f});
    EXPECT_TRUE(!h.IsFocused());          // never focused: no focus manager here
    EXPECT_TRUE(h.VisualOverflowDip() >= 2.5f);
}

// Expander::Render calls DrawFocusRing(HeaderRect(), ...), and HeaderRect() shares
// the control's left/top/right edges, so the default spec's 2.0 inset + 1.5 centered
// stroke puts the ring 2.75 DIP outside on three sides. This declared 0.0f with a
// comment claiming the ring was stroked "just inside the header row".
TEST(DirtyBoundsOverflow, ExpanderCoversItsHeaderFocusRing) {
    Dirtyable<Expander> e;
    PlaceAndDirty(e, {30.0f, 20.0f, 200.0f, 40.0f});
    RectDip dirty = SoleDirtyRect(e);
    EXPECT_TRUE(!dirty.isEmpty());

    // The header shares three edges with bounds_, so ringing the header spills on
    // those three. Checking against the header rect (not bounds_) is what ties the
    // assertion to the geometry Render passes to DrawFocusRing.
    RectDip header{e.Bounds().x, e.Bounds().y, e.Bounds().w, 40.0f};
    EXPECT_TRUE(Contains(dirty, PaintedRingRect(header, FocusRingSpec{})));
    EXPECT_TRUE(dirty.x < e.Bounds().x);
    EXPECT_TRUE(dirty.y < e.Bounds().y);
    EXPECT_TRUE(dirty.right() > e.Bounds().right());
}

// An invisible control reports nothing even when dirty (it paints no pixels).
TEST(DirtyBoundsOverflow, InvisibleControlReportsNothing) {
    Dirtyable<Button> b;
    PlaceAndDirty(b, {0.0f, 0.0f, 80.0f, 30.0f});
    b.SetVisible(false);
    std::vector<RectDip> out;
    b.CollectDirtyBounds(out);
    EXPECT_EQ(out.size(), static_cast<size_t>(0));
}
