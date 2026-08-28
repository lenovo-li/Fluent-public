// LineIndex.cpp — logical-line offset index over a UTF-16 text buffer.

#include "LineIndex.h"
#include <algorithm>

namespace fluent {

void LineIndex::Rebuild(std::wstring_view text) {
    starts_.clear();
    starts_.reserve(text.size() / 40 + 2);  // ~40 chars/line guess; grows if wrong
    starts_.push_back(0);                   // line 0 always starts at offset 0
    textLength_ = text.size();

    // Record the offset AFTER each '\n' as the start of the next line. A
    // trailing '\n' therefore creates a final empty line, which is the counting
    // convention documented in the header.
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == L'\n') starts_.push_back(i + 1);

    starts_.push_back(textLength_);         // sentinel (see header)
}

bool LineIndex::Append(std::wstring_view added, size_t addedStartOffset) {
    // The caller must be appending exactly at the index's end. A mismatch means
    // the buffer and the index have diverged; refuse rather than emit offsets
    // that point at the wrong characters, and leave the index untouched so the
    // caller can recover with Rebuild.
    if (addedStartOffset != textLength_) return false;
    if (added.empty()) return true;

    starts_.pop_back();                     // drop the sentinel; re-added below
    const size_t oldLength = textLength_;
    textLength_ += added.size();

    for (size_t i = 0; i < added.size(); ++i)
        if (added[i] == L'\n') starts_.push_back(oldLength + i + 1);

    starts_.push_back(textLength_);
    return true;
}

void LineIndex::TrimFront(size_t lineCount, size_t bytesRemoved) {
    if (lineCount == 0) return;

    if (lineCount >= LineCount()) {
        // Dropping every line leaves an empty buffer — which by the counting
        // convention is still one (empty) line, not zero.
        starts_.assign({size_t{0}, size_t{0}});
        textLength_ = 0;
        return;
    }

    // The shift is DERIVED from the index (starts_[lineCount] is by definition
    // the first surviving line's offset) rather than taken from bytesRemoved.
    // The two must agree; if they don't, the caller's buffer and this index have
    // already diverged and neither value repairs that — but using the derived
    // one at least keeps the index internally consistent, so LineRange /
    // LineFromOffset stay self-coherent instead of returning offsets that are
    // wrong by a different amount on every line.
    const size_t shift = starts_[lineCount];
    (void)bytesRemoved;

    starts_.erase(starts_.begin(), starts_.begin() + static_cast<ptrdiff_t>(lineCount));
    for (size_t& offset : starts_) offset -= shift;
    textLength_ -= shift;
}

size_t LineIndex::LineCount() const {
    // starts_ holds every line start plus the sentinel, so the line count is one
    // less than its size. The invariant keeps size() >= 2 (an empty buffer is
    // {0, 0}), so this never underflows and never returns 0.
    return starts_.size() - 1;
}

std::pair<size_t, size_t> LineIndex::LineRange(size_t i) const {
    // Out of range is a caller bug, but clamping beats reading past the vector:
    // a virtualized draw loop derives `i` from a scroll offset, and one stale
    // offset would otherwise be a crash instead of a wrong-looking row.
    if (i >= LineCount()) i = LineCount() - 1;

    const size_t start = starts_[i];
    // Any line but the last is terminated by the '\n' immediately before the
    // next line's start, and that terminator is not part of the display line.
    // The last line has no terminator, so it runs to the end of the buffer.
    const size_t end = (i + 1 < LineCount()) ? starts_[i + 1] - 1 : textLength_;
    return {start, end};
}

size_t LineIndex::LineFromOffset(size_t offset) const {
    if (offset > textLength_) offset = textLength_;

    // upper_bound gives the first start strictly greater than `offset`; the line
    // containing `offset` is the one before it.
    auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
    if (it == starts_.begin()) return 0;
    size_t line = static_cast<size_t>((it - starts_.begin()) - 1);

    // Two cases land on the sentinel and must be pulled back to a real line:
    // offset == textLength_ (the caret at end of document), and a trailing '\n'
    // whose empty final line shares the sentinel's offset. Both mean "the last
    // line"; the sentinel itself is not a line anyone can point at.
    if (line >= LineCount()) line = LineCount() - 1;
    return line;
}

} // namespace fluent
