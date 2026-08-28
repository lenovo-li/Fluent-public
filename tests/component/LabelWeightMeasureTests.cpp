// LabelWeightMeasureTests.cpp — Measure must use the font weight Render draws with.
//
// THE DEFECT CLASS. `ContentControl::MeasureLabelSize` hard-coded
// DWRITE_FONT_WEIGHT_NORMAL into both the cache key and the fallback layout, while
// every render path draws through `EffectiveFontWeight(fallback)`. SemiBold is WIDER
// than Normal for the same string, so any control whose drawn weight was not Normal
// measured too narrow and its label overflowed the box its own Measure asked for.
//
// Two ways to reach the mismatch:
//   * Button::Kind::Accent renders SemiBold by control semantics (Button.cpp picks
//     SEMI_BOLD for Accent, NORMAL otherwise) but measured Normal.
//   * Any ContentControl the app calls SetFontWeight(SEMI_BOLD) on: the override is
//     honoured by Render via EffectiveFontWeight and was ignored by Measure.
//
// This is the THIRD instance of one recurring bug in this class — Measure and Render
// reading different font state. The earlier two, both already fixed, were
// ToggleSwitch's font SIZE (typography.bodySize in Measure vs EffectiveFontSize in
// Render) and Expander's header rect (em size used where a line box was needed).
// Hence the shared assertion style below: compare a control's own desired width
// against a directly-measured DWrite width at the weight the control will draw.
//
// WHY THESE TESTS CANNOT BE PURE UNIT TESTS. Real glyph advances are the whole
// subject, so they need a live DirectWrite factory (no D3D, no HWND, no GPU) and
// self-skip when DWrite is unavailable, like the other component tests here.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/graphics/ResourceCache.h"
#include "../../FluentUI/styling/ThemeManager.h"
#include "../../FluentUI/core/UIContext.h"

#include <cstdio>
#include <string>

using namespace fluent;

namespace {

// A string long enough that the Normal-vs-SemiBold advance difference is many DIPs,
// not a rounding artifact. Capitals widen the gap further.
const wchar_t* kLabel = L"WWWWMMMM Weight Sample WWWWMMMM";

DWriteContext& Dw() {
    static DWriteContext ctx;
    static bool ok = SUCCEEDED(ctx.Initialize());
    (void)ok;
    return ctx;
}

const ThemeSnapshot& Th() {
    static const ThemeSnapshot t = BuildSnapshot(ThemeInputs{}, 0);
    return t;
}

// Measure `text` directly through DWrite at a given weight. This is the reference the
// controls are compared against — deriving the expected number from the font itself
// rather than hard-coding one keeps the test valid across fonts and DPI.
float DirectWidth(const wchar_t* text, float fontSize, DWRITE_FONT_WEIGHT weight) {
    IDWriteTextFormat* fmt = Dw().Format(fontSize, weight,
                                         DWRITE_TEXT_ALIGNMENT_LEADING,
                                         DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                                         DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return -1.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(Dw().Factory()->CreateTextLayout(
            text, static_cast<UINT32>(wcslen(text)), fmt, 100000.0f, 100000.0f,
            layout.GetAddressOf())))
        return -1.0f;
    DWRITE_TEXT_METRICS m{};
    if (FAILED(layout->GetMetrics(&m))) return -1.0f;
    return m.widthIncludingTrailingWhitespace;
}

// A context with a real cache, since the cached path builds its own key and is the one
// that shipped the bug. Both paths are exercised: cached here, cache-less below.
struct Env {
    ResourceCache cache;
    UIContext ctx;
    explicit Env(bool withCache) {
        if (withCache) cache.Initialize(&Dw(), nullptr);
        ctx.dwrite = &Dw();
        ctx.theme = &Th();
        ctx.dpiScale = 1.0f;
        ctx.resourceCache = withCache ? &cache : nullptr;
    }
};

}  // namespace

// --- Premise check -----------------------------------------------------------
// Before asserting that a control measures at the right weight, establish that the
// two weights differ measurably for this string. If SemiBold and Normal came out the
// same width (a font without a SemiBold face, say), every assertion below would pass
// no matter what the controls did — vacuously.
TEST(LabelWeightMeasure, SemiBoldIsMeasurablyWiderThanNormal) {
    if (!Dw().Valid()) { std::printf("  [SKIP] no DirectWrite\n"); return; }

    const float size = Th().typography.bodySize;
    const float normal = DirectWidth(kLabel, size, DWRITE_FONT_WEIGHT_NORMAL);
    const float semi   = DirectWidth(kLabel, size, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    std::printf("  normal=%.2f  semibold=%.2f  delta=%.2f\n",
                normal, semi, semi - normal);
    EXPECT_TRUE(normal > 0.0f);
    EXPECT_TRUE(semi > 0.0f);
    EXPECT_TRUE(semi > normal + 1.0f);
}

// --- Button::Kind::Accent ----------------------------------------------------
// An Accent button renders SemiBold, so its desired width must be based on the
// SemiBold advance. Before the fix it measured Normal and came out narrower.
TEST(LabelWeightMeasure, AccentButtonMeasuresAtSemiBold) {
    if (!Dw().Valid()) return;

    Env env(true);
    const float size = Th().typography.bodySize;

    Button standard;
    standard.AttachToContext(env.ctx);
    standard.SetText(kLabel);
    standard.SetKind(Button::Kind::Standard);
    standard.Measure(100000.0f, 100000.0f);

    Button accent;
    accent.AttachToContext(env.ctx);
    accent.SetText(kLabel);
    accent.SetKind(Button::Kind::Accent);
    accent.Measure(100000.0f, 100000.0f);

    std::printf("  standard desired w=%.2f, accent desired w=%.2f\n",
                standard.Desired().w, accent.Desired().w);

    // The Accent button must be WIDER, because SemiBold is wider. This is the
    // assertion that fails outright when MeasureLabelSize ignores the weight: both
    // controls then report the identical Normal-based width.
    EXPECT_TRUE(accent.Desired().w > standard.Desired().w);

    // And quantitatively: the difference between the two desired widths should track
    // the difference DWrite reports for the two weights, since padding is identical.
    const float expectedDelta = DirectWidth(kLabel, size, DWRITE_FONT_WEIGHT_SEMI_BOLD)
                              - DirectWidth(kLabel, size, DWRITE_FONT_WEIGHT_NORMAL);
    const float actualDelta = accent.Desired().w - standard.Desired().w;
    std::printf("  expected delta=%.2f, actual delta=%.2f\n", expectedDelta, actualDelta);
    EXPECT_NEAR(actualDelta, expectedDelta, 1.0f);
}

// SetKind must dirty MEASURE, not just Render. It selects the weight, so the desired
// size changes with it; a Render-only flag leaves a stale desired width in place until
// something unrelated re-measures. The comment on SetKind previously asserted the
// opposite ("only swaps the fill palette ... Render-only").
TEST(LabelWeightMeasure, SetKindDirtiesMeasureBecauseItSelectsWeight) {
    if (!Dw().Valid()) return;

    Env env(true);
    Button b;
    b.AttachToContext(env.ctx);
    b.SetText(kLabel);
    b.SetKind(Button::Kind::Standard);
    b.Measure(100000.0f, 100000.0f);
    const float before = b.Desired().w;

    b.ClearDirtySubtree();
    b.SetKind(Button::Kind::Accent);

    // The flag itself: Measure-dirty after a kind change.
    EXPECT_TRUE(Has(b.Dirty(), DirtyFlags::Measure));

    // And the consequence, so the test still means something if the flag plumbing
    // changes shape: re-measuring now yields a different width.
    b.Measure(100000.0f, 100000.0f);
    std::printf("  desired w before=%.2f after=%.2f\n", before, b.Desired().w);
    EXPECT_TRUE(b.Desired().w > before);
}

// --- SetFontWeight override, across the shared helper's users ------------------
// EffectiveFontWeight lets the app pin a weight on any Control. Render honours it;
// Measure must too, or the label overflows. Checked on all three controls that reach
// the shared measurer, because the fix had to be applied at each call site (the helper
// cannot guess the caller's render weight).

TEST(LabelWeightMeasure, CheckBoxHonoursFontWeightOverrideWhenMeasuring) {
    if (!Dw().Valid()) return;

    Env env(true);
    CheckBox plain;
    plain.AttachToContext(env.ctx);
    plain.SetText(kLabel);
    plain.Measure(100000.0f, 100000.0f);

    CheckBox bold;
    bold.AttachToContext(env.ctx);
    bold.SetText(kLabel);
    bold.SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
    bold.Measure(100000.0f, 100000.0f);

    std::printf("  checkbox normal=%.2f semibold=%.2f\n",
                plain.Desired().w, bold.Desired().w);
    EXPECT_TRUE(bold.Desired().w > plain.Desired().w);
}

TEST(LabelWeightMeasure, RadioButtonHonoursFontWeightOverrideWhenMeasuring) {
    if (!Dw().Valid()) return;

    Env env(true);
    RadioButton plain;
    plain.AttachToContext(env.ctx);
    plain.SetText(kLabel);
    plain.Measure(100000.0f, 100000.0f);

    RadioButton bold;
    bold.AttachToContext(env.ctx);
    bold.SetText(kLabel);
    bold.SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
    bold.Measure(100000.0f, 100000.0f);

    std::printf("  radio normal=%.2f semibold=%.2f\n",
                plain.Desired().w, bold.Desired().w);
    EXPECT_TRUE(bold.Desired().w > plain.Desired().w);
}

TEST(LabelWeightMeasure, ToggleSwitchHonoursFontWeightOverrideWhenMeasuring) {
    if (!Dw().Valid()) return;

    Env env(true);
    ToggleSwitch plain;
    plain.AttachToContext(env.ctx);
    plain.SetText(kLabel);
    plain.Measure(100000.0f, 100000.0f);

    ToggleSwitch bold;
    bold.AttachToContext(env.ctx);
    bold.SetText(kLabel);
    bold.SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
    bold.Measure(100000.0f, 100000.0f);

    std::printf("  toggle normal=%.2f semibold=%.2f\n",
                plain.Desired().w, bold.Desired().w);
    EXPECT_TRUE(bold.Desired().w > plain.Desired().w);
}

// The cache-less path builds its own IDWriteTextFormat and had the same hard-coded
// weight, so it needs its own coverage: a headless context with no ResourceCache is a
// supported configuration (UIContext members are all nullable by design), and a fix
// applied only to the cached branch would leave this one wrong.
TEST(LabelWeightMeasure, WeightIsHonouredOnTheCacheLessPathToo) {
    if (!Dw().Valid()) return;

    Env env(false);                 // no ResourceCache
    EXPECT_TRUE(env.ctx.resourceCache == nullptr);

    Button standard;
    standard.AttachToContext(env.ctx);
    standard.SetText(kLabel);
    standard.SetKind(Button::Kind::Standard);
    standard.Measure(100000.0f, 100000.0f);

    Button accent;
    accent.AttachToContext(env.ctx);
    accent.SetText(kLabel);
    accent.SetKind(Button::Kind::Accent);
    accent.Measure(100000.0f, 100000.0f);

    std::printf("  cache-less: standard=%.2f accent=%.2f\n",
                standard.Desired().w, accent.Desired().w);
    EXPECT_TRUE(accent.Desired().w > standard.Desired().w);
}

// Distinct weights must not collide in the layout cache. If the key omitted the
// weight, the second lookup would return the first entry and both controls would
// report the same width — the same "key that lies about the object it names" trap the
// header comment on MeasureLabelSize warns about for alignment.
TEST(LabelWeightMeasure, LayoutCacheKeyDistinguishesWeights) {
    if (!Dw().Valid()) return;

    Env env(true);

    // Measure Normal first so it is the entry already in the cache.
    Button normalFirst;
    normalFirst.AttachToContext(env.ctx);
    normalFirst.SetText(kLabel);
    normalFirst.SetKind(Button::Kind::Standard);
    normalFirst.Measure(100000.0f, 100000.0f);

    Button semiSecond;
    semiSecond.AttachToContext(env.ctx);
    semiSecond.SetText(kLabel);
    semiSecond.SetKind(Button::Kind::Accent);
    semiSecond.Measure(100000.0f, 100000.0f);

    // A cache hit on the wrong entry shows up as identical widths.
    EXPECT_TRUE(semiSecond.Desired().w != normalFirst.Desired().w);

    // Re-measuring the Normal one must still give the Normal width: the SemiBold
    // insert must not have displaced or overwritten it.
    const float normalW = normalFirst.Desired().w;
    normalFirst.ClearDirtySubtree();
    normalFirst.Measure(100000.0f, 100000.0f);
    EXPECT_NEAR(normalFirst.Desired().w, normalW, 0.01f);
}
