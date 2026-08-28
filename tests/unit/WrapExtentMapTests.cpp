// WrapExtentMapTests.cpp — the soft-wrap extent model (phase 3).
//
// This is the structure that makes wrapped virtualization possible, and it is a pure
// data structure by design: no DWrite, no window, no device. That is deliberate and it
// is what makes the interesting properties checkable here rather than only on hardware.
//
// The properties worth pinning, and why each one is here:
//
//   * Estimates are REPLACED by measurements, never added to them. Getting this wrong
//     double-counts a paragraph's height, and the symptom on screen is a scrollbar that
//     grows every time you scroll past the same text.
//   * Measured paragraphs SURVIVE a re-estimate. The estimate pass runs when
//     charsPerLine drifts; if it overwrote measurements, scrolling back over measured
//     text would make the extent wobble instead of settle.
//   * Y↔paragraph is a round trip. Hit-testing goes one way and drawing goes the other,
//     so a disagreement puts the caret on a different paragraph than the one the click
//     landed on.
//   * The lazy prefix rebuild is INVISIBLE. Every query must give the same answer
//     whether or not a rebuild happened to be pending — a stale prefix is exactly the
//     kind of bug that shows up as one wrong frame after a scroll and is untraceable
//     from a screenshot.
//
// Line counts are integers throughout (see the header), so every assertion below can be
// exact rather than epsilon-based. Where DIPs appear the line height is a round 20 so
// the arithmetic stays readable.
#include "../framework/Test.h"
#include "../../FluentUI/text/WrapExtentMap.h"
#include "../../FluentUI/text/LineIndex.h"
#include <string>

using namespace fluent;

namespace {

// A LineIndex over `paragraphs` paragraphs of `charsEach` characters. Built through
// LineIndex rather than by hand so these tests exercise the same shape TextArea passes.
LineIndex MakeIndex(size_t paragraphs, size_t charsEach) {
    std::wstring text;
    for (size_t i = 0; i < paragraphs; ++i) {
        text.append(charsEach, L'x');
        if (i + 1 < paragraphs) text.push_back(L'\n');
    }
    LineIndex idx;
    idx.Rebuild(text);
    return idx;
}

constexpr float kLh = 20.0f;

} // namespace

// ---------------------------------------------------------------------------
// EstimateVisualLines — the pure estimator
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, EmptyParagraphIsOneVisualLine) {
    // An empty paragraph still occupies a row the caret can sit on. Returning 0 would
    // make blank lines collapse and every offset after them wrong.
    EXPECT_EQ(EstimateVisualLines(0, 40.0f), uint32_t{1});
}

TEST(WrapExtentMap, ShortParagraphIsOneVisualLine) {
    EXPECT_EQ(EstimateVisualLines(10, 40.0f), uint32_t{1});
    EXPECT_EQ(EstimateVisualLines(40, 40.0f), uint32_t{1});
}

TEST(WrapExtentMap, EstimateRoundsUp) {
    // 41 characters at 40 per line is two lines, not one — a ceiling, because the
    // overflow character has to go somewhere.
    EXPECT_EQ(EstimateVisualLines(41, 40.0f), uint32_t{2});
    EXPECT_EQ(EstimateVisualLines(80, 40.0f), uint32_t{2});
    EXPECT_EQ(EstimateVisualLines(81, 40.0f), uint32_t{3});
}

TEST(WrapExtentMap, EstimateHandlesNonPositiveCharsPerLine) {
    // No measurement has happened yet (or a degenerate width). One line per paragraph is
    // the honest fallback: it cannot be right, but it is finite and monotonic.
    EXPECT_EQ(EstimateVisualLines(500, 0.0f), uint32_t{1});
    EXPECT_EQ(EstimateVisualLines(500, -3.0f), uint32_t{1});
}

// ---------------------------------------------------------------------------
// Reset: the all-estimated starting state
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, ResetGivesEveryParagraphAnEstimate) {
    // 10 paragraphs of 100 chars at 40 chars/line = 3 visual lines each = 30 total.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.ParagraphCount(), size_t{10});
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{30});
    EXPECT_EQ(map.MeasuredCount(), size_t{0});
}

TEST(WrapExtentMap, TotalHeightIsLinesTimesLineHeight) {
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.TotalHeightDip(kLh), 30 * kLh);
}

TEST(WrapExtentMap, EmptyDocumentIsOneParagraphOneLine) {
    // LineIndex's convention: an empty buffer is one empty line. The extent map must
    // agree, or an empty editor reports zero height and the caret has nowhere to sit.
    LineIndex idx;
    idx.Rebuild(L"");
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.ParagraphCount(), size_t{1});
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{1});
}

TEST(WrapExtentMap, ResetClearsPriorMeasurements) {
    // A reset means the wrap width changed (or the text did), so every measurement is
    // stale. Keeping one would mix widths in a single extent.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(3, 7);
    EXPECT_EQ(map.MeasuredCount(), size_t{1});
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.MeasuredCount(), size_t{0});
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{30});
}

// ---------------------------------------------------------------------------
// SetMeasured: estimate -> truth
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, MeasuredReplacesEstimateNotAddsToIt) {
    // THE central invariant. Paragraph 0 was estimated at 3 lines; measuring it at 7 must
    // move the total by +4, not +7. Adding instead of replacing is the bug that makes a
    // scrollbar grow every time the same text is scrolled past.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{30});
    map.SetMeasured(0, 7);
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{34});
    EXPECT_EQ(map.VisualLinesAt(0), uint32_t{7});
    EXPECT_TRUE(map.IsMeasured(0));
}

TEST(WrapExtentMap, RemeasuringTheSameParagraphIsIdempotent) {
    // The draw loop measures whatever is on screen every frame, so the same paragraph is
    // re-measured constantly. That must not accumulate.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    for (int i = 0; i < 50; ++i) map.SetMeasured(0, 7);
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{34});
    EXPECT_EQ(map.MeasuredCount(), size_t{1});
}

TEST(WrapExtentMap, MeasuringSmallerThanTheEstimateShrinksTheTotal) {
    // Corrections go both ways. An estimate that was too generous must come down, or the
    // document reports more height than it has and the view scrolls into blank space.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(0, 1);
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{28});
}

TEST(WrapExtentMap, MeasuredZeroIsTreatedAsOneLine) {
    // DWrite reporting a zero-height layout would otherwise collapse the paragraph and
    // desync every following Y. Clamped at the entry point so no caller has to know.
    LineIndex idx = MakeIndex(3, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(1, 0);
    EXPECT_EQ(map.VisualLinesAt(1), uint32_t{1});
}

TEST(WrapExtentMap, SetMeasuredOutOfRangeIsIgnored) {
    LineIndex idx = MakeIndex(3, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    const uint32_t before = map.TotalVisualLines();
    map.SetMeasured(99, 5);
    EXPECT_EQ(map.TotalVisualLines(), before);
}

// ---------------------------------------------------------------------------
// Prefix sums and the Y mapping
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, ParagraphTopIsThePrefixSum) {
    // 3 lines each: paragraph i starts at line 3i.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.VisualLineBefore(0), uint32_t{0});
    EXPECT_EQ(map.VisualLineBefore(1), uint32_t{3});
    EXPECT_EQ(map.VisualLineBefore(5), uint32_t{15});
    EXPECT_EQ(map.ParagraphTopDip(5, kLh), 15 * kLh);
}

TEST(WrapExtentMap, PrefixReflectsMeasurementsImmediately) {
    // The lazy rebuild must be invisible: a query right after a measurement has to see
    // it. If the rebuild were skipped, everything below the measured paragraph would draw
    // at a stale Y for one frame — a visible jump that no test looking only at totals
    // would catch.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(0, 7);           // +4 lines
    EXPECT_EQ(map.VisualLineBefore(1), uint32_t{7});
    EXPECT_EQ(map.VisualLineBefore(2), uint32_t{10});
    EXPECT_EQ(map.VisualLineBefore(5), uint32_t{19});
}

TEST(WrapExtentMap, QueryAfterMeasureSeesItAcrossABlockBoundary) {
    // The REAL shape of the staleness bug, and the reason the test above is not enough on
    // its own. Two things have to line up for a missing invalidation to show:
    //
    //   * a query must happen FIRST, so the prefix array is clean and a skipped
    //     invalidation actually leaves a stale value behind;
    //   * the second query must land in a LATER BLOCK, because block 0's prefix entry is
    //     always zero and an in-block query recomputes from counts_ either way.
    //
    // Measure-then-query inside block 0 satisfies neither, so it passes even with the
    // invalidation deleted. Confirmed by mutation: removing prefixDirty_ from SetMeasured
    // left every other assertion in this file green except the scroll simulation, whose
    // failure pointed at a measurement count rather than at the prefix.
    const size_t n = WrapExtentMap::kBlockSize * 3;
    LineIndex idx = MakeIndex(n, 100);          // 3 estimated lines each
    WrapExtentMap map;
    map.Reset(idx, 40.0f);

    const size_t far = WrapExtentMap::kBlockSize * 2 + 5;
    EXPECT_EQ(map.VisualLineBefore(far), static_cast<uint32_t>(far * 3));   // prefix now clean

    map.SetMeasured(0, 13);                     // 3 -> 13, so every later Y shifts by +10
    EXPECT_EQ(map.VisualLineBefore(far), static_cast<uint32_t>(far * 3 + 10));
    // And the inverse mapping has to agree, or a click would resolve to a paragraph the
    // draw loop put somewhere else.
    EXPECT_EQ(map.ParagraphFromVisualLine(static_cast<uint32_t>(far * 3 + 10)), far);
}

TEST(WrapExtentMap, PrefixIsCorrectAcrossBlockBoundaries) {
    // The block structure is an implementation detail, so the arithmetic must not change
    // at a block seam. With kBlockSize paragraphs per block, this crosses several.
    const size_t n = WrapExtentMap::kBlockSize * 3 + 17;
    LineIndex idx = MakeIndex(n, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    for (size_t i = 0; i < n; ++i)
        EXPECT_EQ(map.VisualLineBefore(i), static_cast<uint32_t>(i * 3));
    EXPECT_EQ(map.TotalVisualLines(), static_cast<uint32_t>(n * 3));
}

TEST(WrapExtentMap, MeasurementInsideOneBlockShiftsLaterBlocks) {
    // A measurement in block 0 must move paragraphs in blocks 1..N. This is the case the
    // deferred block-sum rebuild exists for, and getting it wrong only shows up past the
    // first block — which is exactly why the assertion reaches that far.
    const size_t n = WrapExtentMap::kBlockSize * 3;
    LineIndex idx = MakeIndex(n, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(0, 13);   // 3 -> 13, so +10
    EXPECT_EQ(map.VisualLineBefore(1), uint32_t{13});
    const size_t far = WrapExtentMap::kBlockSize * 2 + 5;
    EXPECT_EQ(map.VisualLineBefore(far), static_cast<uint32_t>(far * 3 + 10));
}

TEST(WrapExtentMap, ParagraphFromVisualLineIsTheInverse) {
    // Round trip: every paragraph's own first line must map back to that paragraph.
    LineIndex idx = MakeIndex(50, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(7, 9);
    map.SetMeasured(20, 1);
    for (size_t i = 0; i < 50; ++i) {
        const uint32_t first = map.VisualLineBefore(i);
        EXPECT_EQ(map.ParagraphFromVisualLine(first), i);
    }
}

TEST(WrapExtentMap, ParagraphFromVisualLineHandlesInteriorLines) {
    // A Y in the MIDDLE of a tall paragraph must still resolve to that paragraph — this
    // is the wrapped case that does not exist under NoWrap, so it is the one most likely
    // to be got wrong.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(2, 10);           // paragraph 2 spans lines [6, 16)
    EXPECT_EQ(map.ParagraphFromVisualLine(6), size_t{2});
    EXPECT_EQ(map.ParagraphFromVisualLine(11), size_t{2});
    EXPECT_EQ(map.ParagraphFromVisualLine(15), size_t{2});
    EXPECT_EQ(map.ParagraphFromVisualLine(16), size_t{3});
}

TEST(WrapExtentMap, ParagraphFromVisualLinePastTheEndClampsToLast) {
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.ParagraphFromVisualLine(9999), size_t{9});
}

TEST(WrapExtentMap, ParagraphFromYUsesLineHeight) {
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    // Paragraph 4 spans lines [12,15) => Y [240, 300) at lh 20.
    EXPECT_EQ(map.ParagraphFromYDip(240.0f, kLh), size_t{4});
    EXPECT_EQ(map.ParagraphFromYDip(299.0f, kLh), size_t{4});
    EXPECT_EQ(map.ParagraphFromYDip(300.0f, kLh), size_t{5});
    // Negative Y (an overscan band starting above the document) clamps to paragraph 0.
    EXPECT_EQ(map.ParagraphFromYDip(-50.0f, kLh), size_t{0});
}

// ---------------------------------------------------------------------------
// The visible span — what the draw loop is allowed to touch
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, SpanForBandCoversOnlyTheBand) {
    // 3 lines each at lh 20 => 60 DIP per paragraph. A 120 DIP band at Y=0 covers
    // paragraphs 0 and 1, and the exclusive bottom edge must keep paragraph 2 out.
    LineIndex idx = MakeIndex(100, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    const LineSpan span = map.ParagraphSpanForBand(0.0f, 120.0f, kLh);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_EQ(span.last, size_t{2});
}

TEST(WrapExtentMap, SpanForBandIsBoundedRegardlessOfDocumentSize) {
    // The whole point of the exercise: a fixed band must touch a fixed number of
    // paragraphs whether the document has 100 or 100 000 of them. If this ever fails,
    // wrapped virtualization is not virtualizing.
    WrapExtentMap small, large;
    LineIndex smallIdx = MakeIndex(100, 100);
    LineIndex largeIdx = MakeIndex(100000, 100);
    small.Reset(smallIdx, 40.0f);
    large.Reset(largeIdx, 40.0f);
    const LineSpan a = small.ParagraphSpanForBand(600.0f, 400.0f, kLh);
    const LineSpan b = large.ParagraphSpanForBand(600.0f, 400.0f, kLh);
    EXPECT_EQ(a.Count(), b.Count());
    EXPECT_TRUE(a.Count() <= size_t{10});
}

TEST(WrapExtentMap, SpanPastTheDocumentIsEmpty) {
    // Legitimate: the overscan surface is taller than a short document near the end.
    LineIndex idx = MakeIndex(5, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    const LineSpan span = map.ParagraphSpanForBand(10000.0f, 400.0f, kLh);
    EXPECT_TRUE(span.Empty());
}

TEST(WrapExtentMap, SpanWithNegativeBandTopStartsAtZero) {
    LineIndex idx = MakeIndex(100, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    const LineSpan span = map.ParagraphSpanForBand(-200.0f, 400.0f, kLh);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_TRUE(span.last > size_t{0});
}

TEST(WrapExtentMap, SpanWithNonPositiveLineHeightIsEmpty) {
    // Before DWrite supplies metrics. An empty span draws nothing, which beats dividing
    // by zero.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_TRUE(map.ParagraphSpanForBand(0.0f, 400.0f, 0.0f).Empty());
}

TEST(WrapExtentMap, SpanCoversATallMeasuredParagraph) {
    // One paragraph measured much taller than its estimate should fill the band by
    // itself. A span that still returned several paragraphs would mean the loop draws
    // past the surface.
    LineIndex idx = MakeIndex(100, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(0, 200);
    const LineSpan span = map.ParagraphSpanForBand(0.0f, 400.0f, kLh);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_EQ(span.last, size_t{1});
}

// ---------------------------------------------------------------------------
// Re-estimation: the resize path
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, ReEstimateUpdatesUnmeasuredParagraphs) {
    // Halving charsPerLine doubles every estimate. This is what a resize does to the part
    // of the document nobody has looked at yet.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{30});
    map.ReEstimateUnmeasured(idx, 20.0f);
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{50});   // 100/20 = 5 lines each
}

TEST(WrapExtentMap, ReEstimatePreservesMeasuredParagraphs) {
    // THE other central invariant. A measurement is ground truth at the current width; a
    // re-estimate driven by a drifting charsPerLine average must not overwrite it, or
    // scrolling back over measured text would make the extent wobble forever instead of
    // settling.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    map.SetMeasured(0, 7);
    map.ReEstimateUnmeasured(idx, 20.0f);
    EXPECT_EQ(map.VisualLinesAt(0), uint32_t{7});
    EXPECT_TRUE(map.IsMeasured(0));
    // 9 unmeasured paragraphs at 5 lines each, plus the measured 7.
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{9 * 5 + 7});
}

TEST(WrapExtentMap, CharsPerLineDriftDecidesWhenToReEstimate) {
    // The guard that keeps the O(document) pass rare. Small drift is not worth a full
    // pass; large drift is. Both directions must trip it, or a width that got wider would
    // never re-estimate.
    WrapExtentMap map;
    EXPECT_TRUE(!WrapExtentMap::ShouldReEstimate(40.0f, 41.0f));
    EXPECT_TRUE(WrapExtentMap::ShouldReEstimate(40.0f, 60.0f));
    EXPECT_TRUE(WrapExtentMap::ShouldReEstimate(60.0f, 40.0f));
    // From "no estimate yet" any real value is a change worth applying.
    EXPECT_TRUE(WrapExtentMap::ShouldReEstimate(0.0f, 40.0f));
}

TEST(WrapExtentMap, MeasuredCharsPerLineAveragesOverMeasurements) {
    // The rolling average that feeds the estimator. Measuring a 100-char paragraph at 2
    // lines implies ~50 chars per line; the average must move toward that rather than
    // stay at whatever the initial guess was.
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 10.0f);
    map.NoteMeasuredCharsPerLine(100, 2);
    const float after = map.MeasuredCharsPerLine();
    EXPECT_TRUE(after > 10.0f);
    for (int i = 0; i < 200; ++i) map.NoteMeasuredCharsPerLine(100, 2);
    // Converges on the observed ratio.
    EXPECT_TRUE(map.MeasuredCharsPerLine() > 45.0f);
    EXPECT_TRUE(map.MeasuredCharsPerLine() < 55.0f);
}

TEST(WrapExtentMap, NoteMeasuredIgnoresDegenerateInput) {
    LineIndex idx = MakeIndex(10, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);
    const float before = map.MeasuredCharsPerLine();
    map.NoteMeasuredCharsPerLine(0, 3);    // empty paragraph says nothing about width
    map.NoteMeasuredCharsPerLine(100, 0);  // zero lines is not a measurement
    EXPECT_EQ(map.MeasuredCharsPerLine(), before);
}

// ---------------------------------------------------------------------------
// Consistency under a realistic scroll
// ---------------------------------------------------------------------------

TEST(WrapExtentMap, ScrollingThroughTheDocumentSettlesTheExtent) {
    // Simulate what actually happens: walk a band down the document, measuring whatever
    // it covers, as the draw loop would. Two things must hold at the end — every
    // paragraph is measured, and the total equals the sum of the measurements. The second
    // is what proves no estimate leaked into the total along the way.
    const size_t n = 300;
    LineIndex idx = MakeIndex(n, 100);
    WrapExtentMap map;
    map.Reset(idx, 40.0f);

    float y = 0.0f;
    int guard = 0;
    while (y < map.TotalHeightDip(kLh) && guard++ < 10000) {
        const LineSpan span = map.ParagraphSpanForBand(y, 200.0f, kLh);
        if (span.Empty()) break;
        for (size_t i = span.first; i < span.last; ++i)
            map.SetMeasured(i, 2 + static_cast<uint32_t>(i % 4));   // pretend measurement
        y += 100.0f;
    }

    EXPECT_EQ(map.MeasuredCount(), n);
    uint32_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += map.VisualLinesAt(i);
    EXPECT_EQ(map.TotalVisualLines(), sum);
}

TEST(WrapExtentMap, TotalAlwaysEqualsTheSumOfParts) {
    // A cheap invariant that catches any accounting slip in the incremental update path,
    // including ones that happen to leave the block sums self-consistent.
    LineIndex idx = MakeIndex(WrapExtentMap::kBlockSize * 2 + 7, 90);
    WrapExtentMap map;
    map.Reset(idx, 30.0f);
    map.SetMeasured(0, 11);
    map.SetMeasured(WrapExtentMap::kBlockSize, 1);
    map.SetMeasured(WrapExtentMap::kBlockSize * 2 + 6, 40);
    map.ReEstimateUnmeasured(idx, 15.0f);
    map.SetMeasured(5, 2);

    uint32_t sum = 0;
    for (size_t i = 0; i < map.ParagraphCount(); ++i) sum += map.VisualLinesAt(i);
    EXPECT_EQ(map.TotalVisualLines(), sum);
    EXPECT_EQ(map.VisualLineBefore(map.ParagraphCount()), sum);
}

TEST(WrapExtentMap, DefaultConstructedMapIsQueryable) {
    // Before any Reset. Every accessor must answer without a crash — TextArea asks for
    // the extent before it has a width, and that path must not need a guard at each call.
    WrapExtentMap map;
    EXPECT_EQ(map.ParagraphCount(), size_t{0});
    EXPECT_EQ(map.TotalVisualLines(), uint32_t{0});
    EXPECT_EQ(map.TotalHeightDip(kLh), 0.0f);
    EXPECT_EQ(map.ParagraphFromVisualLine(5), size_t{0});
    EXPECT_EQ(map.VisualLineBefore(3), uint32_t{0});
    EXPECT_TRUE(map.ParagraphSpanForBand(0.0f, 400.0f, kLh).Empty());
}
