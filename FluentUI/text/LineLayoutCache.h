// LineLayoutCache.h — LRU cache of per-line IDWriteTextLayout objects.
//
// The second half of NoWrap virtualization. LineIndex says *which* lines are on
// screen; this says what each of them looks like, and remembers it so a scroll
// that brings a line back does not pay to lay it out again.
//
// The shape follows from the drawing loop: NoWrap draws one line at a time, so the
// natural cache key is the line number. A miss lays out that line's characters and
// nothing else; a hit reuses the layout. Sized to cover the viewport plus the
// overscan margins ScrollContentHost draws, the steady state of scrolling through a
// large document costs O(visible lines) of layout work per frame instead of
// O(document) — which is the entire point of the exercise.
//
// WHY LINE NUMBER AND NOT TEXT CONTENT is worth stating, because it is what makes
// the log path fast. Keying on the text would mean hashing every line's characters
// on every lookup, and would share entries between identical lines — attractive
// until you notice that appending to the end of a document leaves every existing
// line's number unchanged (LineIndex::Append guarantees exactly this), so a
// number-keyed cache survives an append with no invalidation at all. That is the
// property the whole high-throughput log design rests on.
//
// The flip side, and the one trap here: TrimFront renumbers lines. Dropping the
// first N lines makes old line N+k into new line k, so every cached entry now maps
// to the wrong text. A caller that trims MUST Clear(). There is no way for the
// cache to detect this on its own — the numbers it is handed look perfectly valid.
//
// INVALIDATION, and who is responsible for each kind:
//   * DPI or theme generation changed -> InvalidateIfStale (this class detects it)
//   * text changed (any edit)         -> caller calls Clear()
//   * wrap mode changed               -> caller calls Clear()
//   * lines renumbered by a trim      -> caller calls Clear()
// Only the first is self-detecting because only the first is visible in the
// arguments. The rest are the caller's contract, listed here so the caller can be
// checked against it.
//
// CAPACITY. The default covers a tall display plus generous overscan: at a ~20 DIP
// line height a 1440p viewport is ~70 lines, and ScrollContentHost's overscan is a
// few screens either way, so ~500 is the working set. 512 entries of
// (size_t + ComPtr + list node) is on the order of 20 KB of bookkeeping; the
// layouts themselves dominate, at roughly a few KB each for a normal-length line,
// so a full cache is single-digit MB. That is the deliberate trade — memory for
// not re-laying-out the same line twice.
//
// NOT HANDLED HERE: a single pathologically long line. Under NoWrap there is no
// reflow, so a 50 KB log line becomes a 50 KB layout, and DWrite's per-character
// metrics for it are expensive no matter how few of those characters are on screen.
// Laying out only the horizontally visible prefix is the fix — see §1.5b-3 in
// internal design notes. The `minCover` parameter below is the entry point: a caller that
// knows how far right it needs to see passes a character count; the cache builds or
// promotes the cheapest entry that covers at least that many characters. Long lines
// still produce large layouts when the full line is demanded (caret metrics, hit-test)
// but DrawLinesToSurface never asks for more than the visible window + a safety margin,
// so the steady-state cost of scrolling through a large file full of long lines is
// bounded by the visible columns, not the line length.
//
// THREAD SAFETY: none, by design. DWrite calls in this framework are UI-thread only.
#pragma once

#include "../fl_common.h"
#include <dwrite_3.h>
#include <cstddef>
#include <list>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fluent {

class LineLayoutCache {
public:
    // Layout box for an unwrapped line. NoWrap means the width constraint never
    // breaks a line, so it only has to be large enough not to clip: a bound, not a
    // wrap width. Height likewise bounds one line, not a document.
    static constexpr float kUnboundedWidthDip = 1.0e6f;
    static constexpr float kUnboundedHeightDip = 1.0e5f;

    explicit LineLayoutCache(size_t capacity = 512) : capacity_(capacity ? capacity : 1) {}

    // The layout for `lineNumber`, building it from `lineText` on a miss.
    //
    // `minCover` is the minimum number of characters the returned layout must span.
    // Pass kFullLine (default) for an unbounded layout; pass a smaller value in draw
    // paths that only need the visible prefix. A cached entry whose coverEnd >= minCover
    // is a hit and is promoted. An entry whose coverEnd < minCover is evicted and
    // rebuilt at the new cover — so a caret query after a short draw query silently
    // upgrades to the full layout without the caller knowing.
    //
    // Returns null when DWrite is unavailable or CreateTextLayout fails. A hit never
    // returns null: a failed build is not cached.
    //
    // `dpiScale` and `generation` flush the cache on a mismatch (see InvalidateIfStale).
    static constexpr size_t kFullLine = std::numeric_limits<size_t>::max();
    IDWriteTextLayout* Get(size_t lineNumber, std::wstring_view lineText,
                           IDWriteFactory* factory, IDWriteTextFormat* fmt,
                           float dpiScale, unsigned generation,
                           size_t minCover = kFullLine);

    // The number of characters the cached layout for `lineNumber` covers (i.e. the
    // coverEnd it was built with, clamped to the actual line length). Returns 0 if the
    // line is absent. Exposed for "layout is bounded" test assertions.
    size_t GetCoverEnd(size_t lineNumber) const;

    // Drop every entry. For the caller-detected invalidations listed in the header:
    // text edits, wrap-mode switches, and trims that renumber lines.
    void Clear();

    // Drop ONE entry. Returns true if it was present.
    //
    // WHY A SINGLE-LINE INVALIDATION EXISTS, given Clear() is always correct. An
    // append changes the content of exactly one existing line — the one that used to
    // be last. "a\nb" + "c\nd" becomes "a\nbc\nd": line 1 went from "b" to "bc",
    // while every other line kept both its number and its characters. Clear() would be
    // correct and would also throw away the entire visible screen's worth of layouts,
    // turning every batch of log lines into a full re-layout of the viewport — which is
    // precisely the cost the append fast path exists to avoid. Erase() is the narrowest
    // invalidation that is still correct, and the append path is its only caller.
    //
    // Not to be confused with eviction: this is a correctness invalidation (the cached
    // pixels are wrong), whereas Evict() is a capacity decision about entries that are
    // still valid.
    bool Erase(size_t lineNumber);

    // Set the box every layout is built into, and flush if it changed.
    //
    // NoWrap leaves this at the unbounded default: the width is a bound, not a wrap
    // constraint, so one value serves every line. WRAP IS WHY THIS IS SETTABLE — there
    // the width IS the wrap constraint, so a layout built at 400 DIP breaks its lines
    // differently from one built at 300, and reusing it after a resize would draw the
    // old line breaks. Flushing on a width change is therefore not an optimization
    // choice but a correctness requirement, and it is the reason this lives here rather
    // than being passed per-Get: a per-call width would let two entries in the same
    // cache disagree about the wrap width, and nothing would detect it.
    //
    // The height bounds ONE paragraph rather than a document, which is the whole point
    // of the per-paragraph split: the old whole-document layout was created at
    // maxHeight = 100000 DIP and clipped everything past it, so a large wrapped document
    // simply stopped drawing around 5000 lines. Per paragraph, 100000 DIP is ~5000
    // visual lines of one paragraph — reachable only by a document with no newlines at
    // all, which is NoWrap's case, not this one.
    void SetLayoutBox(float widthDip, float heightDip);
    float LayoutWidth() const { return boxWidth_; }
    float LayoutHeight() const { return boxHeight_; }

    // Drop every entry if the DPI or theme generation differs from the last call,
    // then record the new values. Call it once before a draw pass rather than
    // relying on Get to notice: doing it up front means a whole frame is drawn at
    // one DPI, instead of the first few lines using cached old-DPI layouts and the
    // rest being rebuilt at the new one.
    void InvalidateIfStale(float dpiScale, unsigned generation);

    size_t Size() const { return map_.size(); }
    size_t Capacity() const { return capacity_; }

private:
    void Evict();  // drop the least-recently-used entry

    struct Entry {
        ComPtr<IDWriteTextLayout> layout;
        std::list<size_t>::iterator lruIt;
        size_t coverEnd = 0;  // characters this layout covers: [0, coverEnd)
    };

    size_t capacity_;
    // Line numbers, most-recently-used at the front.
    std::list<size_t> lru_;
    std::unordered_map<size_t, Entry> map_;

    float lastDpi_ = -1.0f;
    unsigned lastGeneration_ = 0;
    // The box layouts are built into. Defaults to unbounded, which is NoWrap's case.
    float boxWidth_ = kUnboundedWidthDip;
    float boxHeight_ = kUnboundedHeightDip;
};

} // namespace fluent
