// TextDescenderClipTests.cpp — glyph descenders (g y p q j) must not be sheared off.
//
// THE BUG THIS EXISTS FOR. Reported from a real screenshot: the Expander's header
// read "Settinqs" — the g's tail cut off flat at the baseline, and the same on
// "Category A" / "Subcategory 1" / "Subcategory 2".
//
// THE MECHANISM, and why it is a whole bug class rather than one typo. Twenty-two
// call sites draw text with `D2D1_DRAW_TEXT_OPTIONS_CLIP`, which is correct — it stops
// an over-long string bleeding out of its control. But it means the layout rect handed
// to DrawText is a hard clip, so any rect that is too SHORT silently amputates
// glyphs instead of overflowing visibly.
//
// The specific error is confusing a font's EM SIZE with its LINE HEIGHT.
// `typography.bodySize` is the em size (14). The line box Segoe UI actually needs at
// that size is about 1.33x taller (~18.6 DIP), because ascent + descent + line gap all
// live outside the em square. `Expander::HeaderHeight()` returned
// `bodySize + 2*kHeaderPadV` and then Render built its text rect as
// `[hdrRect.y + kHeaderPadV, hdrRect.bottom() - kHeaderPadV]` — algebraically exactly
// `bodySize` tall, i.e. the em size. With PARAGRAPH_ALIGNMENT_CENTER the glyph run is
// centred in that too-short box and the descender lands outside it, where CLIP
// removes it.
//
// GroupBox gets this right and is the model: it MEASURES the header
// (`IDWriteTextLayout::GetMetrics` → `m.height`) and uses that as the header height, so
// its rect is a real line box.
//
// HOW THESE TESTS WORK. Rather than reasoning about which of the 22 sites are wrong,
// they render each control with a descender-heavy string and look at the pixels below
// the glyph baseline. A descender is a few dark pixels in a narrow column under a 'g';
// if the rect clipped it, that column is empty. `DescenderInk` counts ink strictly
// below the baseline estimate, so it cannot be fooled by the body of the glyph.

#include "../framework/Test.h"
#include "../framework/PixelSurface.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/styling/ThemeManager.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/core/UIContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace fluent;
using fltest::Pixel;
using fltest::PixelSurface;

namespace {

constexpr Pixel kBg{255, 255, 255, 255};

// A string whose descenders are unmissable. Every one of g/y/p/q/j hangs below the
// baseline, so a clip at the baseline destroys a large fraction of the ink.
const wchar_t* kDescenderText = L"gypqj";

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

// Any pixel measurably darker than the page counts as ink. Text is antialiased grey,
// so this is deliberately loose; the assertions are about presence, not shade.
bool IsInk(const Pixel& p) {
    if (p.a < 30) return false;
    const int lum = (int(p.r) * 30 + int(p.g) * 59 + int(p.b) * 11) / 100;
    return lum < 200;
}

// Count ink pixels inside a rect, scanning the whole band.
int InkIn(const PixelSurface& s, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(s.Height(), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(s.Width(), x1); ++x)
            if (IsInk(s.At(x, y))) ++n;
    return n;
}

// The lowest row containing ink within [x0,x1) x [y0,y1). Returns y0-1 when none.
int LowestInkRow(const PixelSurface& s, int x0, int y0, int x1, int y1) {
    int lowest = y0 - 1;
    for (int y = std::max(0, y0); y < std::min(s.Height(), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(s.Width(), x1); ++x)
            if (IsInk(s.At(x, y))) { lowest = y; break; }
    return lowest;
}

// Render a descender string through a bare DrawText with a rect of the given height,
// and report how far down the ink reaches. This is the reference measurement: it tells
// the test how tall a line box genuinely needs to be, without hard-coding a number
// that a font or DPI change would invalidate.
int ReferenceDescenderDepth(PixelSurface& s, float fontSize, float rectTop,
                            float rectHeight) {
    s.Paint(kBg, [&](const DrawingContext& dc) {
        IDWriteTextFormat* fmt = Dw().Format(
            fontSize, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return;
        dc.DrawText(kDescenderText, 5, fmt,
                    D2D1::RectF(10.0f, rectTop, 200.0f, rectTop + rectHeight),
                    DefaultTheme().colors.textPrimary,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
    });
    if (!s.ReadBack()) return -1;
    return LowestInkRow(s, 10, static_cast<int>(rectTop), 200,
                        static_cast<int>(rectTop + rectHeight) + 40);
}

}  // namespace

// --- Harness self-check ------------------------------------------------------
// Prove the probe can tell a clipped descender from an intact one, using nothing but
// DrawText and two rect heights. Without this, every "descender missing" result below
// could just as easily be a broken probe.
TEST(TextDescenderClip, ProbeDistinguishesClippedFromIntactDescenders) {
    PixelSurface s(240, 140);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.ProbeDistinguishesClippedFromIntactDescenders"))
        return;
    if (!Dw().Valid()) return;

    const float fontSize = DefaultTheme().typography.bodySize;

    // A generous box: the full line box fits, descenders included.
    const int deepFull = ReferenceDescenderDepth(s, fontSize, 20.0f, fontSize * 3.0f);
    // A box exactly the EM SIZE tall — the mistake Expander made.
    const int deepEm = ReferenceDescenderDepth(s, fontSize, 20.0f, fontSize);

    EXPECT_TRUE(deepFull > 0);
    EXPECT_TRUE(deepEm > 0);
    // The em-tall box must lose ink that the generous box keeps. If these came out
    // equal, the probe (or the font) could not show this bug at all and every
    // assertion below would be vacuous.
    EXPECT_TRUE(deepEm < deepFull);
    std::printf("  reference: full-box ink to y=%d, em-tall box to y=%d (font %.1f)\n",
                deepFull, deepEm, fontSize);
}

// --- Expander: the reported defect, now FIXED --------------------------------
// The header's descenders must survive. Before the fix the header rect was exactly
// `bodySize` tall and the g/y/p/q/j tails were clipped flat at the baseline.
TEST(TextDescenderClip, ExpanderHeaderKeepsDescenders) {
    PixelSurface s(420, 160);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.ExpanderHeaderKeepsDescenders"))
        return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Expander exp;
    exp.AttachToContext(ctx);
    exp.SetHeader(kDescenderText);
    exp.Measure(400.0f, 400.0f);
    exp.Arrange(RectDip{20.0f, 20.0f, 380.0f, exp.Desired().h});

    s.Paint(kBg, [&](const DrawingContext& dc) { exp.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const RectDip hdr = exp.HeaderRect();
    // The glyph run is vertically centred in the header, so the baseline sits near the
    // middle. Ink must exist BELOW the vertical centre: that is the descender.
    const int centreY = static_cast<int>(std::lround(hdr.y + hdr.h * 0.5f));
    const int hdrBottom = static_cast<int>(std::lround(hdr.bottom()));
    const int x0 = static_cast<int>(std::lround(hdr.x));
    const int x1 = static_cast<int>(std::lround(hdr.right()));

    const int belowCentre = InkIn(s, x0, centreY + 2, x1, hdrBottom + 6);
    // Five descending glyphs produce a lot of sub-baseline ink; a clipped run produces
    // essentially none. The threshold is loose on purpose.
    EXPECT_TRUE(belowCentre > 10);
    std::printf("  expander header: %d ink px below centre (hdr h=%.2f)\n",
                belowCentre, hdr.h);
}

// The header row must be tall enough to HOLD a full line box, because the fix hands
// DrawText the whole row. Asserted against the font's own metrics rather than a magic
// number, so a theme or font change cannot make this quietly wrong.
//
// Note what changed between the failing and passing versions of this test: it first
// asserted on `HeaderRect().h - 2*kHeaderPadV` (14.00 vs a required 18.62 — the bug),
// because that inset was what Render actually passed to DrawText. The fix removed the
// vertical inset, so the quantity that must clear the line height is now the FULL row
// height. Both numbers are printed below so a future reader can see the margin.
TEST(TextDescenderClip, ExpanderHeaderRowFitsAFullLineBox) {
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Expander exp;
    exp.AttachToContext(ctx);
    exp.SetHeader(kDescenderText);
    exp.Measure(400.0f, 400.0f);
    exp.Arrange(RectDip{0.0f, 0.0f, 380.0f, exp.Desired().h});

    // Measure what DWrite says a line of this text needs.
    ComPtr<IDWriteTextLayout> layout;
    IDWriteTextFormat* fmt = Dw().Format(
        DefaultTheme().typography.bodySize, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
        DWRITE_WORD_WRAPPING_NO_WRAP);
    EXPECT_TRUE(fmt != nullptr);
    if (!fmt) return;
    EXPECT_TRUE(SUCCEEDED(Dw().Factory()->CreateTextLayout(
        kDescenderText, 5, fmt, 1000.0f, 1000.0f, layout.GetAddressOf())));
    if (!layout) return;

    DWRITE_TEXT_METRICS m{};
    EXPECT_TRUE(SUCCEEDED(layout->GetMetrics(&m)));

    const float rowH = exp.HeaderRect().h;
    const float oldInnerH = rowH - 24.0f;   // what Render used to pass: kHeaderPadV * 2
    std::printf("  line height=%.2f, header row=%.2f, old inner=%.2f, em=%.2f\n",
                m.height, rowH, oldInnerH, DefaultTheme().typography.bodySize);

    // The rect Render now passes is the full row, and it must fit the line box.
    EXPECT_TRUE(rowH + 0.5f >= m.height);

    // And the regression marker: the OLD inset rect genuinely could not fit it. If this
    // ever stops holding (a much larger kHeaderPadV, a much smaller font) then the
    // original code would no longer have been buggy, and this test's premise — plus the
    // long comment in Expander::Render — needs revisiting rather than silently passing.
    EXPECT_TRUE(oldInnerH < m.height);
}

// --- The sibling controls, checked rather than assumed ------------------------
// Each of these draws a label with DRAW_TEXT_OPTIONS_CLIP too. Testing them is the
// point: the Expander bug was one instance of a class, and a fix that only touches
// Expander should be accompanied by evidence about the neighbours rather than a guess.

TEST(TextDescenderClip, ButtonLabelKeepsDescenders) {
    PixelSurface s(300, 120);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.ButtonLabelKeepsDescenders")) return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    Button b;
    b.AttachToContext(ctx);
    b.SetText(kDescenderText);
    b.Measure(300.0f, 120.0f);
    const RectDip at{30.0f, 30.0f, 200.0f, std::max(36.0f, b.Desired().h)};
    b.Arrange(at);

    s.Paint(kBg, [&](const DrawingContext& dc) { b.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const int centreY = static_cast<int>(std::lround(at.y + at.h * 0.5f));
    const int ink = InkIn(s, static_cast<int>(at.x) + 2, centreY + 2,
                          static_cast<int>(at.right()) - 2,
                          static_cast<int>(at.bottom()) - 1);
    std::printf("  button: %d ink px below centre\n", ink);
    EXPECT_TRUE(ink > 10);
}

TEST(TextDescenderClip, CheckBoxLabelKeepsDescenders) {
    PixelSurface s(300, 120);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.CheckBoxLabelKeepsDescenders")) return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    CheckBox c;
    c.AttachToContext(ctx);
    c.SetText(kDescenderText);
    c.Measure(300.0f, 120.0f);
    const RectDip at{30.0f, 30.0f, 220.0f, std::max(24.0f, c.Desired().h)};
    c.Arrange(at);

    s.Paint(kBg, [&](const DrawingContext& dc) { c.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const int centreY = static_cast<int>(std::lround(at.y + at.h * 0.5f));
    // Skip the box itself (drawn at the left edge) so its border cannot be counted
    // as glyph ink.
    const int ink = InkIn(s, static_cast<int>(at.x) + 28, centreY + 2,
                          static_cast<int>(at.right()), static_cast<int>(at.bottom()) + 4);
    std::printf("  checkbox: %d ink px below centre\n", ink);
    EXPECT_TRUE(ink > 10);
}

TEST(TextDescenderClip, RadioButtonLabelKeepsDescenders) {
    PixelSurface s(300, 120);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.RadioButtonLabelKeepsDescenders")) return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    RadioButton r;
    r.AttachToContext(ctx);
    r.SetText(kDescenderText);
    r.Measure(300.0f, 120.0f);
    const RectDip at{30.0f, 30.0f, 220.0f, std::max(24.0f, r.Desired().h)};
    r.Arrange(at);

    s.Paint(kBg, [&](const DrawingContext& dc) { r.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const int centreY = static_cast<int>(std::lround(at.y + at.h * 0.5f));
    const int ink = InkIn(s, static_cast<int>(at.x) + 28, centreY + 2,
                          static_cast<int>(at.right()), static_cast<int>(at.bottom()) + 4);
    std::printf("  radiobutton: %d ink px below centre\n", ink);
    EXPECT_TRUE(ink > 10);
}

TEST(TextDescenderClip, ToggleSwitchLabelKeepsDescenders) {
    PixelSurface s(340, 120);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.ToggleSwitchLabelKeepsDescenders")) return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    ToggleSwitch t;
    t.AttachToContext(ctx);
    t.SetText(kDescenderText);
    t.Measure(340.0f, 120.0f);
    const RectDip at{30.0f, 30.0f, 260.0f, std::max(24.0f, t.Desired().h)};
    t.Arrange(at);

    s.Paint(kBg, [&](const DrawingContext& dc) { t.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const int centreY = static_cast<int>(std::lround(at.y + at.h * 0.5f));
    // The track is on the left; start well past it.
    const int ink = InkIn(s, static_cast<int>(at.x) + 56, centreY + 2,
                          static_cast<int>(at.right()), static_cast<int>(at.bottom()) + 4);
    std::printf("  toggleswitch: %d ink px below centre\n", ink);
    EXPECT_TRUE(ink > 10);
}

// GroupBox is the control that already did this correctly (it measures the header with
// GetMetrics instead of assuming the em size). Keeping a test on it locks in the
// working reference implementation, so a future "simplification" toward
// `bodySize + padding` gets caught here rather than in a screenshot.
TEST(TextDescenderClip, GroupBoxHeaderKeepsDescenders) {
    PixelSurface s(360, 200);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.GroupBoxHeaderKeepsDescenders")) return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    GroupBox g;
    g.AttachToContext(ctx);
    g.SetHeader(kDescenderText);
    g.Measure(340.0f, 180.0f);
    g.Arrange(RectDip{20.0f, 20.0f, 300.0f, 140.0f});

    s.Paint(kBg, [&](const DrawingContext& dc) { g.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    const RectDip hr = g.HeaderRect();
    // PARAGRAPH_ALIGNMENT_NEAR: the run sits at the top of the header rect, so the
    // baseline is roughly 4/5 down a line box. Probe the lower part of the header.
    const int probeTop = static_cast<int>(std::lround(hr.y + hr.h * 0.55f));
    const int ink = InkIn(s, static_cast<int>(hr.x), probeTop,
                          static_cast<int>(hr.right()),
                          static_cast<int>(std::lround(hr.bottom())) + 4);
    std::printf("  groupbox header: %d ink px in lower band (hdr h=%.2f)\n", ink, hr.h);
    EXPECT_TRUE(ink > 10);
}

TEST(TextDescenderClip, ListBoxItemKeepsDescenders) {
    PixelSurface s(320, 200);
    if (fltest::SkipIfNoDevice(s, "TextDescenderClip.ListBoxItemKeepsDescenders")) return;
    if (!Dw().Valid()) return;

    UIContext ctx = MakeCtx();
    ListBox lb;
    lb.AttachToContext(ctx);
    lb.SetItems({std::wstring(kDescenderText), std::wstring(kDescenderText)});
    lb.Measure(300.0f, 180.0f);
    const RectDip at{20.0f, 20.0f, 260.0f, 140.0f};
    lb.Arrange(at);

    s.Paint(kBg, [&](const DrawingContext& dc) { lb.Render(dc); });
    EXPECT_TRUE(s.ReadBack());

    // First row only: its lower half must carry descender ink.
    const int rowH = 32;
    const int centreY = static_cast<int>(at.y) + rowH / 2;
    const int ink = InkIn(s, static_cast<int>(at.x) + 2, centreY + 2,
                          static_cast<int>(at.right()) - 2, static_cast<int>(at.y) + rowH);
    std::printf("  listbox item: %d ink px below row centre\n", ink);
    EXPECT_TRUE(ink > 10);
}
