// TextAreaWrapVirtualTests.cpp — wrapped virtualization on TextArea (§3).
//
// Phase 3 replaced Wrap's single whole-document IDWriteTextLayout with per-paragraph
// layouts plus an estimated extent that corrects itself as paragraphs are drawn. What
// makes this file necessary — rather than trusting the pre-existing Wrap tests — is that
// the old and new implementations agree on almost everything observable. Both wrap text
// at the content width, both put the caret in the same place, both report a plausible
// extent. The differences are in work NOT done and in ceilings NO LONGER hit:
//
//   * no layout covers more than one paragraph  -> WrapMeasuredParagraphs()
//   * a resize does not re-wrap the document    -> measured count after a bounds change
//   * content past ~5000 wrapped lines is drawn -> extent beyond the old maxHeight
//
// The pre-existing Wrap tests were satisfied by estimates alone: deleting the
// SetMeasured call in the measure step left all 806 of them green. That is what these
// assertions exist to catch, and it is why they go through
// MeasureParagraphsInBand — the method the draw loop itself calls — rather than through
// a draw, which returns at its null-DC guard headless.
//
// DWrite is real here, so every measurement below is a genuine DirectWrite wrap.
#include "../framework/Test.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/core/UIContext.h"
#include <string>

using namespace fluent;

namespace {

class MockHost : public WindowServices {
public:
    MockHost() { dwrite_.Initialize(); }
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
    bool Ready() const { return dwrite_.Valid(); }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host, AnimationRegistry& anims) {
    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dwrite = &host.DWrite();
    ctx.dpiScale = 1.0f;
    return ctx;
}

struct Fixture {
    MockHost host;
    AnimationRegistry anims;
    TextArea area;

    bool Ready() const { return host.Ready(); }

    // Wrap mode with real bounds. 300 x 120 gives ~278 DIP of content width, so a
    // paragraph of a few hundred characters wraps onto several visual lines.
    void Open(const std::wstring& text) {
        area.SetWrapMode(TextWrapMode::Wrap);
        area.SetText(text);
        area.SetBounds({20, 40, 300, 120});
        area.AttachToContext(MakeCtx(host, anims));
    }
    ~Fixture() { area.DetachFromContext(); }
};

// `n` paragraphs of `charsEach` characters, separated by newlines. Real words rather
// than a run of one letter, so DWrite has actual break opportunities — a single
// unbroken token wraps by character and would exercise a different path.
std::wstring Paragraphs(size_t n, size_t charsEach) {
    const std::wstring word = L"lorem ipsum dolor sit amet ";
    std::wstring para;
    while (para.size() < charsEach) para += word;
    para.resize(charsEach);
    std::wstring out;
    for (size_t i = 0; i < n; ++i) {
        out += para;
        if (i + 1 < n) out += L'\n';
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The paragraph model exists and is the unit of layout
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, ParagraphCountMatchesNewlineCount) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"one\ntwo\nthree");
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{3});
}

TEST(TextAreaWrapVirtual, ParagraphCountIsZeroInNoWrapMode) {
    // The two models are separate: NoWrap has no paragraph extent map, and reporting a
    // count there would suggest it does.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"one\ntwo\nthree");
    f.area.SetWrapMode(TextWrapMode::NoWrap);
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{0});
}

TEST(TextAreaWrapVirtual, EmptyDocumentIsOneParagraph) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{1});
}

// ---------------------------------------------------------------------------
// Nothing is measured until it is looked at — THE property of virtualization
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, OpeningADocumentMeasuresNothing) {
    // The claim that makes a large wrapped document openable: SetText must not wrap
    // anything. The old implementation laid out the entire buffer here — 3.7 seconds on
    // 28 MB, measured. Nothing in the rendered output distinguishes the two, so this
    // needs the counter.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(2000, 300));
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{2000});
    EXPECT_EQ(f.area.WrapMeasuredParagraphs(), size_t{0});
}

TEST(TextAreaWrapVirtual, MeasuringABandMeasuresOnlyThatBand) {
    // Cost is O(visible), not O(document). The band is 120 DIP of a 2000-paragraph
    // document, so a handful of paragraphs may be measured — nowhere near all of them.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(2000, 300));
    const size_t visited = f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_TRUE(visited > size_t{0});
    EXPECT_TRUE(visited < size_t{20});
    EXPECT_EQ(f.area.WrapMeasuredParagraphs(), visited);
}

TEST(TextAreaWrapVirtual, MeasuringABandActuallyRecordsMeasurements) {
    // THE assertion that a green suite was missing. Removing the SetMeasured call in the
    // measure step left every other Wrap test passing, because an estimate is a plausible
    // height and no other test could tell the difference. IsMeasured is the observable
    // that can.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(50, 300));
    EXPECT_TRUE(!f.area.WrapParagraphIsMeasured(0));
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_TRUE(f.area.WrapParagraphIsMeasured(0));
}

TEST(TextAreaWrapVirtual, BandCostIsIndependentOfDocumentSize) {
    // Same band, two documents differing by 50x in paragraph count. If the number of
    // paragraphs visited tracked the document rather than the band, virtualization is not
    // happening — and the extent would still look right, so nothing else would show it.
    // NOTE: not named `small` — RPC headers #define it to `char`, which turns
    // `Fixture small, large;` into a syntax error pointing at an unrelated line.
    Fixture tiny;
    Fixture huge;
    if (!tiny.Ready() || !huge.Ready()) return;
    tiny.Open(Paragraphs(40, 300));
    huge.Open(Paragraphs(2000, 300));
    const size_t a = tiny.area.MeasureParagraphsInBand(0.0f, 120.0f);
    const size_t b = huge.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_EQ(a, b);
}

TEST(TextAreaWrapVirtual, WalkKeepsGoingWhenParagraphsMeasureShorterThanEstimated) {
    // The band must end up genuinely COVERED, which is not the same as "the span computed
    // before measuring was honoured".
    //
    // Estimates can be wrong in both directions, and the two directions fail differently.
    // If a paragraph measures TALLER than its estimate, the up-front span covers too many
    // paragraphs — harmless overdraw. If it measures SHORTER, the band needs MORE
    // paragraphs than the span named, and a walk that stops at the original span.last
    // leaves the bottom of the band blank until some later frame happens to redraw it.
    //
    // That second case needs building deliberately: paragraphs whose estimate is too tall.
    // Long paragraphs at a WIDE control do it — the seed guesses ~2 characters per DIP
    // while a wide control fits far more, so each paragraph is estimated at several visual
    // lines and measures as one or two.
    //
    // Confirmed by mutation: replacing the walk's re-read of each paragraph's top with a
    // `p >= span.last` bound left the rest of this file green, because every other test
    // here uses paragraphs that measure taller than estimated.
    // NARROW GLYPHS are what create the gap. The seed assumes an average advance of
    // fontSize * 0.5 (7 DIP at the default 14), which is about right for mixed Latin prose
    // — so ordinary text is estimated within a line or two of the truth and the
    // over-estimate case never appears. Periods and spaces advance far less than that, so a
    // paragraph of them fits several times more characters per line than the seed predicts,
    // and its estimate comes out too tall.
    Fixture f;
    if (!f.Ready()) return;
    std::wstring narrow;
    for (size_t i = 0; i < 400; ++i) {
        narrow.append(400, L'.');
        if (i + 1 < 400) narrow.push_back(L'\n');
    }
    f.area.SetWrapMode(TextWrapMode::Wrap);
    f.area.SetText(narrow);
    f.area.SetBounds({20, 40, 1400, 300});
    f.area.AttachToContext(MakeCtx(f.host, f.anims));

    const float lh = f.area.LineHeightDip();
    const uint32_t estimated = f.area.WrapParagraphVisualLines(0);
    EXPECT_TRUE(estimated > uint32_t{1});   // the premise: estimated as several lines

    const float band = 400.0f;
    const size_t visited = f.area.MeasureParagraphsInBand(0.0f, band);
    EXPECT_TRUE(visited > size_t{0});
    EXPECT_TRUE(f.area.WrapParagraphVisualLines(0) < estimated);   // premise: over-estimated

    // The covered height must reach the band's bottom edge: the walk visited enough
    // paragraphs that the next one starts at or past it.
    EXPECT_TRUE(f.area.WrapParagraphTopDip(visited) >= band - lh);
}

TEST(TextAreaWrapVirtual, MeasuringIsIdempotent) {
    // The draw loop measures the visible band every frame, so re-measuring must be free
    // and must not accumulate — this is what makes a static wrapped view cost nothing.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(50, 300));
    const size_t first = f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    const float extentAfterFirst = f.area.MeasuredContentHeightDip();
    for (int i = 0; i < 5; ++i) f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_EQ(f.area.WrapMeasuredParagraphs(), first);
    EXPECT_EQ(f.area.MeasuredContentHeightDip(), extentAfterFirst);
}

TEST(TextAreaWrapVirtual, MeasuringInNoWrapModeDoesNothing) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(50, 300));
    f.area.SetWrapMode(TextWrapMode::NoWrap);
    EXPECT_EQ(f.area.MeasureParagraphsInBand(0.0f, 120.0f), size_t{0});
}

// ---------------------------------------------------------------------------
// A wrapped paragraph really occupies several visual lines
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, LongParagraphMeasuresAsSeveralVisualLines) {
    // The basic correctness of the measurement: 600 characters at ~278 DIP of width is
    // several visual lines, not one. If this were 1, the whole extent model would be
    // reporting a document one line tall per paragraph.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(10, 600));
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);
    EXPECT_TRUE(f.area.WrapParagraphIsMeasured(0));
    EXPECT_TRUE(f.area.WrapParagraphVisualLines(0) > uint32_t{1});
}

TEST(TextAreaWrapVirtual, ShortParagraphMeasuresAsOneVisualLine) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"short\nalso short\ntiny");
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_EQ(f.area.WrapParagraphVisualLines(0), uint32_t{1});
}

TEST(TextAreaWrapVirtual, EmptyParagraphIsOneVisualLine) {
    // A blank line between two paragraphs is a row the caret can sit on. Zero would
    // collapse it and shift every following paragraph's Y.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"first\n\nthird");
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_EQ(f.area.WrapParagraphVisualLines(1), uint32_t{1});
}

TEST(TextAreaWrapVirtual, MeasuredExtentReplacesEstimatedExtent) {
    // Measuring must CORRECT the extent, not add to it. Adding would make the document
    // grow every time the same region is drawn — a scrollbar that creeps while the view
    // sits still.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(200, 400));
    const float estimated = f.area.MeasuredContentHeightDip();
    EXPECT_TRUE(estimated > 0.0f);
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    const float afterOnce = f.area.MeasuredContentHeightDip();
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_EQ(f.area.MeasuredContentHeightDip(), afterOnce);
}

// ---------------------------------------------------------------------------
// Paragraph tops, and the growth that follows a measurement
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, ParagraphTopsAreMonotonic) {
    // Whatever the mix of measured and estimated heights, paragraph N+1 cannot start
    // above paragraph N. A violation means the prefix sums disagree with the counts, and
    // the visible symptom is paragraphs drawn on top of each other.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(100, 400));
    f.area.MeasureParagraphsInBand(200.0f, 120.0f);   // measure a band in the middle
    float previous = -1.0f;
    for (size_t p = 0; p < 100; ++p) {
        const float top = f.area.WrapParagraphTopDip(p);
        EXPECT_TRUE(top > previous);
        previous = top;
    }
}

TEST(TextAreaWrapVirtual, FirstParagraphStartsAtZero) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(10, 400));
    EXPECT_EQ(f.area.WrapParagraphTopDip(0), 0.0f);
}

TEST(TextAreaWrapVirtual, SpanForBandIsBoundedAndNonEmpty) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(500, 300));
    const LineSpan span = f.area.WrapParagraphSpanForBand(0.0f, 120.0f);
    EXPECT_TRUE(!span.Empty());
    EXPECT_TRUE(span.Count() < size_t{20});
}

TEST(TextAreaWrapVirtual, SpanPastTheDocumentIsEmpty) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"one\ntwo");
    const LineSpan span = f.area.WrapParagraphSpanForBand(100000.0f, 120.0f);
    EXPECT_TRUE(span.Empty());
}

// ---------------------------------------------------------------------------
// The ceilings that used to exist
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, ExtentGoesPastTheOldMaxHeightCeiling) {
    // The old whole-document layout was created with maxHeight = 100000 DIP and drawn
    // with D2D1_DRAW_TEXT_OPTIONS_CLIP, so everything past ~5000 wrapped lines was
    // clipped away — the symptom was "text stops at some row and the rest is white",
    // while the scrollbar happily scrolled into the blank. Per-paragraph layouts have no
    // such ceiling. 20 000 paragraphs of wrapped text is far past it.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20000, 200));
    EXPECT_TRUE(f.area.MeasuredContentHeightDip() > 100000.0f);
}

TEST(TextAreaWrapVirtual, ParagraphsPastTheOldCeilingAreReachable) {
    // Not just a taller extent — the content out there must be addressable. A band placed
    // beyond 100000 DIP has to resolve to real paragraphs and measure them.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20000, 200));
    const LineSpan span = f.area.WrapParagraphSpanForBand(150000.0f, 120.0f);
    EXPECT_TRUE(!span.Empty());
    EXPECT_TRUE(span.first > size_t{1000});
    const size_t visited = f.area.MeasureParagraphsInBand(150000.0f, 120.0f);
    EXPECT_TRUE(visited > size_t{0});
    EXPECT_TRUE(f.area.WrapParagraphIsMeasured(span.first));
}

// ---------------------------------------------------------------------------
// Resize: the other cost the old design carried
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, ResizeDiscardsMeasurementsRatherThanKeepingStaleOnes) {
    // A paragraph wrapped at 278 DIP breaks differently at 100 DIP, so every measurement
    // is stale after a width change and keeping any would mix widths in one extent. The
    // map resets to estimates — which is also why a resize is now O(paragraphs) of
    // arithmetic instead of a full re-wrap.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(200, 400));
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    EXPECT_TRUE(f.area.WrapMeasuredParagraphs() > size_t{0});

    f.area.SetBounds({20, 40, 150, 120});
    EXPECT_EQ(f.area.WrapMeasuredParagraphs(), size_t{0});
}

TEST(TextAreaWrapVirtual, NarrowingIncreasesTheExtent) {
    // The behaviour that must survive virtualization: narrower means more visual lines
    // means a taller document. Checked on the ESTIMATE, which is what a resize produces
    // before anything is redrawn — so this also pins that the estimate responds to width
    // rather than being a constant.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(100, 400));
    f.area.SetBounds({20, 40, 600, 120});
    const float wide = f.area.MeasuredContentHeightDip();
    f.area.SetBounds({20, 40, 150, 120});
    const float narrow = f.area.MeasuredContentHeightDip();
    EXPECT_TRUE(wide > 0.0f);
    EXPECT_TRUE(narrow > wide);
}

TEST(TextAreaWrapVirtual, ResizeThenMeasureGivesWidthAppropriateHeights) {
    // End to end: after a narrowing resize, a re-measured paragraph must report MORE
    // visual lines than it did at the wider width. This is the assertion that would catch
    // a cache that handed back layouts built at the old wrap width.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20, 400));
    f.area.SetBounds({20, 40, 600, 120});
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);
    const uint32_t wideLines = f.area.WrapParagraphVisualLines(0);
    EXPECT_TRUE(wideLines > uint32_t{0});

    f.area.SetBounds({20, 40, 150, 120});
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);
    const uint32_t narrowLines = f.area.WrapParagraphVisualLines(0);
    EXPECT_TRUE(narrowLines > wideLines);
}

// ---------------------------------------------------------------------------
// Caret and hit-test go through one paragraph, and agree with each other
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, CaretYFollowsTheParagraph) {
    // The caret's Y must be inside its own paragraph's band. With per-paragraph layouts
    // this is where an off-by-one-paragraph error would land the caret a screen away.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(50, 200));
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);

    // Put the caret at the start of paragraph 3 and check the reported rect.
    const std::wstring& text = f.area.Text();
    size_t offset = 0, seen = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && ++seen == 3) { offset = i + 1; break; }
    }
    f.area.TestSetCaret(static_cast<UINT32>(offset));

    RectDip caret{};
    EXPECT_TRUE(f.area.CaretRectDip(caret));
    const float top = f.area.WrapParagraphTopDip(3);
    const float height = f.area.WrapParagraphVisualLines(3) * f.area.LineHeightDip();
    // CaretRectDip is in window space with the scroll subtracted; convert the paragraph's
    // content-space band the same way for the comparison.
    const float caretContentY = caret.y - (40.0f + 8.0f) + f.area.CurrentOffsetDip();
    EXPECT_TRUE(caretContentY >= top - 1.0f);
    EXPECT_TRUE(caretContentY <= top + height + 1.0f);
}

TEST(TextAreaWrapVirtual, CaretInsideAWrappedParagraphIsBelowItsTop) {
    // A caret in the middle of a paragraph that wrapped onto several lines must sit on the
    // wrapped line it belongs to, not at the paragraph's top. That distinction is the
    // `y += ly` in WrapCaretMetrics; without it every caret snaps to its paragraph's first
    // line, which looks right for short paragraphs and wrong for every long one.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(5, 800));
    f.area.MeasureParagraphsInBand(0.0f, 800.0f);
    EXPECT_TRUE(f.area.WrapParagraphVisualLines(0) > uint32_t{2});

    f.area.TestSetCaret(0);
    RectDip atStart{};
    EXPECT_TRUE(f.area.CaretRectDip(atStart));

    // Far into the first paragraph, which is 800 characters wrapped at ~278 DIP.
    f.area.TestSetCaret(700);
    RectDip deep{};
    EXPECT_TRUE(f.area.CaretRectDip(deep));
    EXPECT_TRUE(deep.y > atStart.y);
}

TEST(TextAreaWrapVirtual, HitTestAndCaretAgree) {
    // Round trip: hit-test the caret's own rect and the offset must come back. A
    // disagreement puts the caret somewhere other than where the user clicked, and the two
    // directions are computed by different code (ParagraphFromYDip vs VisualLineBefore).
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(30, 300));
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);

    const std::wstring& text = f.area.Text();
    size_t offset = 0, seen = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && ++seen == 2) { offset = i + 1; break; }
    }
    f.area.TestSetCaret(static_cast<UINT32>(offset));
    RectDip caret{};
    EXPECT_TRUE(f.area.CaretRectDip(caret));

    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.clickCount = 1;
    // Aim at the vertical middle of the caret, a hair right of its left edge.
    e.position = {caret.x + 0.5f, caret.y + caret.h * 0.5f};
    f.area.OnPointerPressed(e);
    EXPECT_EQ(f.area.SelectionEndForTest(), static_cast<UINT32>(offset));
}

TEST(TextAreaWrapVirtual, ClickBelowTheLastParagraphLandsOnIt) {
    // Clamping, not clipping: a click in the blank area under a short document must place
    // the caret at the end rather than resolving to a paragraph that does not exist.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"one\ntwo");
    f.area.MeasureParagraphsInBand(0.0f, 120.0f);
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.clickCount = 1;
    e.position = {200.0f, 40.0f + 110.0f};   // near the bottom of the control
    f.area.OnPointerPressed(e);
    EXPECT_EQ(f.area.SelectionEndForTest(), static_cast<UINT32>(f.area.Text().size()));
}

// ---------------------------------------------------------------------------
// Navigation across wrapped lines
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, DownArrowMovesWithinAWrappedParagraph) {
    // Down must move one VISUAL line, so inside a paragraph that wrapped onto several
    // lines it stays in the same paragraph. Moving a whole paragraph at a time is the bug
    // this catches, and it only appears with wrapped text.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(5, 800));
    f.area.MeasureParagraphsInBand(0.0f, 800.0f);
    f.area.TestSetCaret(0);

    KeyEventArgs e;
    e.vk = VK_DOWN;
    f.area.OnKeyDownRouted(e);
    const UINT32 after = f.area.SelectionEndForTest();
    EXPECT_TRUE(after > UINT32{0});
    // Still inside paragraph 0, which is 800 characters long.
    EXPECT_TRUE(after < UINT32{800});
}

TEST(TextAreaWrapVirtual, EndGoesToTheVisualLineEndNotTheParagraphEnd) {
    // End on a wrapped line stops at that line's break, which is what every editor does.
    // Under the old whole-document layout this fell out of HitTestPoint; per paragraph it
    // has to be computed against the paragraph's own layout, so it is worth pinning.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(3, 800));
    f.area.MeasureParagraphsInBand(0.0f, 800.0f);
    EXPECT_TRUE(f.area.WrapParagraphVisualLines(0) > uint32_t{2});

    f.area.TestSetCaret(0);
    KeyEventArgs e;
    e.vk = VK_END;
    f.area.OnKeyDownRouted(e);
    const UINT32 after = f.area.SelectionEndForTest();
    EXPECT_TRUE(after > UINT32{0});
    EXPECT_TRUE(after < UINT32{800});   // not the paragraph end
}

TEST(TextAreaWrapVirtual, UpArrowAtTheTopStaysAtZero) {
    // The size_t-wraparound guard, same failure mode NoWrap has: Up at the document start
    // must clamp to 0, not jump to the last paragraph.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20, 300));
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);
    f.area.TestSetCaret(0);
    KeyEventArgs e;
    e.vk = VK_UP;
    f.area.OnKeyDownRouted(e);
    EXPECT_EQ(f.area.SelectionEndForTest(), UINT32{0});
}

// ---------------------------------------------------------------------------
// Editing keeps the model in step
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, EditInvalidatesTheExtentMap) {
    // An edit that adds a newline adds a paragraph, and every measurement keyed by
    // paragraph number past the edit now describes different text. The map must rebuild
    // rather than keep those.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20, 300));
    f.area.MeasureParagraphsInBand(0.0f, 400.0f);
    EXPECT_TRUE(f.area.WrapMeasuredParagraphs() > size_t{0});

    const size_t measuredBefore = f.area.WrapMeasuredParagraphs();
    f.area.TestSetCaret(0);
    f.area.OnTextInput(L'\n');
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{21});
    // Not zero: the edit routes through Changed -> EnsureCaretVisible, which asks for the
    // caret's geometry and therefore measures the caret's own paragraph on the way. That
    // is a legitimate measurement of the NEW text. What must be true is that the stale
    // ones were discarded, i.e. the count collapsed to roughly the caret's paragraph
    // rather than surviving the edit.
    EXPECT_TRUE(f.area.WrapMeasuredParagraphs() < measuredBefore);
    EXPECT_TRUE(f.area.WrapMeasuredParagraphs() <= size_t{2});
}

TEST(TextAreaWrapVirtual, SetTextRebuildsTheParagraphModel) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20, 300));
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{20});
    f.area.SetText(L"a\nb\nc");
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{3});
}

TEST(TextAreaWrapVirtual, AppendUnderWrapExtendsTheParagraphModel) {
    // AppendText's Wrap path takes the ordinary invalidation route (there is no per-
    // paragraph fast path under Wrap, and the header says so), but the model must still
    // end up describing the new text.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"one\ntwo");
    f.area.AppendText(L"\nthree");
    EXPECT_EQ(f.area.WrapParagraphCount(), size_t{3});
}

TEST(TextAreaWrapVirtual, ModeSwitchDiscardsTheOtherModelsState) {
    // Switching Wrap -> NoWrap -> Wrap must not carry measurements across: the NoWrap pass
    // laid out lines at an unbounded width, and those layouts are in the same cache.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Paragraphs(20, 400));
    // A band tall enough to cover well more paragraphs than the incidental caret-driven
    // measurement below, so "the band's measurements were discarded" is distinguishable
    // from "nothing was measured to begin with". 400-char paragraphs wrap onto ~6 visual
    // lines each at this width, so 400 DIP of band is only three of them.
    f.area.MeasureParagraphsInBand(0.0f, 1600.0f);
    const size_t measuredBefore = f.area.WrapMeasuredParagraphs();
    EXPECT_TRUE(measuredBefore > size_t{6});
    f.area.SetWrapMode(TextWrapMode::NoWrap);
    f.area.SetWrapMode(TextWrapMode::Wrap);
    // A handful survive rather than none, and for a defensible reason: each SetWrapMode
    // goes through OnTextLayoutDirty, whose composition refresh asks for the caret
    // geometry, which measures the caret's paragraph at the current width. Those are
    // measurements of the CURRENT mode's geometry, not carried over from the other one.
    // The assertion that matters is that the band measured before the switch is gone.
    EXPECT_TRUE(f.area.WrapMeasuredParagraphs() < measuredBefore);
    EXPECT_TRUE(f.area.WrapMeasuredParagraphs() <= size_t{3});
}

// ---------------------------------------------------------------------------
// Deferral before a width is known
// ---------------------------------------------------------------------------

TEST(TextAreaWrapVirtual, NoExtentBeforeBoundsArrive) {
    // Wrapping at ContentWidth()'s 1 DIP clamp floor puts one visual line per character —
    // the most expensive wrap that exists. The deferral that avoided that for the old
    // whole-document layout must still hold for the estimate.
    MockHost host;
    AnimationRegistry anims;
    if (!host.Ready()) return;
    TextArea area;
    area.SetWrapMode(TextWrapMode::Wrap);
    area.AttachToContext(MakeCtx(host, anims));
    area.SetText(Paragraphs(200, 300));
    EXPECT_TRUE(!area.LayoutWidthKnown());
    EXPECT_EQ(area.MeasuredContentHeightDip(), 0.0f);
    area.DetachFromContext();
}

TEST(TextAreaWrapVirtual, ExtentAppearsOnceBoundsArrive) {
    MockHost host;
    AnimationRegistry anims;
    if (!host.Ready()) return;
    TextArea area;
    area.SetWrapMode(TextWrapMode::Wrap);
    area.AttachToContext(MakeCtx(host, anims));
    area.SetText(Paragraphs(200, 300));
    area.SetBounds({20, 40, 300, 120});
    EXPECT_TRUE(area.LayoutWidthKnown());
    EXPECT_TRUE(area.MeasuredContentHeightDip() > 120.0f);
    area.DetachFromContext();
}
