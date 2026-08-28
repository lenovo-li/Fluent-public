// VisibleLinesTests.cpp — the band-to-lines arithmetic behind NoWrap virtualization.
//
// This is the claim the whole phase rests on: drawing costs O(lines in the band),
// not O(lines in the document). The draw loop that consumes this is unreachable
// headless (it returns immediately on the null DC a fake backend supplies), so the
// judgment was extracted here specifically so it can be checked — the repo's
// PlanRedraw / FramePacing pattern, applied for the same reason.
#include "../framework/Test.h"
#include "../../FluentUI/text/VisibleLines.h"
#include <vector>

using namespace fluent;

// --- The core property ------------------------------------------------------

TEST(VisibleLines, CountIsBoundedByBandNotDocument) {
    // THE test. Same 20 DIP band, same 20 DIP line height, documents three orders of
    // magnitude apart: the number of lines to draw must not move. If this ever fails,
    // virtualization is gone and a large document is back to being O(document) per
    // frame — which is the exact bug this phase exists to remove.
    const float lh = 20.0f;
    const LineSpan small = VisibleLineSpan(0.0f, 100.0f, lh, 10);
    const LineSpan huge  = VisibleLineSpan(0.0f, 100.0f, lh, 1000000);
    EXPECT_EQ(small.Count(), size_t{6});   // lines 0..5 (the 6th starts at the edge)
    EXPECT_EQ(huge.Count(), size_t{6});
}

TEST(VisibleLines, ScrollingDeepIntoADocumentStaysCheap) {
    // Same band size, scrolled a million lines down. Cost must be identical to the
    // top of the document — the offset must not enter into the count.
    const float lh = 20.0f;
    const LineSpan top  = VisibleLineSpan(0.0f, 200.0f, lh, 2000000);
    const LineSpan deep = VisibleLineSpan(20000000.0f, 200.0f, lh, 2000000);
    EXPECT_EQ(deep.Count(), top.Count());
}

// --- Where the band starts --------------------------------------------------

TEST(VisibleLines, FirstLineComesFromTheBandTop) {
    const float lh = 20.0f;
    EXPECT_EQ(VisibleLineSpan(0.0f,   40.0f, lh, 100).first, size_t{0});
    EXPECT_EQ(VisibleLineSpan(20.0f,  40.0f, lh, 100).first, size_t{1});
    EXPECT_EQ(VisibleLineSpan(100.0f, 40.0f, lh, 100).first, size_t{5});
}

TEST(VisibleLines, PartiallyScrolledLineIsStillIncluded) {
    // Band starts mid-line-2. That line is partly visible, so it must be drawn —
    // rounding up here would leave a blank strip at the top of the viewport.
    const LineSpan span = VisibleLineSpan(50.0f, 40.0f, 20.0f, 100);
    EXPECT_EQ(span.first, size_t{2});
}

TEST(VisibleLines, NegativeBandTopClampsToZero) {
    // An overscan surface can legitimately begin above the document when the view is
    // at the very top. Without the clamp, the float-to-size_t conversion of a
    // negative index is undefined and lands on a huge value — which would report the
    // span as empty and draw nothing at all.
    const LineSpan span = VisibleLineSpan(-100.0f, 200.0f, 20.0f, 50);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_TRUE(span.Count() > 0);
}

// --- Where the band ends ----------------------------------------------------

TEST(VisibleLines, LastIsExclusiveAtTheBandBottom) {
    // Band [0, 100) at 20 DIP/line covers lines 0-4 fully; line 5's top is exactly
    // at 100, the exclusive edge. It is included because the band's own bottom pixel
    // row belongs to it — but line 6 must not be.
    const LineSpan span = VisibleLineSpan(0.0f, 100.0f, 20.0f, 100);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_EQ(span.last, size_t{6});
}

TEST(VisibleLines, ClampsToLineCount) {
    // A band taller than the document must not run past the last line, or the draw
    // loop indexes the line index out of range.
    const LineSpan span = VisibleLineSpan(0.0f, 10000.0f, 20.0f, 7);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_EQ(span.last, size_t{7});
}

TEST(VisibleLines, BandEntirelyBelowTheDocumentIsEmpty) {
    // Happens near the end of a short buffer: the overscan surface extends past the
    // last line. Drawing nothing is correct; indexing line 500 of a 10-line document
    // is not.
    const LineSpan span = VisibleLineSpan(1000.0f, 200.0f, 20.0f, 10);
    EXPECT_TRUE(span.Empty());
    EXPECT_EQ(span.Count(), size_t{0});
}

TEST(VisibleLines, BandEntirelyAboveTheDocumentIsEmpty) {
    const LineSpan span = VisibleLineSpan(-500.0f, 100.0f, 20.0f, 10);
    EXPECT_TRUE(span.Empty());
}

// --- Degenerate inputs ------------------------------------------------------

TEST(VisibleLines, ZeroLineHeightIsEmptyNotADivideByZero) {
    // LineHeight() is a DWrite measurement, and before attach it has no real value.
    // Returning an empty span makes the draw loop a no-op for that frame, which is
    // the honest answer — the alternative is a division by zero.
    EXPECT_TRUE(VisibleLineSpan(0.0f, 100.0f, 0.0f, 50).Empty());
    EXPECT_TRUE(VisibleLineSpan(0.0f, 100.0f, -5.0f, 50).Empty());
}

TEST(VisibleLines, EmptyDocumentIsEmptySpan) {
    EXPECT_TRUE(VisibleLineSpan(0.0f, 100.0f, 20.0f, 0).Empty());
}

TEST(VisibleLines, ZeroHeightBandIsEmpty) {
    // A collapsed control has no visible band, so nothing should be laid out. Without
    // this, a zero-height viewport would still pay for one line's layout per frame.
    EXPECT_TRUE(VisibleLineSpan(0.0f, 0.0f, 20.0f, 50).Empty());
    EXPECT_TRUE(VisibleLineSpan(0.0f, -10.0f, 20.0f, 50).Empty());
}

TEST(VisibleLines, SingleLineDocument) {
    const LineSpan span = VisibleLineSpan(0.0f, 100.0f, 20.0f, 1);
    EXPECT_EQ(span.first, size_t{0});
    EXPECT_EQ(span.last, size_t{1});
    EXPECT_EQ(span.Count(), size_t{1});
}

// --- Coverage: every line of the document is reachable ----------------------

TEST(VisibleLines, ConsecutiveBandsCoverEveryLineExactlyOnce) {
    // Walking a document one viewport at a time must visit every line, and the seam
    // between adjacent bands must not skip one. A skipped line renders as a blank row
    // that appears only at certain scroll offsets — the kind of bug that survives
    // casual testing because it is invisible unless you scroll to exactly that spot.
    //
    // Overlap at the seam is acceptable (a line straddling the boundary is drawn by
    // both bands); a gap is not. So the assertion is coverage, not partition.
    const float lh = 20.0f;
    const size_t lineCount = 97;          // deliberately not a multiple of the band
    const float bandHeight = 100.0f;      // 5 lines per band, so seams fall mid-line

    std::vector<bool> seen(lineCount, false);
    for (float top = 0.0f; top < lineCount * lh; top += bandHeight) {
        const LineSpan span = VisibleLineSpan(top, bandHeight, lh, lineCount);
        for (size_t i = span.first; i < span.last; ++i) seen[i] = true;
    }
    for (size_t i = 0; i < lineCount; ++i) EXPECT_TRUE(seen[i]);
}

TEST(VisibleLines, SpanNeverExceedsTheDocument) {
    // Fuzz the three float inputs across the awkward cases — negative tops, bands
    // taller than the document, fractional line heights — and assert the invariant
    // the draw loop depends on for memory safety: last <= lineCount, always.
    const size_t lineCount = 40;
    const float heights[] = {1.0f, 13.7f, 20.0f, 99.5f};
    const float tops[] = {-500.0f, -1.0f, 0.0f, 0.5f, 199.0f, 800.0f, 1e6f};
    const float bands[] = {0.0f, 1.0f, 37.5f, 400.0f, 1e5f};
    for (float lh : heights)
        for (float top : tops)
            for (float band : bands) {
                const LineSpan span = VisibleLineSpan(top, band, lh, lineCount);
                EXPECT_TRUE(span.last <= lineCount);
                EXPECT_TRUE(span.first <= span.last || span.Empty());
            }
}
