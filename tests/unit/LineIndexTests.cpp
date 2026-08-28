// LineIndexTests.cpp — logical-line index over a text buffer.
//
// LineIndex is the foundation of NoWrap text virtualization: every drawing,
// caret and hit-test decision in that mode is derived from it, so an off-by-one
// here shows up as text drawn on the wrong row or a caret one character adrift.
// It is pure data with no DWrite / window dependency, so all of it is reachable
// headless — including the log path's central invariant (Append composes to the
// same state as Rebuild), which is otherwise only observable on real hardware
// under a live data feed.
#include "../framework/Test.h"
#include "../../FluentUI/text/LineIndex.h"

using namespace fluent;

namespace {

// Assert the full shape of an index against a buffer: line count, and every
// line's range extracted as text. Comparing extracted STRINGS rather than raw
// offsets is deliberate — an offset pair can be self-consistently wrong (both
// ends shifted by the same amount), and reconstructing the text catches that.
void ExpectLines(const LineIndex& idx, std::wstring_view text,
                 const std::vector<std::wstring>& expected) {
    EXPECT_EQ(idx.LineCount(), expected.size());
    EXPECT_EQ(idx.TextLength(), text.size());
    if (idx.LineCount() != expected.size()) return;
    for (size_t i = 0; i < expected.size(); ++i) {
        auto [start, end] = idx.LineRange(i);
        EXPECT_TRUE(start <= end);
        EXPECT_TRUE(end <= text.size());
        if (start > end || end > text.size()) continue;
        EXPECT_TRUE(text.substr(start, end - start) == expected[i]);
    }
}

} // namespace

// --- Rebuild: the shapes a text buffer actually takes ----------------------

TEST(LineIndex, EmptyBufferIsOneEmptyLine) {
    // The convention that costs the least downstream: LineCount() is never 0, so
    // no caller needs a "document has no lines" branch. An empty document has one
    // empty line, which is also where the caret sits.
    LineIndex idx;
    idx.Rebuild(L"");
    ExpectLines(idx, L"", {L""});
}

TEST(LineIndex, DefaultConstructedMatchesEmptyRebuild) {
    // A default-constructed index must already be usable — the control holds one
    // as a member and may query it before any SetText lands.
    LineIndex fresh;
    EXPECT_EQ(fresh.LineCount(), size_t{1});
    EXPECT_EQ(fresh.TextLength(), size_t{0});
    auto [start, end] = fresh.LineRange(0);
    EXPECT_EQ(start, size_t{0});
    EXPECT_EQ(end, size_t{0});
    EXPECT_EQ(fresh.LineFromOffset(0), size_t{0});
}

TEST(LineIndex, SingleLineNoTrailingNewline) {
    LineIndex idx;
    idx.Rebuild(L"hello");
    ExpectLines(idx, L"hello", {L"hello"});
}

TEST(LineIndex, ThreeLinesNoTrailingNewline) {
    LineIndex idx;
    idx.Rebuild(L"aa\nbb\nc");
    ExpectLines(idx, L"aa\nbb\nc", {L"aa", L"bb", L"c"});
}

TEST(LineIndex, TrailingNewlineAddsAnEmptyLine) {
    // "a\n" is TWO lines: the caret can sit on the empty second one. Treating the
    // trailing newline as decoration on line 0 would make the last row of every
    // log file unreachable.
    LineIndex idx;
    idx.Rebuild(L"a\n");
    ExpectLines(idx, L"a\n", {L"a", L""});
}

TEST(LineIndex, OnlyNewlines) {
    LineIndex idx;
    idx.Rebuild(L"\n\n\n");
    ExpectLines(idx, L"\n\n\n", {L"", L"", L"", L""});
}

TEST(LineIndex, EmptyLinesBetweenContent) {
    LineIndex idx;
    idx.Rebuild(L"a\n\nb");
    ExpectLines(idx, L"a\n\nb", {L"a", L"", L"b"});
}

TEST(LineIndex, CarriageReturnDoesNotEndALine) {
    // TextArea::SanitizeInput normalizes CRLF and lone CR to '\n' before text
    // reaches the buffer, so a '\r' here means someone bypassed that path. The
    // index deliberately does NOT treat it as a terminator: it must agree with
    // what DWrite will draw, and being cleverer than the renderer would put the
    // caret on a row the text isn't on.
    LineIndex idx;
    idx.Rebuild(L"a\rb\nc");
    ExpectLines(idx, L"a\rb\nc", {L"a\rb", L"c"});
}

TEST(LineIndex, RebuildIsIdempotentAndReplacesPriorState) {
    // A second Rebuild must fully replace the first — a stale tail would leave
    // phantom lines pointing past the new buffer.
    LineIndex idx;
    idx.Rebuild(L"one\ntwo\nthree\nfour");
    idx.Rebuild(L"x\ny");
    ExpectLines(idx, L"x\ny", {L"x", L"y"});
}

// --- LineFromOffset: the caret path ---------------------------------------

TEST(LineIndex, LineFromOffsetAtLineBoundaries) {
    // Buffer:      a  a  \n b  b  \n c
    // Offset:      0  1  2  3  4  5  6   (7 == end of buffer)
    // The newline at offset 2 terminates line 0, so it belongs to line 0; offset
    // 3 is the first character of line 1.
    const std::wstring text = L"aa\nbb\nc";
    LineIndex idx;
    idx.Rebuild(text);

    EXPECT_EQ(idx.LineFromOffset(0), size_t{0});  // start of line 0
    EXPECT_EQ(idx.LineFromOffset(1), size_t{0});
    EXPECT_EQ(idx.LineFromOffset(2), size_t{0});  // the '\n' ending line 0
    EXPECT_EQ(idx.LineFromOffset(3), size_t{1});  // start of line 1
    EXPECT_EQ(idx.LineFromOffset(4), size_t{1});
    EXPECT_EQ(idx.LineFromOffset(5), size_t{1});  // the '\n' ending line 1
    EXPECT_EQ(idx.LineFromOffset(6), size_t{2});  // start of line 2
    EXPECT_EQ(idx.LineFromOffset(7), size_t{2});  // caret at end of document
}

TEST(LineIndex, LineFromOffsetPastEndClampsToLastLine) {
    // A caret index can transiently exceed the buffer (a pending delete, a
    // clamped paste). Clamping keeps that from indexing the sentinel, which is
    // not a line.
    LineIndex idx;
    idx.Rebuild(L"ab\ncd");
    EXPECT_EQ(idx.LineFromOffset(5), size_t{1});    // exactly the end
    EXPECT_EQ(idx.LineFromOffset(6), size_t{1});    // one past
    EXPECT_EQ(idx.LineFromOffset(99999), size_t{1});
}

TEST(LineIndex, LineFromOffsetOnTrailingEmptyLine) {
    // The empty final line of "a\n" starts at the same offset the buffer ends at,
    // which is also where the sentinel lives. The end-of-document caret must land
    // on that line (index 1), not on the sentinel and not back on line 0.
    LineIndex idx;
    idx.Rebuild(L"a\n");
    EXPECT_EQ(idx.LineCount(), size_t{2});
    EXPECT_EQ(idx.LineFromOffset(2), size_t{1});
}

TEST(LineIndex, LineFromOffsetAgreesWithLineRangeEverywhere) {
    // The cross-check that makes the two accessors one contract instead of two:
    // for every offset in the buffer, the line reported by LineFromOffset must be
    // a line whose range actually contains that offset.
    const std::wstring text = L"first\n\nthird line\nfourth\n";
    LineIndex idx;
    idx.Rebuild(text);
    for (size_t off = 0; off <= text.size(); ++off) {
        const size_t line = idx.LineFromOffset(off);
        EXPECT_TRUE(line < idx.LineCount());
        auto [start, end] = idx.LineRange(line);
        // `off` is in [start, end] — end inclusive, because a caret may sit at
        // the end of a line, and for a terminated line that offset is the '\n'.
        EXPECT_TRUE(off >= start);
        EXPECT_TRUE(off <= end + 1);
    }
}

TEST(LineIndex, LineRangeOutOfBoundsClampsInsteadOfReadingPastTheEnd) {
    // A virtualized draw loop derives the row index from a scroll offset; one
    // stale offset should render a wrong row, not corrupt memory.
    LineIndex idx;
    idx.Rebuild(L"a\nb");
    auto [start, end] = idx.LineRange(99);
    EXPECT_EQ(start, size_t{2});
    EXPECT_EQ(end, size_t{3});
}

// --- Append: the log path ------------------------------------------------

TEST(LineIndex, AppendMatchesRebuildOfConcatenation) {
    // THE core invariant of the log path. Appending in arbitrary chunks — with
    // splits landing mid-line, exactly on a newline, and on an empty chunk — must
    // reach byte-identical state to one Rebuild of the whole text. If this drifts,
    // a long-running log view slowly starts drawing the wrong lines.
    const std::vector<std::wstring> chunks = {
        L"first line\n", L"second ", L"line\n", L"", L"\n", L"tail",
    };
    std::wstring whole;
    LineIndex appended;
    appended.Rebuild(L"");
    for (const std::wstring& chunk : chunks) {
        EXPECT_TRUE(appended.Append(chunk, whole.size()));
        whole += chunk;
    }

    LineIndex rebuilt;
    rebuilt.Rebuild(whole);

    EXPECT_EQ(appended.LineCount(), rebuilt.LineCount());
    EXPECT_EQ(appended.TextLength(), rebuilt.TextLength());
    for (size_t i = 0; i < rebuilt.LineCount(); ++i) {
        auto [aStart, aEnd] = appended.LineRange(i);
        auto [rStart, rEnd] = rebuilt.LineRange(i);
        EXPECT_EQ(aStart, rStart);
        EXPECT_EQ(aEnd, rEnd);
    }
    for (size_t off = 0; off <= whole.size(); ++off)
        EXPECT_EQ(appended.LineFromOffset(off), rebuilt.LineFromOffset(off));
}

TEST(LineIndex, AppendOneCharacterAtATimeMatchesRebuild) {
    // The degenerate chunking: every character its own Append. Catches
    // sentinel-handling bugs that a coarser split hides, because here every
    // possible boundary is exercised.
    const std::wstring whole = L"a\nbb\n\nccc\n";
    LineIndex idx;
    idx.Rebuild(L"");
    for (size_t i = 0; i < whole.size(); ++i)
        EXPECT_TRUE(idx.Append(whole.substr(i, 1), i));

    LineIndex rebuilt;
    rebuilt.Rebuild(whole);
    EXPECT_EQ(idx.LineCount(), rebuilt.LineCount());
    for (size_t i = 0; i < rebuilt.LineCount(); ++i) {
        auto [aStart, aEnd] = idx.LineRange(i);
        auto [rStart, rEnd] = rebuilt.LineRange(i);
        EXPECT_EQ(aStart, rStart);
        EXPECT_EQ(aEnd, rEnd);
    }
}

TEST(LineIndex, AppendPreservesExistingLineNumbers) {
    // The reason Append exists at all: existing lines keep their NUMBERS, which
    // is what lets a layout cache keyed by line number survive an append with no
    // invalidation. If line numbers shifted, every append would flush the cache
    // and the log path would be no faster than SetText.
    LineIndex idx;
    idx.Rebuild(L"keep0\nkeep1\n");
    auto [s0, e0] = idx.LineRange(0);
    auto [s1, e1] = idx.LineRange(1);

    idx.Append(L"new2\nnew3\n", idx.TextLength());

    auto [s0b, e0b] = idx.LineRange(0);
    auto [s1b, e1b] = idx.LineRange(1);
    EXPECT_EQ(s0, s0b);
    EXPECT_EQ(e0, e0b);
    EXPECT_EQ(s1, s1b);
    EXPECT_EQ(e1, e1b);
    EXPECT_EQ(idx.LineCount(), size_t{5});  // keep0, keep1, new2, new3, ""
}

TEST(LineIndex, AppendAtWrongOffsetIsRefusedAndChangesNothing) {
    // A desync must be reported, not absorbed. Absorbing it would produce an
    // index whose offsets point at the wrong characters — silently wrong text on
    // screen, with nothing to trace it back to.
    LineIndex idx;
    idx.Rebuild(L"abc");
    EXPECT_FALSE(idx.Append(L"xyz", 0));    // stale offset
    EXPECT_FALSE(idx.Append(L"xyz", 99));   // impossible offset
    ExpectLines(idx, L"abc", {L"abc"});     // untouched
    EXPECT_TRUE(idx.Append(L"xyz", 3));     // the correct append point
    ExpectLines(idx, L"abcxyz", {L"abcxyz"});
}

TEST(LineIndex, AppendEmptyIsANoOp) {
    LineIndex idx;
    idx.Rebuild(L"a\nb");
    EXPECT_TRUE(idx.Append(L"", 3));
    ExpectLines(idx, L"a\nb", {L"a", L"b"});
}

TEST(LineIndex, AppendToDefaultConstructedIndex) {
    // The log case with no SetText first: a fresh index must accept an append at
    // offset 0 without a preparatory Rebuild.
    LineIndex idx;
    EXPECT_TRUE(idx.Append(L"line\n", 0));
    ExpectLines(idx, L"line\n", {L"line", L""});
}

// --- TrimFront: the ring-buffer path -------------------------------------

TEST(LineIndex, TrimFrontDropsLeadingLinesAndRebasesOffsets) {
    // After trimming, offsets must index the TRIMMED buffer — the caller erased
    // those characters, so an un-rebased index would read past the end.
    const std::wstring before = L"drop0\ndrop1\nkeep2\nkeep3";
    LineIndex idx;
    idx.Rebuild(before);

    const size_t removed = idx.LineRange(2).first;  // start of the first kept line
    idx.TrimFront(2, removed);

    const std::wstring after = before.substr(removed);
    ExpectLines(idx, after, {L"keep2", L"keep3"});
}

TEST(LineIndex, TrimFrontMatchesRebuildOfTheTrimmedText) {
    // Same cross-check as the Append invariant: trimming must land where a fresh
    // Rebuild of the surviving text would.
    const std::wstring before = L"a\nbb\nccc\ndddd\n";
    LineIndex trimmed;
    trimmed.Rebuild(before);
    const size_t removed = trimmed.LineRange(2).first;
    trimmed.TrimFront(2, removed);

    LineIndex rebuilt;
    const std::wstring after = before.substr(removed);
    rebuilt.Rebuild(after);

    EXPECT_EQ(trimmed.LineCount(), rebuilt.LineCount());
    EXPECT_EQ(trimmed.TextLength(), rebuilt.TextLength());
    for (size_t i = 0; i < rebuilt.LineCount(); ++i) {
        auto [tStart, tEnd] = trimmed.LineRange(i);
        auto [rStart, rEnd] = rebuilt.LineRange(i);
        EXPECT_EQ(tStart, rStart);
        EXPECT_EQ(tEnd, rEnd);
    }
    for (size_t off = 0; off <= after.size(); ++off)
        EXPECT_EQ(trimmed.LineFromOffset(off), rebuilt.LineFromOffset(off));
}

TEST(LineIndex, TrimFrontThenAppendStaysConsistent) {
    // The steady state of a capped log: trim the head, append the tail, forever.
    // Append verifies its offset against TextLength(), so a TrimFront that failed
    // to update TextLength() would make the very next append refuse.
    LineIndex idx;
    idx.Rebuild(L"old0\nold1\nold2\n");
    const size_t removed = idx.LineRange(1).first;
    idx.TrimFront(1, removed);

    EXPECT_TRUE(idx.Append(L"new\n", idx.TextLength()));
    ExpectLines(idx, L"old1\nold2\nnew\n", {L"old1", L"old2", L"new", L""});
}

TEST(LineIndex, TrimFrontZeroIsANoOp) {
    LineIndex idx;
    idx.Rebuild(L"a\nb\n");
    idx.TrimFront(0, 0);
    ExpectLines(idx, L"a\nb\n", {L"a", L"b", L""});
}

TEST(LineIndex, TrimFrontAllLinesLeavesOneEmptyLine) {
    // Trimming everything must not produce a zero-line index — LineCount() >= 1
    // is relied on by every caller.
    LineIndex idx;
    idx.Rebuild(L"a\nb\nc");
    idx.TrimFront(idx.LineCount(), idx.TextLength());
    ExpectLines(idx, L"", {L""});
    EXPECT_EQ(idx.LineFromOffset(0), size_t{0});
    EXPECT_TRUE(idx.Append(L"fresh", 0));  // usable again afterwards
}

TEST(LineIndex, TrimFrontBeyondLineCountClampsToClear) {
    LineIndex idx;
    idx.Rebuild(L"a\nb");
    idx.TrimFront(99, 3);
    ExpectLines(idx, L"", {L""});
}

TEST(LineIndex, RepeatedTrimAndAppendKeepsLineCountBounded) {
    // The ring buffer running for a while: cap at 4 lines, feed 200. This is the
    // shape of the log path's memory behaviour, and the assertion is the one the
    // user will check on real hardware — the line count settles instead of
    // growing.
    LineIndex idx;
    std::wstring buffer;
    const size_t cap = 4;
    for (int i = 0; i < 200; ++i) {
        const std::wstring row = L"row\n";
        EXPECT_TRUE(idx.Append(row, buffer.size()));
        buffer += row;
        if (idx.LineCount() > cap) {
            const size_t drop = idx.LineCount() - cap;
            const size_t removed = idx.LineRange(drop).first;
            idx.TrimFront(drop, removed);
            buffer.erase(0, removed);
        }
        EXPECT_TRUE(idx.LineCount() <= cap);
        EXPECT_EQ(idx.TextLength(), buffer.size());
    }
    // And the surviving content is still coherent, not just correctly sized.
    for (size_t i = 0; i + 1 < idx.LineCount(); ++i) {
        auto [start, end] = idx.LineRange(i);
        EXPECT_TRUE(buffer.substr(start, end - start) == L"row");
    }
}
