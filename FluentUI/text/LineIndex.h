// LineIndex.h — logical-line offset index over a UTF-16 text buffer.
//
// The enabling data structure for text virtualization. It answers the three
// questions a virtualized text view asks on every frame, none of which the
// current "one IDWriteTextLayout for the whole document" design can answer
// without having already laid out the entire buffer:
//
//   * how many lines are there?          -> LineCount(), O(1)
//   * what characters are on line i?     -> LineRange(i), O(1)
//   * which line holds character n?      -> LineFromOffset(n), O(log lines)
//
// With those, plus a fixed line height (which NoWrap guarantees), drawing costs
// O(visible lines) instead of O(document) — exactly the shape
// TreeView::DrawRowsToSurface already has, and for exactly the same reason.
//
// WHY THIS IS A LOGICAL-LINE INDEX, AND WHAT THAT COMMITS US TO. A line here is
// a run of characters between newlines: the index is built by scanning for '\n'
// and nothing else. That makes it correct only where one logical line renders as
// one visual line, i.e. NoWrap. Under soft wrap a single logical line becomes N
// visual lines, N depends on the wrap width, and every N changes when the window
// resizes — which is the whole reason soft-wrap virtualization is a separate,
// harder problem (see internal documentation, phase
// 3). Callers that speak in "visual lines" may use this class directly *while in
// NoWrap mode*, where the two coincide; nothing in this type should grow a
// wrap-width parameter. When soft wrap arrives it wants a different index
// layered above this one, not a mutation of it.
//
// The buffer is expected to have newlines already normalized to '\n' — TextArea's
// SanitizeInput does that on every input path, so a lone '\r' cannot reach here.
// A stray '\r' would simply not end a line, which is the same thing DWrite would
// do with the character; this class does not try to be more clever than the
// renderer it feeds.
//
// LINE COUNTING CONVENTION: LineCount() == (number of '\n') + 1. So an empty
// buffer has ONE line (empty, range [0,0)), and "a\n" has TWO lines ("a" and a
// trailing empty one). This matches Notepad / VS Code / every editor: a trailing
// newline is a line the caret can sit on, not a decoration on the previous line.
// It also means LineCount() is never zero, which removes an empty-case branch
// from every caller — no "if there are no lines" path can exist.
//
// MEMORY COST, stated up front because it is the price this design pays. One
// size_t per line. A 40 MB (20 M UTF-16 characters) log at ~80 characters per
// line is ~250 K lines ≈ 2 MB of index on x64; at 40 characters per line, ~4 MB.
// That is a few percent of the text itself and it is the deliberate trade: an
// O(1) line lookup in exchange for 8 bytes per line. A delta-encoded or chunked
// index would shrink it, at the cost of making LineRange non-constant — not
// worth it until the index itself shows up in a memory profile.
//
// Offsets are UTF-16 code-unit indices into the buffer, matching TextEditBase's
// caret / selection units. This class stores no text of its own; it holds
// offsets into a buffer the caller owns, and the caller is responsible for
// keeping the two in step (Rebuild after an arbitrary edit, Append after an
// append, TrimFront alongside erasing the front of the buffer).
//
// Headless-testable by construction: no DWrite, no window, no D2D. That is a
// repo convention ("Pure functions get extracted for testability") and here it
// is also the only way to test the log path's core invariant — that N successive
// Appends land in the same state as one Rebuild of the concatenation.
#pragma once

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace fluent {

class LineIndex {
public:
    // Full rebuild: scan the whole buffer for newlines. O(n) in characters —
    // tens of milliseconds for tens of MB, paid once per SetText. Any edit that
    // is not a pure append or a front-trim has to come through here, because an
    // insertion in the middle shifts every following line start.
    void Rebuild(std::wstring_view text);

    // Append-only fast path (the log case). Scans ONLY the added range, so cost
    // is O(added), not O(document). Every existing entry stays valid and every
    // existing line keeps its number — which is what lets a layout cache keyed
    // by line number survive an append untouched. That property, not the scan
    // cost, is the real reason this method exists.
    //
    // `addedStartOffset` must equal TextLength() (the append point). It is a
    // parameter rather than implied so a caller that has diverged from the index
    // is caught here instead of silently producing offsets that point into the
    // wrong characters: a mismatch is treated as a desync and forces the caller
    // back through Rebuild by returning false without touching anything.
    bool Append(std::wstring_view added, size_t addedStartOffset);

    // Discard the first `lineCount` logical lines, as happens when a ring-buffer
    // log caps its line count. `bytesRemoved` must equal the number of UTF-16
    // code units that were erased from the front of the buffer (i.e., the start
    // offset of the first KEPT line). All surviving offsets are decremented by
    // that amount so they remain valid indices into the trimmed buffer.
    //
    // After TrimFront, line numbers reset: what was line (lineCount) is now
    // line 0. A layout cache keyed by line number must therefore be invalidated
    // (at minimum the first lineCount entries, but a full flush is simpler and
    // correct). The line-number shift is what makes append-without-invalidating
    // the normal case and trim-with-invalidation the capping exception.
    void TrimFront(size_t lineCount, size_t bytesRemoved);

    // --- Queries -------------------------------------------------------------

    // Number of logical lines. Always >= 1 (see counting convention above).
    size_t LineCount() const;

    // Total number of UTF-16 code units the index was built over. Used by
    // Append() to verify the append point; callers may also use it to keep a
    // parallel text buffer in step.
    size_t TextLength() const { return textLength_; }

    // Half-open character range [start, end) for line i, where end does NOT
    // include the '\n' terminator (the terminator belongs to no display line).
    // Requires i < LineCount(); behaviour is undefined for out-of-range i.
    std::pair<size_t, size_t> LineRange(size_t i) const;

    // The index of the logical line that contains `offset`. O(log LineCount())
    // via binary search on the starts_ array. Used by caret queries and by
    // hit-test to translate a character offset back to a (line, column) pair.
    //
    // Edge cases that must be consistent:
    //   offset == 0                    -> line 0
    //   offset == start of line i      -> i  (the newline at end of i-1
    //                                        is not 'in' line i-1's range)
    //   offset == end of last line     -> last line index (== LineCount()-1)
    //   offset > TextLength()          -> clamped to last line
    size_t LineFromOffset(size_t offset) const;

private:
    // starts_[i] is the offset of line i's first character; starts_[0] is always
    // 0. One extra entry past the last line holds textLength_ as a SENTINEL, so
    // the invariant is starts_.size() == LineCount() + 1.
    //
    // The sentinel exists so LineFromOffset's binary search can be a plain
    // upper_bound over the whole vector: without a past-the-end entry, an offset
    // inside the final line finds no greater start and the search has to special-
    // case it. Paying one size_t to delete that branch is worth it — that branch
    // is on the caret path, which is the one place an off-by-one is guaranteed to
    // be noticed.
    //
    // Initialized to {0, 0} — a default-constructed index is a valid index over
    // an empty buffer (one empty line), not an empty structure that every
    // accessor must first check. Nothing here may leave starts_.size() < 2.
    std::vector<size_t> starts_{0, 0};
    size_t textLength_ = 0;
};

} // namespace fluent
