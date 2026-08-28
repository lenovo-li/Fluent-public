// WrapExtentMap.h — where each paragraph sits vertically under soft wrap, when
// most paragraphs have never been laid out.
//
// This is the piece that makes Wrap-mode virtualization possible, and the reason it is
// hard is worth stating before the API: under soft wrap, "how tall is this document"
// cannot be answered without wrapping every paragraph, and the scrollbar needs an
// answer on the first frame. NoWrap has no such problem — line count times a constant
// line height, exact and O(1) (see LineIndex). Wrap has no equivalent, because a
// paragraph's visual line count depends on where the glyphs happen to break at this
// particular width.
//
// THE TRADE THIS CLASS IMPLEMENTS, chosen deliberately over the alternatives:
//
//   * lay out everything up front  -> the extent is exact immediately, and opening a
//                                     28 MB document takes 3.7 seconds (measured).
//                                     This is what Wrap did before.
//   * lay out in the background    -> the extent converges while the CPU burns, and
//                                     touching a not-yet-measured region blocks until
//                                     it catches up. This is Scintilla's "idle
//                                     wrapping"; see the two upstream bugs cited in
//                                     internal documentation.
//   * ESTIMATE, correct on contact -> the extent is approximate at first and becomes
//                                     exact where the user has actually been. No stall,
//                                     no background CPU, no worst case.
//
// The third is what this is, and it is what VS Code and Sublime do. Its one visible
// cost is that THE SCROLLBAR MOVES SLIGHTLY as unmeasured regions are scrolled into
// view and their real heights replace the estimates. That is not a defect to be fixed
// later; it is the price of not stalling, it was accepted explicitly before this was
// written, and any change here that seems to "fix the jumping" is really re-choosing
// one of the other two rows above.
//
// EVERYTHING IS COUNTED IN VISUAL LINES, NOT DIPs. A paragraph's height is its visual
// line count times a constant line height, so the bookkeeping is exact integer
// arithmetic and only the query multiplies by the line height. Storing float heights
// would accumulate rounding across hundreds of thousands of incremental updates, and
// the symptom of that — a total height that drifts by a few DIP per edit — stays
// invisible until the scrollbar can no longer reach the last line.
//
// THE LINE HEIGHT IS A QUERY PARAMETER, NOT STATE. TextArea already owns it (memoized
// per font size and DPI), and a second copy here would be a cache to keep in step
// across font changes, DPI changes and theme changes — three invalidation paths for a
// value the caller can simply pass. Every DIP-valued method therefore takes it.
//
// COST MODEL, which is the whole point:
//   Reset                       O(paragraphs), pure arithmetic, no DWrite.
//   SetMeasured                 O(1) amortized.
//   VisualLineBefore / ParagraphFromVisualLine / ParagraphSpanForBand
//                               O(blocks) once per frame (a lazy prefix rebuild, only
//                               when something was measured since the last query) plus
//                               O(kBlockSize) per call.
//   TotalVisualLines            O(1).
//
// The lazy rebuild is the load-bearing detail. The obvious implementation — push the
// delta into every following block on each SetMeasured — is O(paragraphs / kBlockSize)
// PER MEASUREMENT, so a frame measuring the 50 paragraphs on screen pays that 50
// times. Marking the prefix dirty instead and rebuilding once per query makes it
// O(paragraphs / kBlockSize) PER FRAME, the same shape as the draw loop itself.
//
// WHY BLOCKED PREFIX SUMS AND NOT A FENWICK TREE. A tree gives O(log n) for both
// update and query instead of O(n / kBlockSize) and O(kBlockSize). At the sizes here (a
// few thousand blocks) the flat array wins on constant factor and cache behaviour, it
// is a fraction of the code, and — with the lazy rebuild above — the update cost is
// already off the per-measurement path. This was settled in the plan; the tree is not
// an improvement waiting to be made.
//
// CHARACTER COUNTS ARE NOT STORED HERE. Reset and ReEstimateUnmeasured take the
// LineIndex and read paragraph lengths from it, because the index already holds every
// paragraph boundary and duplicating them would be another 8 bytes per paragraph for a
// value that must then be kept in step with the index.
//
// MEMORY: 4 bytes per paragraph (the visual line count) + 1 byte per paragraph (the
// measured flag) + 4 bytes per block. 320 K paragraphs is ~1.6 MB. Stated because it is
// a real cost of the design, alongside LineIndex's 8 bytes per line.
//
// Headless-testable by construction: no DWrite, no window, no D2D. The measurement
// itself is the caller's job (only it can call DWrite); this class records the result
// and answers geometry questions about it.
#pragma once

#include "LineIndex.h"
#include "VisibleLines.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fluent {

// How many visual lines a paragraph of `chars` characters is GUESSED to occupy at
// `charsPerLine`. Pure, so the estimator is checkable without a map.
//
// Always at least 1: an empty paragraph still occupies one visual line (the caret can
// sit on it), and a zero would let a run of blank lines collapse to no height at all —
// which shows up as a caret that cannot be placed on them.
inline uint32_t EstimateVisualLines(size_t chars, float charsPerLine) {
    if (!(charsPerLine > 0.0f)) return 1u;        // also catches NaN
    const float up = std::ceil(static_cast<float>(chars) / charsPerLine);
    if (!(up >= 1.0f)) return 1u;
    // Clamp before the narrowing cast: a charsPerLine near zero would otherwise
    // overflow uint32 and wrap to a tiny count, making a huge paragraph report as
    // short — a failure that looks like a layout bug rather than an arithmetic one.
    constexpr float kMax = 16777216.0f;           // 2^24 visual lines, far past sane
    return static_cast<uint32_t>(up < kMax ? up : kMax);
}

class WrapExtentMap {
public:
    // Paragraphs per block for the prefix-sum array. 256 keeps the in-block linear scan
    // short while making the block array ~1/256 the paragraph count, so both sides of
    // the O(blocks) + O(kBlockSize) cost stay small at every realistic size.
    static constexpr size_t kBlockSize = 256;

    // Should the estimates be rebuilt because the characters-per-line figure has moved?
    //
    // Pure and static so the policy is testable on its own, and because the caller needs
    // to ask before deciding to spend an O(paragraphs) pass. The threshold guards that
    // cost: re-estimating on every measurement would put an O(document) pass back on the
    // scroll path, which is precisely what this class exists to remove.
    //
    // A `basis` of zero means "nothing has been estimated yet", so any real value is
    // worth applying.
    static bool ShouldReEstimate(float basis, float current) {
        if (!(basis > 0.0f)) return current > 0.0f;
        if (!(current > 0.0f)) return false;
        return std::fabs(current - basis) / basis > kReEstimateFraction;
    }

    // Discard everything and rebuild estimates for every paragraph in `lines` at
    // `charsPerLine`. This is the resize path as well as the load path: a width change
    // invalidates every MEASURED value (a paragraph wrapped at 800 DIP breaks
    // differently at 600), so there is nothing to preserve and the honest move is to
    // start over from estimates. O(paragraphs) arithmetic — no DWrite, which is what
    // makes a resize drag affordable at all.
    void Reset(const LineIndex& lines, float charsPerLine);

    // Recompute the estimate for every paragraph that has NOT been measured, at a new
    // charsPerLine. Called when the rolling average has drifted (see ShouldReEstimate),
    // so the unvisited part of the document reflects what the visited part turned out to
    // be. Measured paragraphs are left exactly as they are — an estimate must never
    // overwrite a real measurement, which is the invariant that makes the extent
    // monotonically more accurate rather than oscillating.
    void ReEstimateUnmeasured(const LineIndex& lines, float charsPerLine);

    // Record that paragraph `p` really occupies `visualLines` visual lines. Idempotent
    // and O(1); a repeat call with the same value does no work, which is what makes
    // redrawing an already-measured band free.
    void SetMeasured(size_t p, uint32_t visualLines);

    // Feed one measurement into the characters-per-line rolling average. Separate from
    // SetMeasured because the average is about the DOCUMENT's typical density while
    // SetMeasured is about one paragraph's geometry: a caller measuring a paragraph for
    // a reason other than drawing it (a caret query on a paragraph scrolled off screen)
    // should still improve the geometry without skewing the average.
    void NoteMeasuredCharsPerLine(size_t chars, uint32_t visualLines);

    // The rolling average, or the seed passed to Reset before any measurement.
    float MeasuredCharsPerLine() const { return charsPerLine_; }
    // The value the CURRENT estimates were computed with. Pair it with
    // MeasuredCharsPerLine() and ShouldReEstimate() to decide on a re-estimate pass.
    float EstimateBasis() const { return estimateBasis_; }

    // --- Geometry -------------------------------------------------------------

    size_t ParagraphCount() const { return counts_.size(); }
    // Visual lines in the whole document: measured where known, estimated elsewhere.
    uint32_t TotalVisualLines() const { return totalLines_; }
    float TotalHeightDip(float lineHeightDip) const {
        return static_cast<float>(totalLines_) * lineHeightDip;
    }
    // How many paragraphs carry a real measurement. Diagnostic / test surface: the
    // direct observable for "did the draw loop actually measure what it drew", which no
    // rendered output can show.
    size_t MeasuredCount() const { return measuredCount_; }
    bool IsMeasured(size_t p) const { return p < flags_.size() && flags_[p] != 0; }
    uint32_t VisualLinesAt(size_t p) const {
        return p < counts_.size() ? counts_[p] : 1u;
    }

    // Visual lines before paragraph `p`, i.e. the index of its first visual line.
    // `p == ParagraphCount()` is legal and returns the document total, so a caller can
    // ask for the end without a special case.
    uint32_t VisualLineBefore(size_t p) const;
    // The paragraph containing visual line `line`, clamped into range. Empty map -> 0.
    size_t ParagraphFromVisualLine(uint32_t line) const;

    float ParagraphTopDip(size_t p, float lineHeightDip) const {
        return static_cast<float>(VisualLineBefore(p)) * lineHeightDip;
    }
    float ParagraphHeightDip(size_t p, float lineHeightDip) const {
        return static_cast<float>(VisualLinesAt(p)) * lineHeightDip;
    }
    size_t ParagraphFromYDip(float y, float lineHeightDip) const;

    // Paragraphs intersecting [bandTop, bandTop + bandHeight). Same contract as
    // VisibleLineSpan, and the same return type on purpose: the two modes' draw loops
    // then differ only in how a row's height is obtained, not in shape.
    LineSpan ParagraphSpanForBand(float bandTopDip, float bandHeightDip,
                                  float lineHeightDip) const;

private:
    // Re-estimating is O(paragraphs), so it is worth doing only once the average has
    // moved enough to visibly change the extent. 25% is chosen to be well clear of the
    // noise a few atypical paragraphs produce while still catching a genuine shift in
    // the document's character (prose to code, Latin to CJK).
    static constexpr float kReEstimateFraction = 0.25f;

    // Rebuild blockPrefix_ from blockLines_ if a measurement has invalidated it. Lazy:
    // a frame measures many paragraphs then asks a few questions, and rebuilding per
    // measurement is the cost this design specifically avoids.
    void EnsurePrefix() const;

    // Visual lines per paragraph: measured where flags_[i], estimated otherwise.
    std::vector<uint32_t> counts_;
    // 1 = counts_[i] came from a real layout. A byte per paragraph rather than
    // vector<bool>: the bit-packing would save 7/8 of a byte and cost a shift-and-mask
    // on a path that runs per visible paragraph per frame.
    std::vector<uint8_t> flags_;
    // Visual lines contained in each block of kBlockSize paragraphs. Maintained exactly
    // by every SetMeasured, in O(1).
    std::vector<uint32_t> blockLines_;
    // Visual lines before each block. Derived from blockLines_, rebuilt lazily.
    mutable std::vector<uint32_t> blockPrefix_;
    mutable bool prefixDirty_ = true;

    uint32_t totalLines_ = 0;
    size_t measuredCount_ = 0;
    // The rolling average, and the value the current estimates were computed with.
    // Keeping both is what lets ShouldReEstimate ask "have the estimates gone stale"
    // rather than "has the average moved", which is true almost constantly.
    float charsPerLine_ = 1.0f;
    float estimateBasis_ = 0.0f;
    // Rolling-average accumulators. A running total rather than a decayed average so
    // early noise cannot pin the value: the first paragraph measured is often atypical
    // (a title, a blank line), and a decayed average would let it dominate until enough
    // samples arrived to wash it out.
    uint64_t densityChars_ = 0;
    uint64_t densityLines_ = 0;
};

} // namespace fluent
