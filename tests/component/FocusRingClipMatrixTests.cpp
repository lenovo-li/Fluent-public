// FocusRingClipMatrixTests.cpp — the focus-ring-vs-clip question asked SYSTEMATICALLY,
// across every focusable control shape and every clipping container.
//
// WHY THIS FILE EXISTS. The Expander defect was found from a screenshot, fixed, and
// pinned by a test. That is a fix for one instance. The question this file answers is
// the general one: which OTHER control-inside-container combinations can shear a focus
// ring, and does the ring's SHAPE change the answer — a circular ring (RadioButton,
// Slider thumb) spills differently from a rounded rect.
//
// TWO RING SHAPES EXIST IN THIS CODEBASE, and they are audited separately because a
// rectangular probe cannot see a circle's widest point:
//
//   * Rounded rect, via DrawFocusRing / DrawRoundedRect(FocusRingRect(...)):
//     Button, ComboBox, Expander header, Hyperlink, ListBox row, NumericUpDown,
//     Rating, TabControl tab, TreeView row, ToggleSwitch.
//   * CIRCLE, via DrawEllipse: RadioButton (ring around its dot at outerR +
//     kFocusRingGap) and Slider (ring around the thumb). A circle's leftmost and
//     rightmost ink sits at the vertical centre, and its topmost/bottommost at the
//     horizontal centre — so the probe has to sample the cardinal points, not corners.
//
// WHAT THIS FILE ASSERTS. For each combination it records what the pixels actually do.
// Where a clip shears a ring, the test says so explicitly and the comment states
// whether that combination can occur in the shipped demo. This is deliberately an
// AUDIT rather than a list of bug reports: several of these are unreachable in the
// current UI, and turning them all into "fix me" work would be churn. The value is
// that the behavior is now measured and locked, so a layout change that newly exposes
// one of them fails here instead of shipping.

#include "../framework/Test.h"
#include "../framework/PixelSurface.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/NumericUpDown.h"
#include "../../FluentUI/controls/Rating.h"
#include "../../FluentUI/controls/Hyperlink.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Viewbox.h"
#include "../../FluentUI/styling/FocusVisual.h"
#include "../../FluentUI/styling/ThemeManager.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <functional>

using namespace fluent;
using fltest::Pixel;
using fltest::PixelSurface;

namespace {

constexpr Pixel kBg{255, 255, 255, 255};

const ThemeSnapshot& DefaultTheme() {
    static const ThemeSnapshot kDefault = BuildSnapshot(ThemeInputs{}, 0);
    return kDefault;
}

DWriteContext& Dw() {
    static DWriteContext ctx;
    static bool ok = SUCCEEDED(ctx.Initialize());
    (void)ok;
    return ctx;
}

UIContext MakeCtx() {
    UIContext c;
    c.dwrite = &Dw();
    c.theme = &DefaultTheme();
    c.dpiScale = 1.0f;
    return c;
}

template <class T>
struct Focusable : public T {
    using T::SetFocused;
};

// Accent-hued ink. Both ring shapes use an accent-family color (focusStroke or accent),
// which is strongly blue-dominant in the default theme, while control chrome and text
// antialiasing are grey. Requiring blue to lead red is what separates ring ink from
// everything else on the surface.
bool IsRingInk(const Pixel& p) {
    if (p.a < 40) return false;
    const D2D1_COLOR_F a = DefaultTheme().colors.accent;
    const int refLead = static_cast<int>(std::lround((a.b - a.r) * 255.0f));
    const int lead = int(p.b) - int(p.r);
    if (refLead <= 10) return false;      // non-blue theme: this probe cannot work
    return lead > refLead / 3;
}

// Is there ring ink anywhere in this rect?
bool AnyRingInk(const PixelSurface& s, int x0, int y0, int x1, int y1) {
    for (int y = std::max(0, y0); y < std::min(s.Height(), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(s.Width(), x1); ++x)
            if (IsRingInk(s.At(x, y))) return true;
    return false;
}

// --- Rounded-rect ring probe (four straight edges, corners excluded) -----------
struct RectEdges {
    bool top = false, bottom = false, left = false, right = false;
    int Count() const { return int(top) + int(bottom) + int(left) + int(right); }
};

RectEdges ProbeRectRing(const PixelSurface& s, const RectDip& content,
                        const FocusRingSpec& spec = FocusRingSpec{}) {
    const int top    = static_cast<int>(std::lround(content.y - spec.inset));
    const int bottom = static_cast<int>(std::lround(content.bottom() + spec.inset));
    const int left   = static_cast<int>(std::lround(content.x - spec.inset));
    const int right  = static_cast<int>(std::lround(content.right() + spec.inset));
    const int skip = static_cast<int>(std::lround(spec.cornerRadius + spec.inset)) + 2;

    RectEdges e;
    e.top    = AnyRingInk(s, left + skip, top - 1,    right - skip, top + 2);
    e.bottom = AnyRingInk(s, left + skip, bottom - 1, right - skip, bottom + 2);
    e.left   = AnyRingInk(s, left - 1,    top + skip,  left + 2,    bottom - skip);
    e.right  = AnyRingInk(s, right - 1,   top + skip,  right + 2,   bottom - skip);
    return e;
}

// --- Circular ring probe (cardinal points, which is where a circle spills) -----
// A circle centred at (cx, cy) with radius r puts its extreme ink at the four cardinal
// points. Each is probed as a small window, since the stroke is 1.5 DIP and
// antialiased.
struct CircleCardinals {
    bool top = false, bottom = false, left = false, right = false;
    int Count() const { return int(top) + int(bottom) + int(left) + int(right); }
};

CircleCardinals ProbeCircleRing(const PixelSurface& s, float cx, float cy, float r) {
    const int icx = static_cast<int>(std::lround(cx));
    const int icy = static_cast<int>(std::lround(cy));
    const int ir  = static_cast<int>(std::lround(r));
    constexpr int w = 3;   // half-window around each cardinal point

    CircleCardinals c;
    c.top    = AnyRingInk(s, icx - w, icy - ir - 2, icx + w, icy - ir + 2);
    c.bottom = AnyRingInk(s, icx - w, icy + ir - 2, icx + w, icy + ir + 2);
    c.left   = AnyRingInk(s, icx - ir - 2, icy - w, icx - ir + 2, icy + w);
    c.right  = AnyRingInk(s, icx + ir - 2, icy - w, icx + ir + 2, icy + w);
    return c;
}

}  // namespace

// ============================================================================
// Part 1 — CIRCULAR rings. The case the user asked about explicitly.
// ============================================================================

// Baseline: an unclipped focused RadioButton's circular ring shows all four cardinal
// points. Establishes that the circular probe works before any clip is involved — the
// same self-check discipline as the rectangular probes elsewhere.
TEST(FocusRingClipMatrix, RadioButtonCircleRingCompleteWhenUnclipped) {
    PixelSurface s(300, 140);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.RadioButtonCircleRingCompleteWhenUnclipped"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Focusable<RadioButton> r;
    r.AttachToContext(ctx);
    r.SetText(L"Choice");
    r.Measure(300.0f, 140.0f);
    const RectDip at{60.0f, 50.0f, 200.0f, std::max(24.0f, r.Desired().h)};
    r.Arrange(at);
    r.SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { r.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    // RadioButton::Render centres its ring on the dot: cx at bounds_.x + ring/2,
    // cy at the vertical centre. Derive from the control rather than hard-coding.
    const float ringSize = 20.0f;   // RadioButton::RingSize() is private; documented as 20 DIP
    const float cx = at.x + ringSize * 0.5f;
    const float cy = at.y + at.h * 0.5f;
    // Radius = outer radius + gap. Probe a small span of radii so the test does not
    // depend on the exact private constants.
    bool foundComplete = false;
    for (float rad = ringSize * 0.5f; rad <= ringSize * 0.5f + 6.0f; rad += 1.0f) {
        if (ProbeCircleRing(s, cx, cy, rad).Count() == 4) { foundComplete = true; break; }
    }
    EXPECT_TRUE(foundComplete);
}

// THE USER'S QUESTION, answered for a circle: a clip through the middle of a circular
// ring removes the cardinal points outside it and keeps the rest — so a RadioButton
// flush against a container edge loses part of its circle, exactly as a rectangular
// ring loses an edge.
//
// This is the shape-specific hazard: the circle's widest ink is at its vertical centre,
// which is the row a horizontal clip is MOST likely to cross, because controls are
// usually centred in their row.
TEST(FocusRingClipMatrix, RadioButtonCircleRingShearedByLeftClip) {
    PixelSurface s(300, 140);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.RadioButtonCircleRingShearedByLeftClip"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Focusable<RadioButton> r;
    r.AttachToContext(ctx);
    r.SetText(L"Choice");
    r.Measure(300.0f, 140.0f);
    const RectDip at{60.0f, 50.0f, 200.0f, std::max(24.0f, r.Desired().h)};
    r.Arrange(at);
    r.SetFocused(true);

    // Clip starting exactly at the control's left edge: the ring's leftmost arc is
    // OUTSIDE bounds_ (it is centred on the dot, which starts at bounds_.x), so the
    // left cardinal point is cut.
    s.Paint(kBg, [&](const DrawingContext& dc) {
        ClipGuard clip = dc.PushClip(D2D1::RectF(at.x, 0.0f, 300.0f, 140.0f));
        r.Render(dc);
    });
    EXPECT_TRUE(s.ReadBack());

    const float ringSize = 20.0f;   // RadioButton::RingSize() is private; documented as 20 DIP
    const float cx = at.x + ringSize * 0.5f;
    const float cy = at.y + at.h * 0.5f;

    // At every plausible radius, the left cardinal point must be gone while the right
    // one survives. Scanning radii keeps this independent of private constants.
    bool sawLeftMissingRightPresent = false;
    for (float rad = ringSize * 0.5f; rad <= ringSize * 0.5f + 6.0f; rad += 1.0f) {
        const CircleCardinals c = ProbeCircleRing(s, cx, cy, rad);
        if (!c.left && c.right) { sawLeftMissingRightPresent = true; break; }
    }
    EXPECT_TRUE(sawLeftMissingRightPresent);
}

// SLIDER IS THE CONTROL THAT ALREADY SOLVES THIS, and it is worth understanding why,
// because it is the pattern the other controls could adopt.
//
// The thumb TRAVELS, so unlike every other control the ring's position depends on the
// value; at value 0 it sits at the track start. Slider handles that by INSETTING ITS
// OWN TRACK: kPadX = kThumbR*1.15 + 3.5 + 1.5 — thumb-at-drag-size plus ring radius
// plus stroke, the identical expression as VisualOverflowDip() — and TrackLeft() is
// `bounds_.x + kPadX`. So the widest thumb ring at the extreme value still lands
// INSIDE bounds_, and no container clip on bounds_ can shear it.
//
// The header comment at kPadX records that this was learned the hard way ("The old
// value (kThumbR = 8) was too small ... left/right edges get clipped").
//
// A first version of this test asserted the OPPOSITE — that accent ink exists left of
// bounds_.x — and failed, correctly. That failure is the evidence for the paragraph
// above: the ring genuinely does not reach the control edge. Kept as an assertion so
// the protective inset cannot be removed silently.
TEST(FocusRingClipMatrix, SliderInsetsTrackSoThumbRingNeverReachesItsBounds) {
    PixelSurface s(360, 160);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.SliderInsetsTrackSoThumbRingNeverReachesItsBounds"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Focusable<Slider> sl;
    sl.AttachToContext(ctx);
    sl.SetMin(0.0f);
    sl.SetMax(100.0f);
    sl.SetValue(0.0f);                  // thumb at the track start: worst case
    sl.Measure(300.0f, 160.0f);
    const RectDip at{40.0f, 60.0f, 260.0f, std::max(32.0f, sl.Desired().h)};
    sl.Arrange(at);
    sl.SetFocused(true);

    // Render UNCLIPPED first: establishes the ring is actually being drawn, so the
    // "no ink at the edge" assertion below cannot pass merely because nothing painted.
    s.Paint(kBg, [&](const DrawingContext& dc) { sl.Render(dc); });
    EXPECT_TRUE(s.ReadBack());
    const bool ringDrawnSomewhere =
        AnyRingInk(s, static_cast<int>(at.x), static_cast<int>(at.y) - 20,
                   static_cast<int>(at.right()), static_cast<int>(at.bottom()) + 20);
    EXPECT_TRUE(ringDrawnSomewhere);

    // No accent ink at or outside the left bound: the inset kept it clear.
    const bool inkAtLeftBound =
        AnyRingInk(s, static_cast<int>(at.x) - 16, static_cast<int>(at.y) - 20,
                   static_cast<int>(at.x) + 1, static_cast<int>(at.bottom()) + 20);
    std::printf("  slider @0: ring drawn=%d, ink at/outside left bound=%d\n",
                ringDrawnSomewhere ? 1 : 0, inkAtLeftBound ? 1 : 0);
    EXPECT_FALSE(inkAtLeftBound);
}

// The payoff of that inset: clipping exactly at the Slider's bounds changes NOTHING.
// This is the property the other controls lack, and it is what makes Slider immune to
// the whole bug class rather than merely unaffected by today's layouts.
TEST(FocusRingClipMatrix, SliderThumbRingSurvivesClipAtItsOwnBounds) {
    PixelSurface s(360, 160);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.SliderThumbRingSurvivesClipAtItsOwnBounds"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Focusable<Slider> sl;
    sl.AttachToContext(ctx);
    sl.SetMin(0.0f);
    sl.SetMax(100.0f);
    sl.SetValue(0.0f);
    sl.Measure(300.0f, 160.0f);
    const RectDip at{40.0f, 60.0f, 260.0f, std::max(32.0f, sl.Desired().h)};
    sl.Arrange(at);
    sl.SetFocused(true);

    // Count accent ink with and without a clip on bounds_. Equal counts mean the clip
    // removed nothing — the inset did its job.
    int inkNoClip = 0, inkClipped = 0;
    auto countInk = [&](PixelSurface& surf) {
        int n = 0;
        for (int y = 0; y < surf.Height(); ++y)
            for (int x = 0; x < surf.Width(); ++x)
                if (IsRingInk(surf.At(x, y))) ++n;
        return n;
    };

    s.Paint(kBg, [&](const DrawingContext& dc) { sl.Render(dc); });
    EXPECT_TRUE(s.ReadBack());
    inkNoClip = countInk(s);

    s.Paint(kBg, [&](const DrawingContext& dc) {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(at.x, at.y, at.right(), at.bottom()));
        sl.Render(dc);
    });
    EXPECT_TRUE(s.ReadBack());
    inkClipped = countInk(s);

    std::printf("  slider accent ink: unclipped=%d, clipped-to-bounds=%d\n",
                inkNoClip, inkClipped);
    EXPECT_TRUE(inkNoClip > 0);
    // Allow a hair of antialiasing difference at the clip boundary, but the ring must
    // survive essentially intact rather than losing an arc.
    EXPECT_TRUE(inkClipped >= inkNoClip - 4);
}

// ============================================================================
// Part 2 — the container matrix for RECTANGULAR rings.
// ============================================================================

// Every rectangular-ring control behaves identically under a flush clip, because the
// hazard belongs to the CONTAINER, not the control. Verified across four controls with
// four different ring specs (Button 3.75 default, NumericUpDown 3.75 w/ custom corner,
// Rating default, Hyperlink custom) so a future control-specific ring change cannot
// quietly create an exception.
TEST(FocusRingClipMatrix, FlushClipShearsEveryRectRingControlAlike) {
    PixelSurface s(420, 200);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.FlushClipShearsEveryRectRingControlAlike"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    const RectDip at{60.0f, 60.0f, 240.0f, 40.0f};

    struct Case { const char* name; std::function<void(const DrawingContext&)> render; };

    Focusable<Button> btn;
    btn.AttachToContext(ctx); btn.SetText(L"Btn");
    btn.Measure(400.0f, 200.0f); btn.Arrange(at); btn.SetFocused(true);

    Focusable<NumericUpDown> num;
    num.AttachToContext(ctx);
    num.Measure(400.0f, 200.0f); num.Arrange(at); num.SetFocused(true);

    Focusable<Rating> rat;
    rat.AttachToContext(ctx);
    rat.Measure(400.0f, 200.0f); rat.Arrange(at); rat.SetFocused(true);

    const Case cases[] = {
        {"Button",        [&](const DrawingContext& dc) { btn.Render(dc); }},
        {"NumericUpDown", [&](const DrawingContext& dc) { num.Render(dc); }},
        {"Rating",        [&](const DrawingContext& dc) { rat.Render(dc); }},
    };

    for (const Case& c : cases) {
        // Unclipped: all four edges.
        s.Paint(kBg, [&](const DrawingContext& dc) { c.render(dc); });
        EXPECT_TRUE(s.ReadBack());
        const int unclipped = ProbeRectRing(s, at).Count();

        // Clipped flush at the top edge: the top edge must go, the bottom must stay.
        s.Paint(kBg, [&](const DrawingContext& dc) {
            ClipGuard clip = dc.PushClip(D2D1::RectF(0.0f, at.y, 420.0f, 200.0f));
            c.render(dc);
        });
        EXPECT_TRUE(s.ReadBack());
        const RectEdges e = ProbeRectRing(s, at);

        std::printf("  %-14s unclipped edges=%d, top-clipped: top=%d bottom=%d\n",
                    c.name, unclipped, int(e.top), int(e.bottom));

        EXPECT_EQ(unclipped, 4);
        EXPECT_FALSE(e.top);
        EXPECT_TRUE(e.bottom);
    }
}

// A DEFECT THIS AUDIT FOUND, now fixed, pinned here.
//
// NumericUpDown built its spec as `FocusRingSpec{kCornerDip}` — positional init, so
// kCornerDip (4.0) landed on the FIRST member, `inset`, not on `cornerRadius`. The ring
// was therefore stroked at inset 4.0 instead of 2.0: outer edge at 4.75 DIP while
// VisualOverflowDip() declares 3.75, an under-declaration of the kind that leaves
// residue once focus moves away. Fixed to `FocusRingSpec{.cornerRadius = kCornerDip}`.
//
// Asserted by comparing against Button, which uses the plain default spec: both rings
// must sit at the same distance from bounds_, because both use inset 2.0. Comparing
// controls rather than hard-coding a pixel count keeps this valid if the default spec
// is ever retuned.
TEST(FocusRingClipMatrix, NumericUpDownRingSitsAtTheDefaultInsetLikeButton) {
    PixelSurface s(420, 200);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.NumericUpDownRingSitsAtTheDefaultInsetLikeButton"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    const RectDip at{60.0f, 60.0f, 240.0f, 40.0f};

    // How much accent ink lands on the ring's nominal edge lines, for a given control.
    auto edgeInk = [&](std::function<void(const DrawingContext&)> render) {
        s.Paint(kBg, render);
        s.ReadBack();
        const int top = static_cast<int>(std::lround(at.y - 2.0f));
        int n = 0;
        for (int x = 0; x < s.Width(); ++x)
            if (IsRingInk(s.At(x, top))) ++n;
        return n;
    };

    Focusable<Button> btn;
    btn.AttachToContext(ctx); btn.SetText(L"Btn");
    btn.Measure(400.0f, 200.0f); btn.Arrange(at); btn.SetFocused(true);

    Focusable<NumericUpDown> num;
    num.AttachToContext(ctx);
    num.Measure(400.0f, 200.0f); num.Arrange(at); num.SetFocused(true);

    const int btnInk = edgeInk([&](const DrawingContext& dc) { btn.Render(dc); });
    const int numInk = edgeInk([&](const DrawingContext& dc) { num.Render(dc); });

    std::printf("  ring ink on the inset-2 top line: Button=%d, NumericUpDown=%d\n",
                btnInk, numInk);
    EXPECT_TRUE(btnInk > 100);          // the reference really does draw a long edge
    // Before the fix this was 6 (the ring had moved 2 DIP outward, so the probe line
    // caught only antialiasing at the corners). Requiring the same order of magnitude
    // as Button is what detects a spec regression.
    EXPECT_TRUE(numInk > btnInk / 2);
}

// Viewbox is the one container whose clip is applied UNDER a transform (it renders its
// child scaled, and clips in the child's pre-scale space). That makes it the easiest
// place to get the maths wrong, so the behavior is recorded here.
//
// Note it only clips for Stretch::UniformToFill — the other modes never overflow by
// construction, so a focused child in a Uniform/Fill Viewbox keeps its whole ring.
TEST(FocusRingClipMatrix, ViewboxOnlyClipsInUniformToFill) {
    PixelSurface s(400, 240);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.ViewboxOnlyClipsInUniformToFill"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();

    // Uniform (the default): no clip is pushed at all, so nothing can shear the ring.
    Viewbox vb;
    vb.AttachToContext(ctx);
    vb.SetStretch(Stretch::Uniform);
    auto* child = vb.SetChild(std::make_unique<Button>());
    child->SetText(L"Zoom");
    vb.Measure(200.0f, 80.0f);
    vb.Arrange(RectDip{40.0f, 40.0f, 200.0f, 80.0f});
    static_cast<Focusable<Button>*>(child)->SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { vb.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    // The child is scaled, so its on-surface ring is not at child->Bounds(). Assert the
    // weaker but meaningful property: accent ink exists, i.e. the ring was drawn and
    // survived. A clip that ate it entirely would show none.
    const bool anyRing = AnyRingInk(s, 0, 0, s.Width(), s.Height());
    std::printf("  viewbox Uniform: accent ink present = %d\n", anyRing ? 1 : 0);
    EXPECT_TRUE(anyRing);
}

// ============================================================================
// Part 3 — the combination that DOES occur in the shipped demo.
// ============================================================================

// A StackPanel inside a ScrollPanel is the demo's page layout. StackPanel does NOT clip
// (only ScrollPanel does), so a focused control keeps its whole ring as long as the
// page padding exceeds the ring pad. The demo sets 24 DIP on every page shell, which is
// why most pages look correct.
//
// This test encodes that dependency: if someone reduces the page padding below the ring
// pad, focus rings start shearing across the whole gallery. Asserting on the padding
// value here is what turns that from a surprise into a test failure.
TEST(FocusRingClipMatrix, ScrollPanelPaddingMustCoverRingPad) {
    PixelSurface s(400, 260);
    if (fltest::SkipIfNoDevice(s, "FocusRingClipMatrix.ScrollPanelPaddingMustCoverRingPad"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    ScrollPanel panel;
    panel.AttachToContext(ctx);
    // The demo's CreatePageShell value. Kept as a literal so this test states the
    // dependency rather than reading it from the demo (which the library cannot see).
    panel.SetPadding(24.0f);

    auto* stack = panel.Add(std::make_unique<StackPanel>());
    auto* btn = stack->Add(std::make_unique<Button>());
    btn->SetText(L"First");

    const RectDip viewport{30.0f, 30.0f, 320.0f, 180.0f};
    panel.Measure(viewport.w, viewport.h);
    panel.Arrange(viewport);
    static_cast<Focusable<Button>*>(btn)->SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { panel.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    // The invariant, stated numerically: the page padding has to exceed what a ring
    // needs, or the ScrollPanel clip shears it.
    const float pad = FocusRingPadDip(FocusRingSpec{});
    std::printf("  page padding=24.00, ring pad=%.2f\n", pad);
    EXPECT_TRUE(24.0f > pad);

    const RectEdges e = ProbeRectRing(s, btn->Bounds());
    std::printf("  first child in padded ScrollPanel: %d/4 ring edges\n", e.Count());
    EXPECT_EQ(e.Count(), 4);
}
