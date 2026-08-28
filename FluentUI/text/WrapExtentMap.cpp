// WrapExtentMap.cpp — per-paragraph vertical extent under soft wrap.

#include "WrapExtentMap.h"
#include <algorithm>

namespace fluent {

namespace {
size_t BlockCountFor(size_t paragraphs) {
    return (paragraphs + WrapExtentMap::kBlockSize - 1) / WrapExtentMap::kBlockSize;
}
} // namespace

void WrapExtentMap::Reset(const LineIndex& lines, float charsPerLine) {
    const size_t n = lines.LineCount();
    charsPerLine_ = charsPerLine > 0.0f ? charsPerLine : 1.0f;
    estimateBasis_ = charsPerLine_;

    counts_.assign(n, 1u);
    flags_.assign(n, uint8_t{0});
    blockLines_.assign(BlockCountFor(n), uint32_t{0});
    measuredCount_ = 0;
    totalLines_ = 0;

    // The density accumulators are deliberately NOT cleared. Reset runs on every resize,
    // and the typical characters-per-line at a new width is the old figure scaled by the
    // width ratio — not unknown. Discarding the samples would make the first frame after
    // every resize estimate from the caller's seed again, and that is the one moment the
    // estimate is most visible (the scrollbar re-converges under the user's hand). The
    // caller passes a width-scaled seed in; the retained samples keep refining it.

    for (size_t i = 0; i < n; ++i) {
        auto [start, end] = lines.LineRange(i);
        const uint32_t est = EstimateVisualLines(end - start, charsPerLine_);
        counts_[i] = est;
        blockLines_[i / kBlockSize] += est;
        totalLines_ += est;
    }
    prefixDirty_ = true;
}

void WrapExtentMap::ReEstimateUnmeasured(const LineIndex& lines, float charsPerLine) {
    if (!(charsPerLine > 0.0f)) return;
    charsPerLine_ = charsPerLine;
    estimateBasis_ = charsPerLine;
    const size_t n = std::min(counts_.size(), lines.LineCount());

    for (size_t i = 0; i < n; ++i) {
        if (flags_[i]) continue;                  // never overwrite a measurement
        auto [start, end] = lines.LineRange(i);
        const uint32_t est = EstimateVisualLines(end - start, charsPerLine_);
        const uint32_t old = counts_[i];
        if (est == old) continue;
        counts_[i] = est;
        // Signed delta through int64: est may be smaller than old, and doing this in the
        // unsigned domain would wrap totalLines_ to an enormous value — a document whose
        // scrollbar suddenly believes it is billions of lines tall.
        const int64_t delta = static_cast<int64_t>(est) - static_cast<int64_t>(old);
        blockLines_[i / kBlockSize] = static_cast<uint32_t>(
            static_cast<int64_t>(blockLines_[i / kBlockSize]) + delta);
        totalLines_ = static_cast<uint32_t>(static_cast<int64_t>(totalLines_) + delta);
    }
    prefixDirty_ = true;
}

void WrapExtentMap::SetMeasured(size_t p, uint32_t visualLines) {
    if (p >= counts_.size()) return;
    if (visualLines < 1u) visualLines = 1u;       // see EstimateVisualLines

    const bool wasMeasured = flags_[p] != 0;
    const uint32_t old = counts_[p];
    if (wasMeasured && old == visualLines) return;   // nothing to do at all

    if (!wasMeasured) {
        flags_[p] = 1;
        ++measuredCount_;
    }
    if (old == visualLines) return;               // flag flipped, geometry unchanged

    counts_[p] = visualLines;
    const int64_t delta = static_cast<int64_t>(visualLines) - static_cast<int64_t>(old);
    blockLines_[p / kBlockSize] = static_cast<uint32_t>(
        static_cast<int64_t>(blockLines_[p / kBlockSize]) + delta);
    totalLines_ = static_cast<uint32_t>(static_cast<int64_t>(totalLines_) + delta);
    // Only the PREFIX is invalidated, not recomputed — the lazy step that keeps measuring
    // O(1). See the header for why per-measurement propagation is what is being avoided.
    prefixDirty_ = true;
}

void WrapExtentMap::NoteMeasuredCharsPerLine(size_t chars, uint32_t visualLines) {
    // A paragraph on a single visual line never reached a line break, so it says nothing
    // about where breaks fall — only that it was shorter than the width. Including those
    // would drag the average toward however short the document's short paragraphs happen
    // to be, and a document of mostly-short paragraphs would then badly over-estimate its
    // long ones. Only multi-line paragraphs actually sampled a break.
    if (visualLines < 2 || chars == 0) return;
    densityChars_ += chars;
    densityLines_ += visualLines;
    if (densityLines_ > 0) {
        charsPerLine_ = static_cast<float>(densityChars_) /
                        static_cast<float>(densityLines_);
    }
    if (!(charsPerLine_ > 0.0f)) charsPerLine_ = 1.0f;
}

void WrapExtentMap::EnsurePrefix() const {
    if (!prefixDirty_) return;
    blockPrefix_.resize(blockLines_.size());
    uint32_t running = 0;
    for (size_t b = 0; b < blockLines_.size(); ++b) {
        blockPrefix_[b] = running;
        running += blockLines_[b];
    }
    prefixDirty_ = false;
}

uint32_t WrapExtentMap::VisualLineBefore(size_t p) const {
    if (counts_.empty() || p == 0) return 0;
    if (p >= counts_.size()) return totalLines_;   // the document end, no special case
    EnsurePrefix();
    const size_t block = p / kBlockSize;
    uint32_t total = blockPrefix_[block];
    for (size_t i = block * kBlockSize; i < p; ++i) total += counts_[i];
    return total;
}

size_t WrapExtentMap::ParagraphFromVisualLine(uint32_t line) const {
    if (counts_.empty()) return 0;
    if (line >= totalLines_) return counts_.size() - 1;

    EnsurePrefix();
    // First block starting strictly past `line`, minus one, is the block holding it.
    auto it = std::upper_bound(blockPrefix_.begin(), blockPrefix_.end(), line);
    const size_t block = (it == blockPrefix_.begin())
                       ? 0
                       : static_cast<size_t>((it - blockPrefix_.begin()) - 1);

    uint32_t running = blockPrefix_[block];
    const size_t first = block * kBlockSize;
    const size_t last = std::min(first + kBlockSize, counts_.size());
    for (size_t i = first; i < last; ++i) {
        running += counts_[i];
        if (running > line) return i;
    }
    return counts_.size() - 1;
}

size_t WrapExtentMap::ParagraphFromYDip(float y, float lineHeightDip) const {
    if (counts_.empty() || !(lineHeightDip > 0.0f)) return 0;
    if (y <= 0.0f) return 0;                       // includes an overscan band above 0
    // Convert to a visual-line index and resolve there. The block sums are exact
    // integers, so comparing against a float Y could pick the wrong block at a boundary —
    // a one-paragraph jump in the middle of a drag-select.
    const float lineF = y / lineHeightDip;
    if (!(lineF >= 0.0f)) return 0;
    if (lineF >= static_cast<float>(totalLines_)) return counts_.size() - 1;
    return ParagraphFromVisualLine(static_cast<uint32_t>(lineF));
}

LineSpan WrapExtentMap::ParagraphSpanForBand(float bandTopDip, float bandHeightDip,
                                             float lineHeightDip) const {
    LineSpan span;
    if (counts_.empty() || !(lineHeightDip > 0.0f) || !(bandHeightDip > 0.0f))
        return span;

    const float bottom = bandTopDip + bandHeightDip;
    if (bottom <= 0.0f) return span;               // band entirely above the document

    const float top = bandTopDip > 0.0f ? bandTopDip : 0.0f;
    if (top >= TotalHeightDip(lineHeightDip)) return span;   // entirely past the end

    span.first = ParagraphFromYDip(top, lineHeightDip);

    // Walk forward accumulating REAL per-paragraph heights rather than dividing: heights
    // are not uniform here, which is the entire difference from NoWrap, and dividing
    // would quietly reintroduce the fixed-height assumption.
    //
    // The band's bottom edge is EXCLUSIVE, matching VisibleLineSpan — without that, two
    // adjacent bands both draw the seam paragraph and both pay to lay it out.
    float y = ParagraphTopDip(span.first, lineHeightDip);
    size_t p = span.first;
    while (p < counts_.size() && y < bottom) {
        y += ParagraphHeightDip(p, lineHeightDip);
        ++p;
    }
    span.last = p;
    return span;
}

} // namespace fluent
