// ClippedFocusRingTests.cpp — real controls, focused, rendered inside the real
// containers, with the focus ring counted in PIXELS.
//
// This is the file that reproduces the user's screenshots. FocusRingPixelTests proves
// the harness and the mechanism in isolation (a bare DrawFocusRing under a ClipGuard);
// this one wires up the actual control, the actual container, and the actual
// Render call chain, so a failure here names a product defect rather than a property
// of D2D clipping.
//
// THE MECHANISM, STATED ONCE. A focus ring is drawn OUTSIDE the focused control's
// bounds — FocusRingRect inflates by spec.inset (2.0) and strokes 1.5 centered, so
// ink lands up to 2.75 DIP beyond bounds_. Containers that clip their content to
// their own bounds (ScrollPanel, Expander's reveal clip, ListBox, TabControl's strip)
// push a ClipGuard before rendering children. A child sitting flush against the
// container's content edge therefore has the outward part of its ring cut off: the
// ring is emitted correctly, declared correctly in VisualOverflowDip, and still never
// reaches the surface. That is invisible to every geometry-only test.
//
// WHAT THESE TESTS ASSERT, AND WHAT THEY DELIBERATELY DO NOT. They assert which ring
// edges reach the surface. They do NOT assert that the current behavior is correct —
// several of them record the CURRENT behavior with an explicit comment saying whether
// it matches the screenshots. Where the behavior is wrong, the test is written to
// fail if it silently changes, so the decision about the fix stays with a human. Read
// each test's comment; do not assume a passing test here means the UI looks right.

#include "../framework/Test.h"
#include "../framework/PixelSurface.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/styling/FocusVisual.h"
#include "../../FluentUI/styling/ThemeManager.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace fluent;
using fltest::Pixel;
using fltest::PixelSurface;

namespace {

constexpr Pixel kBg{255, 255, 255, 255};

const ThemeSnapshot& DefaultTheme() {
    static const ThemeSnapshot kDefault = BuildSnapshot(ThemeInputs{}, 0);
    return kDefault;
}

Pixel RingPixel() {
    const D2D1_COLOR_F c = DefaultTheme().colors.focusStroke;
    Pixel p;
    p.r = static_cast<uint8_t>(std::lround(std::clamp(c.r, 0.0f, 1.0f) * 255.0f));
    p.g = static_cast<uint8_t>(std::lround(std::clamp(c.g, 0.0f, 1.0f) * 255.0f));
    p.b = static_cast<uint8_t>(std::lround(std::clamp(c.b, 0.0f, 1.0f) * 255.0f));
    p.a = static_cast<uint8_t>(std::lround(std::clamp(c.a, 0.0f, 1.0f) * 255.0f));
    return p;
}

// Ring ink detection. A control's own fill also differs from the page background, so
// "moved away from the background" is not enough here — the probe must match the ring
// HUE. Compare against the ring color with a generous tolerance (antialiased coverage
// over an unknown backdrop) and reject anything closer to a grey.
bool IsRingInk(const Pixel& px) {
    const Pixel ring = RingPixel();
    if (px.a < 40) return false;
    // The accent is strongly blue-dominant; a grey chrome fill is not. Requiring the
    // blue channel to lead red by a real margin is what separates ring ink from the
    // control's own border and from text antialiasing.
    const int blueLead = int(px.b) - int(px.r);
    const int ringLead = int(ring.b) - int(ring.r);
    if (ringLead <= 10) return px.Near(ring, 40);   // non-blue theme: fall back
    return blueLead > ringLead / 3;
}

bool RowHasRing(const PixelSurface& s, int y, int x0, int x1) {
    for (int yy = y - 1; yy <= y + 1; ++yy)
        for (int x = x0; x <= x1; ++x)
            if (IsRingInk(s.At(x, yy))) return true;
    return false;
}

bool ColHasRing(const PixelSurface& s, int x, int y0, int y1) {
    for (int xx = x - 1; xx <= x + 1; ++xx)
        for (int y = y0; y <= y1; ++y)
            if (IsRingInk(s.At(xx, y))) return true;
    return false;
}

struct RingEdges {
    bool top = false, bottom = false, left = false, right = false;
    int Count() const { return int(top) + int(bottom) + int(left) + int(right); }
};

// Probe the ring around `content`. The probe lines sit at the ring centerline
// (content inflated by inset); corners are skipped so no edge can be vouched for by
// its neighbour's rounded corner.
RingEdges ProbeRing(const PixelSurface& s, const RectDip& content,
                    const FocusRingSpec& spec = FocusRingSpec{}) {
    const int top    = static_cast<int>(std::lround(content.y - spec.inset));
    const int bottom = static_cast<int>(std::lround(content.bottom() + spec.inset));
    const int left   = static_cast<int>(std::lround(content.x - spec.inset));
    const int right  = static_cast<int>(std::lround(content.right() + spec.inset));
    const int skip = static_cast<int>(std::lround(spec.cornerRadius + spec.inset)) + 2;

    RingEdges e;
    e.top    = RowHasRing(s, top,    left + skip, right - skip);
    e.bottom = RowHasRing(s, bottom, left + skip, right - skip);
    e.left   = ColHasRing(s, left,   top + skip,  bottom - skip);
    e.right  = ColHasRing(s, right,  top + skip,  bottom - skip);
    return e;
}

// A minimal live context: DWrite only. No window, no composition, no focus manager —
// focus is set directly with SetFocused, which is what FocusManager does to the
// element anyway (see FocusManagerTests.SetFocusIsSingleTruth).
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

// Expose SetFocused, which is protected on UIElement for good reason (FocusManager is
// the single authority) but is exactly the state a render test needs.
template <class T>
struct Focusable : public T {
    using T::SetFocused;
};

}  // namespace

// --- Baseline: an unclipped focused Button shows all four edges ---------------
// If this fails, nothing else in the file means anything: it would mean the probe
// cannot see a real control's real ring, as opposed to a synthetic one.
TEST(ClippedFocusRing, UnclippedButtonShowsAllFourEdges) {
    PixelSurface s(300, 160);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.UnclippedButtonShowsAllFourEdges"))
        return;
    if (!Dw().Valid()) return;

    Focusable<Button> b;
    UIContext ctx = MakeCtx();
    b.AttachToContext(ctx);
    b.SetText(L"OK");
    const RectDip bounds{60.0f, 50.0f, 140.0f, 40.0f};
    b.Measure(300.0f, 160.0f);
    b.Arrange(bounds);
    b.SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { b.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const RingEdges e = ProbeRing(s, b.Bounds());
    EXPECT_TRUE(e.top);
    EXPECT_TRUE(e.bottom);
    EXPECT_TRUE(e.left);
    EXPECT_TRUE(e.right);
}

// --- The screenshot case: a focused control flush against a ScrollPanel edge ---
// ScrollPanel::Render pushes a ClipGuard on its own bounds before rendering children
// (ScrollPanel.cpp:299) — correct in itself, it is what stops scrolled content
// bleeding out of the viewport. A child arranged flush against the top of that
// viewport has its ring's upper edge cut off.
//
// This documents CURRENT behavior and it MATCHES the user's screenshots (the missing
// top/bottom edges on controls at a container boundary). It is a real defect: a
// keyboard user tabbing to the first row of a scroll area sees a broken ring.
TEST(ClippedFocusRing, ScrollPanelClipCutsRingOfChildFlushWithTopEdge) {
    PixelSurface s(320, 220);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ScrollPanelClipCutsRingOfChildFlushWithTopEdge"))
        return;
    if (!Dw().Valid()) return;

    ScrollPanel panel;
    UIContext ctx = MakeCtx();
    panel.AttachToContext(ctx);

    auto* btn = panel.Add(std::make_unique<Button>());
    btn->SetText(L"First");

    const RectDip viewport{40.0f, 40.0f, 240.0f, 140.0f};
    panel.Measure(viewport.w, viewport.h);
    panel.Arrange(viewport);

    // Force the child flush with the viewport's top edge, which is where a scrolled
    // first row sits. Arrange directly so the test does not depend on ScrollPanel's
    // internal padding.
    const RectDip childBounds{viewport.x + 20.0f, viewport.y, 180.0f, 40.0f};
    btn->Arrange(childBounds);
    static_cast<Focusable<Button>*>(btn)->SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { panel.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const RingEdges e = ProbeRing(s, childBounds);
    // The top edge lies above the viewport and is clipped away.
    EXPECT_FALSE(e.top);
    // The other three are inside the clip and survive — which is precisely what
    // makes the artifact look like "the ring lost one side" rather than "no focus".
    EXPECT_TRUE(e.bottom);
    EXPECT_TRUE(e.left);
    EXPECT_TRUE(e.right);
}

// The same container, the same control, moved inward by more than the ring pad:
// every edge survives. This is the control test that proves the previous failure is
// about the CLIP EDGE and not about ScrollPanel rendering children wrongly in
// general — and it is the assertion that will start failing if someone "fixes" the
// clip by removing it, since content would then bleed and this test's neighbours in
// ScrollPanelTests would catch that.
TEST(ClippedFocusRing, ScrollPanelChildInsetByRingPadKeepsAllEdges) {
    PixelSurface s(320, 220);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ScrollPanelChildInsetByRingPadKeepsAllEdges"))
        return;
    if (!Dw().Valid()) return;

    ScrollPanel panel;
    UIContext ctx = MakeCtx();
    panel.AttachToContext(ctx);

    auto* btn = panel.Add(std::make_unique<Button>());
    btn->SetText(L"Inset");

    const RectDip viewport{40.0f, 40.0f, 240.0f, 140.0f};
    panel.Measure(viewport.w, viewport.h);
    panel.Arrange(viewport);

    // Inset by more than FocusRingPadDip (2 + 0.75 + 1 = 3.75) on every side.
    const float pad = FocusRingPadDip(FocusRingSpec{}) + 2.0f;
    const RectDip childBounds{viewport.x + pad, viewport.y + pad, 160.0f, 40.0f};
    btn->Arrange(childBounds);
    static_cast<Focusable<Button>*>(btn)->SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { panel.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    EXPECT_EQ(ProbeRing(s, childBounds).Count(), 4);
}

// The bottom edge, for the same reason — this is the signature in the "Show menu"
// button screenshot (three edges drawn, bottom missing, forming an inverted U).
TEST(ClippedFocusRing, ScrollPanelClipCutsRingOfChildFlushWithBottomEdge) {
    PixelSurface s(320, 220);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ScrollPanelClipCutsRingOfChildFlushWithBottomEdge"))
        return;
    if (!Dw().Valid()) return;

    ScrollPanel panel;
    UIContext ctx = MakeCtx();
    panel.AttachToContext(ctx);

    auto* btn = panel.Add(std::make_unique<Button>());
    btn->SetText(L"Last");

    const RectDip viewport{40.0f, 40.0f, 240.0f, 140.0f};
    panel.Measure(viewport.w, viewport.h);
    panel.Arrange(viewport);

    const RectDip childBounds{viewport.x + 20.0f, viewport.bottom() - 40.0f,
                              180.0f, 40.0f};
    btn->Arrange(childBounds);
    static_cast<Focusable<Button>*>(btn)->SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { panel.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const RingEdges e = ProbeRing(s, childBounds);
    EXPECT_FALSE(e.bottom);   // below the viewport: clipped
    EXPECT_TRUE(e.top);
    EXPECT_TRUE(e.left);
    EXPECT_TRUE(e.right);
}

// A CheckBox rather than a Button, to show the defect belongs to the container and
// not to one control's ring spec. CheckBox declares 4.75 overflow (a bigger inset),
// so if the clip were somehow honoring the declaration this would behave
// differently — it does not, which confirms VisualOverflowDip has no influence on
// clipping at all. It governs the redraw region only.
TEST(ClippedFocusRing, ClipIgnoresDeclaredOverflowForCheckBoxToo) {
    PixelSurface s(320, 200);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ClipIgnoresDeclaredOverflowForCheckBoxToo"))
        return;
    if (!Dw().Valid()) return;

    ScrollPanel panel;
    UIContext ctx = MakeCtx();
    panel.AttachToContext(ctx);

    auto* cb = panel.Add(std::make_unique<CheckBox>());
    cb->SetText(L"Enable");

    const RectDip viewport{30.0f, 30.0f, 260.0f, 140.0f};
    panel.Measure(viewport.w, viewport.h);
    panel.Arrange(viewport);

    const RectDip childBounds{viewport.x + 15.0f, viewport.y, 200.0f, 24.0f};
    cb->Arrange(childBounds);
    static_cast<Focusable<CheckBox>*>(cb)->SetFocused(true);

    s.Paint(kBg, [&](const DrawingContext& dc) { panel.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    // The CheckBox ring is drawn around its BOX, not its whole bounds, so probe the
    // declared overflow band above the control instead of a full ring: the question
    // is only whether ink exists above the viewport's top edge, and it must not,
    // because the clip removed it.
    bool inkAboveViewport = false;
    for (int y = static_cast<int>(viewport.y) - 5;
         y < static_cast<int>(viewport.y); ++y)
        for (int x = static_cast<int>(childBounds.x) - 5;
             x < static_cast<int>(childBounds.right()); ++x)
            if (IsRingInk(s.At(x, y))) inkAboveViewport = true;

    EXPECT_FALSE(inkAboveViewport);
}

// --- Expander: the defect from the bug report, now FIXED ---------------------
// This is the one the screenshots pinned down hardest ("Option 2 只画出了顶边").
//
// Expander::Arrange hands the content the FULL width, and single-child containers in
// this framework do not honour a child's Margin (Border / GroupBox / Viewbox /
// Expander all behave this way — the demo's Margin(12) on the content StackPanel has
// no effect on the Expander's own clip). So a Button inside is flush left, flush
// right, and the LAST one is flush with the bottom of the content rect. The reveal
// clip used to be exactly that rect, so three of the four ring edges were cut.
//
// Measured before the fix, on geometry matching the Gallery's "Initially expanded"
// card: clip x 50..450 / y 68..140, button at x 50..450 / y 108..140.
//
// The fix inflates the clip by FocusRingPadDip on the sides and top, and on the
// bottom too once the reveal has settled. These tests assert the FIXED behavior, so
// they fail if the clip is narrowed back.
namespace {

// Build a fully-open Expander with two buttons, mirroring the demo page. Returns the
// second (bottom-most) button, which is the one flush with the content bottom.
struct ExpanderFixture {
    Expander exp;
    Button* first = nullptr;
    Button* last = nullptr;
};

void BuildOpenExpander(ExpanderFixture& f, UIContext& ctx, const RectDip& at) {
    f.exp.AttachToContext(ctx);
    f.exp.SetHeader(L"Settings");
    f.exp.SetExpanded(true);

    auto content = std::make_unique<StackPanel>();
    content->SetSpacing(8.0f);
    f.first = content->Add(std::make_unique<Button>());
    f.first->SetText(L"Option 1");
    f.last = content->Add(std::make_unique<Button>());
    f.last->SetText(L"Option 2");
    f.exp.SetContent(std::move(content));

    // Drive the reveal ease to completion. WantsAnimationTick() going false is the
    // same predicate Render uses to decide the bottom edge is safe to pad, so this
    // loop is what puts the fixture in the "settled" state a user tabs into.
    for (int i = 0; i < 500 && f.exp.WantsAnimationTick(); ++i)
        f.exp.OnAnimationTick(0.05f);

    f.exp.Measure(at.w, 1000.0f);
    f.exp.Arrange(RectDip{at.x, at.y, at.w, f.exp.Desired().h});
}

}  // namespace

// The bottom-most child, flush with the content rect's bottom AND both sides, keeps
// all four ring edges. Before the fix this drew only its top edge.
TEST(ClippedFocusRing, ExpanderContentClipKeepsRingOfBottomFlushChild) {
    PixelSurface s(520, 260);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ExpanderContentClipKeepsRingOfBottomFlushChild"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    ExpanderFixture f;
    BuildOpenExpander(f, ctx, RectDip{50.0f, 30.0f, 400.0f, 0.0f});
    EXPECT_TRUE(f.last != nullptr);
    EXPECT_FALSE(f.exp.WantsAnimationTick());   // settled, so the bottom is padded

    static_cast<Focusable<Button>*>(f.last)->SetFocused(true);
    s.Paint(kBg, [&](const DrawingContext& dc) { f.exp.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const RingEdges e = ProbeRing(s, f.last->Bounds());
    EXPECT_TRUE(e.top);
    EXPECT_TRUE(e.bottom);    // was FALSE before the fix (bottom flush with clip)
    EXPECT_TRUE(e.left);      // was FALSE before the fix (left flush with clip)
    EXPECT_TRUE(e.right);     // was FALSE before the fix (right flush with clip)
    EXPECT_EQ(e.Count(), 4);
}

// The first child is flush with the sides but not the bottom, so it isolates the
// horizontal half of the fix. Its top sits below the clip's top edge, so a
// regression that only padded the bottom would still pass the test above and fail
// this one.
TEST(ClippedFocusRing, ExpanderContentClipKeepsRingOfSideFlushChild) {
    PixelSurface s(520, 260);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ExpanderContentClipKeepsRingOfSideFlushChild"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    ExpanderFixture f;
    BuildOpenExpander(f, ctx, RectDip{50.0f, 30.0f, 400.0f, 0.0f});
    EXPECT_TRUE(f.first != nullptr);

    static_cast<Focusable<Button>*>(f.first)->SetFocused(true);
    s.Paint(kBg, [&](const DrawingContext& dc) { f.exp.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    EXPECT_EQ(ProbeRing(s, f.first->Bounds()).Count(), 4);
}

// The reveal wipe must still CLIP, and the fix must not widen it by more than the
// ring allowance. This is the assertion that stops someone "simplifying" the fix into
// an unbounded clip (or deleting it), which would let content paint arbitrarily far
// past the wipe edge.
//
// TWO THINGS THIS TEST HAD TO LEARN THE HARD WAY, both worth keeping written down
// because each produced a wrong test first:
//
// 1. A one-argument `Expander::SetExpanded(bool)` SNAPS: it calls
//    `reveal_.SetImmediate(...)`, so reveal_ lands on 0.0 or 1.0 immediately,
//    `WantsAnimationTick()` is false on the next line, and no mid-wipe state exists.
//    A first version of this test tried to sit mid-animation and silently skipped every
//    run, which is worse than no test.
//
//    UPDATE: the reveal ease is no longer unreachable. `SetExpanded` now takes an
//    `Expander::Transition` (default `Instant`, preserving the snap this test relies
//    on), and `Transition::Animate` genuinely eases — see ExpanderTransitionTests. This
//    test deliberately keeps the default, because what it is checking is the CLIP
//    geometry at a settled reveal, not the animation. If you want a mid-reveal clip
//    test, pass Transition::Animate and tick partway; the geometry note in point 2
//    still applies there.
//
// 2. The clip's height is `ContentDesiredHeight() * reveal`, NOT the arranged height.
//    So arranging the Expander shorter than its content does not tighten the clip:
//    with a 232 DIP content the clip runs to y=300 even when the Expander's own bounds
//    end at y=165, and the tail rows legitimately paint there. Asserting "no ink below
//    the Expander's bounds" therefore fails against CORRECT behavior. The boundary
//    that is actually a contract is the one derived from the same expression Render
//    uses.
//
// So this measures the clip bottom the way Render computes it, and asserts nothing
// paints beyond that plus the ring pad.
TEST(ClippedFocusRing, ExpanderContentClipBoundsContentAtRevealEdge) {
    PixelSurface s(520, 420);
    if (fltest::SkipIfNoDevice(s, "ClippedFocusRing.ExpanderContentClipBoundsContentAtRevealEdge"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Expander exp;
    exp.AttachToContext(ctx);
    exp.SetHeader(L"Settings");
    exp.SetExpanded(true);

    auto content = std::make_unique<StackPanel>();
    content->SetSpacing(8.0f);
    for (int i = 0; i < 6; ++i) {
        auto* btn = content->Add(std::make_unique<Button>());
        btn->SetText(L"Row");
    }
    exp.SetContent(std::move(content));

    exp.Measure(400.0f, 1000.0f);
    // Arrange shorter than desired so the content genuinely overflows its slot and the
    // clip has real work to do.
    const float fullH = exp.Desired().h;
    exp.Arrange(RectDip{50.0f, 30.0f, 400.0f, fullH * 0.5f});

    s.Paint(kBg, [&](const DrawingContext& dc) { exp.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    // Reproduce Render's clip bottom: content top + full content desired height
    // (reveal == 1.0 here), plus the settled ring pad. Content is allowed up to there.
    const float pad = FocusRingPadDip(FocusRingSpec{});
    const float contentTop = exp.HeaderRect().bottom();
    const float clipBottom = contentTop + exp.Content()->Desired().h + pad;
    const int firstForbiddenY = static_cast<int>(std::lround(clipBottom)) + 2;

    // The premise: the surface actually extends past the clip, so "nothing beyond it"
    // is a meaningful thing to check rather than a statement about the canvas edge.
    EXPECT_TRUE(firstForbiddenY + 10 < s.Height());

    bool inkPastClip = false;
    for (int y = firstForbiddenY; y < s.Height(); ++y)
        for (int x = static_cast<int>(exp.Bounds().x);
             x < static_cast<int>(exp.Bounds().right()); ++x)
            if (!s.At(x, y).Near(kBg, 12)) inkPastClip = true;

    EXPECT_FALSE(inkPastClip);
}
