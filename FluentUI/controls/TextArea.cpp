// TextArea.cpp — multi-line specifics on top of TextEditBase.

#include "TextArea.h"
#include "../styling/ThemeTokens.h"
#include "../input/InputManager.h"
#include "../window/WindowServices.h"
#include "../composition/ICompositionBackend.h"
#include "../animation/Animation.h"     // CaretBlinkHalfPeriodSec
#include "../text/VisibleLines.h"
#include "../core/ScrollMath.h"         // EnsureVisibleOffset (shared with TreeView)
#include "../diagnostics/LayoutCostProbe.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace fluent {

namespace {
constexpr float kPadX = 10.0f;         // left text padding (DIP)
constexpr float kPadY = 8.0f;          // top/bottom text padding (DIP)
constexpr float kCaretW = 1.0f;
constexpr float kScrollReserve = 12.0f; // right gutter so text clears the thumb
} // namespace

// ---------------------------------------------------------------------------
// NoWrap helpers (line geometry + cache)
// ---------------------------------------------------------------------------

std::wstring_view TextArea::LineTextView(const std::wstring& display, size_t lineNumber) const {
    EnsureLineIndex();
    auto [start, end] = lines_.LineRange(lineNumber);
    // The index is built over text_ while `display` may additionally hold an IME
    // composition inserted at the caret, which shifts every offset after it. Clamping
    // keeps the view inside the buffer; the composition's own characters are drawn by
    // the caret-line path, which recomputes the range against the display string.
    const size_t len = display.size();
    if (start > len) return {};
    if (end > len) end = len;
    return std::wstring_view(display.data() + start, end - start);
}

// A line's text as a slice of text_ (no composition folded in). The index is built
// over text_, so this is a pure slice with no copying at any document size.
std::wstring_view TextArea::LineSliceOfText(size_t lineNumber) const {
    auto [start, end] = lines_.LineRange(lineNumber);
    if (start > text_.size()) return {};
    if (end > text_.size()) end = text_.size();
    return std::wstring_view(text_.data() + start, end - start);
}

// Fold a laid-out line's width into the horizontal high-water mark.
//
// GetMetrics is called per line per frame here, which sounds expensive and is not:
// DWrite computes a layout's metrics once and caches them on the layout object, so
// every call after the first is a struct copy. Doing it at the point where a layout is
// OBTAINED (rather than in a separate measuring pass) is what keeps the horizontal
// extent's cost identical to the drawing cost — O(visible lines), never O(document).
//
// `width` and not `widthIncludingTrailingWhitespace`: trailing spaces are invisible, so
// letting them extend the scroll range would let the user scroll into blank space by
// however many spaces happen to sit at the end of the longest line. The caret can still
// be placed past the last glyph — EnsureCaretVisible scrolls to the caret directly and
// does not go through this mark.
void TextArea::NoteLineWidth(IDWriteTextLayout* layout) const {
    if (!layout) return;
    DWRITE_TEXT_METRICS tm{};
    if (FAILED(layout->GetMetrics(&tm))) return;
    if (tm.width > maxSeenLineWidth_) maxSeenLineWidth_ = tm.width;
}

IDWriteTextLayout* TextArea::LineLayout(size_t lineNumber, size_t minCover) const {
    if (!Dwrite()) return nullptr;
    IDWriteTextFormat* fmt = Dwrite()->Format(
        EffectiveFontSize(), EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL), DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return nullptr;
    const unsigned gen = Context().theme ? Context().theme->generation : 0u;
    EnsureLineIndex();


    // While an IME composition is active, the caret's line contains characters the
    // index has never seen, and its content changes on every keystroke. Build that ONE
    // line uncached; every other line stays on the cached slice path.
    //
    // WHY THIS MATTERS ENOUGH TO SPECIAL-CASE: the composition used to be folded in by
    // asking DisplayText for the whole document, which copies the entire buffer when a
    // composition exists. That happened once per visible line per frame — on a 28 MB
    // document, hundreds of MB of memcpy per frame plus a full index rebuild per key.
    // Typing Chinese into a large document stalled for seconds; typing English did not,
    // because with no composition DisplayText copies nothing. The asymmetry was the tell.
    if (!composition_.empty() && lineNumber == lines_.LineFromOffset(caret_)) {
        auto [lineStart, lineEnd] = lines_.LineRange(lineNumber);
        if (lineStart > text_.size()) return nullptr;
        if (lineEnd > text_.size()) lineEnd = text_.size();
        compositionLine_.assign(text_, lineStart, lineEnd - lineStart);
        const size_t insertAt = (caret_ >= lineStart && caret_ <= lineEnd)
                              ? caret_ - lineStart : compositionLine_.size();
        compositionLine_.insert(insertAt, composition_);
        compositionLayout_.Reset();
        if (FAILED(Dwrite()->Factory()->CreateTextLayout(
                compositionLine_.c_str(), static_cast<UINT32>(compositionLine_.size()),
                fmt, LineLayoutCache::kUnboundedWidthDip,
                LineLayoutCache::kUnboundedHeightDip,
                compositionLayout_.GetAddressOf())))
            return nullptr;
        NoteLineWidth(compositionLayout_.Get());
        return compositionLayout_.Get();
    }

    IDWriteTextLayout* layout =
        lineLayouts_.Get(lineNumber, LineSliceOfText(lineNumber),
                         Dwrite()->Factory(), fmt, Context().dpiScale, gen,
                         minCover);
    NoteLineWidth(layout);
    return layout;
}

void TextArea::NoWrapCaretMetrics(UINT32 index, float& x, float& y, float& height) const {
    x = 0.0f; y = 0.0f; height = LineHeight();
    EnsureLineIndex();

    // `index` may point PAST the composition string (callers pass
    // caret_ + composition_.size() so the caret sits after the composed text), while
    // the index is built over text_ and knows nothing about those characters. Locate
    // the line using the text_-relative caret, then measure inside the caret line's
    // layout — which LineLayout built WITH the composition spliced in, so its local
    // offsets do span the composed characters.
    const size_t composeLen = composition_.size();
    const UINT32 textIndex = (composeLen > 0 && index >= composeLen)
                           ? static_cast<UINT32>(index - composeLen)
                           : index;
    const size_t lineNum = lines_.LineFromOffset(textIndex);
    y = LineTopDip(lineNum);
    IDWriteTextLayout* layout = LineLayout(lineNum);
    if (!layout) return;

    auto [lineStart, lineEnd] = lines_.LineRange(lineNum);
    // Local offset within the line's LAYOUT. On the caret line while composing, the
    // layout is longer than the text_ slice by exactly composeLen, and the caret
    // belongs after the composed characters — so both the offset and its clamp have
    // to account for them, or the caret snaps back to where composing began.
    const bool composingHere = composeLen > 0 && lineNum == lines_.LineFromOffset(caret_);
    const UINT32 lineLocal = static_cast<UINT32>(
        textIndex > lineStart ? textIndex - lineStart : 0u)
        + (composingHere ? static_cast<UINT32>(composeLen) : 0u);
    const UINT32 maxLocal = static_cast<UINT32>(lineEnd - lineStart)
        + (composingHere ? static_cast<UINT32>(composeLen) : 0u);
    DWRITE_HIT_TEST_METRICS hm{};
    float lx = 0, ly = 0;
    layout->HitTestTextPosition(std::min(lineLocal, maxLocal), FALSE, &lx, &ly, &hm);
    x = lx;
    if (hm.height > 0) height = hm.height;
}

UINT32 TextArea::NoWrapHitIndex(float localX, float localY) const {
    EnsureLineIndex();
    if (lines_.LineCount() == 0) return 0;
    const float lh = LineHeight();
    if (lh <= 0.0f) return 0;
    // Clamp the Y probe to the valid content range so a click below the last
    // line lands on the last line (not past the end of the index).
    const float clampedY = std::max(0.0f, std::min(localY,
        LineTopDip(lines_.LineCount() - 1) + lh * 0.5f));
    const size_t lineNum = static_cast<size_t>(std::max(0.0f, clampedY / lh));
    const size_t safeLineNum = std::min(lineNum, lines_.LineCount() - 1);
    IDWriteTextLayout* layout = LineLayout(safeLineNum);
    if (!layout) return 0;
    auto [lineStart, lineEnd] = lines_.LineRange(safeLineNum);
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestPoint(localX, 0.0f, &trailing, &inside, &hm);
    const UINT32 lineLocal = hm.textPosition + (trailing ? hm.length : 0);
    return static_cast<UINT32>(lineStart) + lineLocal;
}

// ---------------------------------------------------------------------------
// Wrap mode
// ---------------------------------------------------------------------------

void TextArea::SetWrapMode(TextWrapMode mode) {
    if (!SetProperty(wrapMode_, mode, DirtyFlags::Measure)) return;
    // Everything cached under the old mode is now wrong. The layout was built with
    // the other mode's word-wrapping flag, and the index was either unbuilt (coming
    // from Wrap) or built over text that the new mode will break differently.
    //
    // OnTextLayoutDirty is the single place that invalidates both, so route through
    // it rather than duplicating the two assignments: SetProperty only records dirty
    // flags for the frame pipeline, it does not know this control caches anything.
    OnTextLayoutDirty();

    // Restore the layout box to the correct default for the new mode. Wrap sets it to
    // the wrap width in EnsureWrapExtent; NoWrap wants unbounded so the width is a
    // bound, not a wrap constraint. Without this a TextArea created in Wrap mode (the
    // default) then switched to NoWrap before Arrange would build every line at whatever
    // narrow width Wrap saw first (often ContentWidth()'s 1 DIP clamp floor), clipping
    // all but the first character of each line.
    if (mode == TextWrapMode::NoWrap) {
        lineLayouts_.SetLayoutBox(LineLayoutCache::kUnboundedWidthDip,
                                   LineLayoutCache::kUnboundedHeightDip);
    }
}

void TextArea::EnsureLineIndex() const {
    // Built in BOTH modes. NoWrap asks "which logical line is at Y" and gets an exact
    // answer; Wrap asks "which PARAGRAPH is at Y", which the index answers together with
    // WrapExtentMap's per-paragraph visual line counts. Before wrapped virtualization
    // this returned early under Wrap because nothing there used paragraph boundaries —
    // now the extent map is built from them.
    if (!lineIndexDirty_) return;

    // Built over text_, NOT DisplayText(). The IME composition string is a transient
    // overlay at the caret: it does not change the document's line structure, and
    // rebuilding the index for it would make every keystroke of a Chinese/Japanese
    // composition an O(document) scan. On a 28 MB buffer that is seconds per key,
    // which is exactly the stall this fixes. Composition is folded in per line at
    // draw time instead (see LineTextView), where it costs one line's worth of work.
    lines_.Rebuild(text_);
    ++lineIndexRebuilds_;
    lineIndexDirty_ = false;
}

// ---------------------------------------------------------------------------
// Wrapped virtualization (§3)
// ---------------------------------------------------------------------------

// The seed for "how many characters fit on one visual line" before anything has been
// measured. Crude on purpose: only the order of magnitude matters, because the first
// drawn frame measures every visible paragraph and feeds the rolling average, and the
// estimates that remain describe text nobody is looking at.
//
// fontSize * 0.5 as an average advance width is the same rough figure §1.5b-3 uses for
// its prefix-clip estimate — being consistent about the guess means the two are wrong in
// the same direction rather than fighting each other.
float TextArea::SeedCharsPerLine() const {
    const float avgGlyphW = std::max(1.0f, EffectiveFontSize() * 0.5f);
    return std::max(1.0f, WrapLayoutWidth() / avgGlyphW);
}

void TextArea::EnsureWrapExtent() const {
    if (wrapMode_ != TextWrapMode::Wrap) return;
    LayoutCostProbe::Scope probe(LayoutCostKey::TextAreaWrapExtent);
    EnsureLineIndex();

    const float width = WrapLayoutWidth();
    // A width change invalidates every MEASUREMENT, not just the estimates: a paragraph
    // wrapped at 400 DIP breaks differently at 300. So a resize is a full Reset, and that
    // is the honest move — there is nothing in the old map worth keeping.
    //
    // This is also the reason a resize is now cheap: Reset is O(paragraphs) of integer
    // arithmetic with no DWrite at all, where the old whole-document layout re-wrapped
    // the entire buffer on every resize frame.
    const bool widthChanged = width != wrapExtentWidth_;
    if (!wrapExtentDirty_ && !widthChanged) return;

    // Scale the seed by the width ratio rather than recomputing from scratch, so a resize
    // starts from what the document turned out to be at the old width instead of throwing
    // that away. WrapExtentMap keeps its density samples across a Reset for the same
    // reason (see its Reset).
    float seed = SeedCharsPerLine();
    if (wrapExtentWidth_ > 0.0f && width > 0.0f && wrapExtent_.MeasuredCount() > 0) {
        const float scaled = wrapExtent_.MeasuredCharsPerLine() * (width / wrapExtentWidth_);
        if (scaled > 0.0f) seed = scaled;
    }

    wrapExtent_.Reset(lines_, seed);
    wrapExtentWidth_ = width;
    wrapExtentDirty_ = false;
    // The layout cache holds paragraphs wrapped at the OLD width; SetLayoutBox flushes
    // when the box actually changes, so this is a no-op when only the text changed.
    lineLayouts_.SetLayoutBox(width, LineLayoutCache::kUnboundedHeightDip);
}

IDWriteTextLayout* TextArea::WrapParagraphLayout(size_t p) const {
    if (!Dwrite() || !LayoutWidthKnown()) return nullptr;
    IDWriteTextFormat* fmt = Dwrite()->Format(
        EffectiveFontSize(), EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL), DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP);
    if (!fmt) return nullptr;
    EnsureWrapExtent();
    if (p >= lines_.LineCount()) return nullptr;

    // While an IME composition is active, the caret's paragraph needs the composition
    // folded in, same as LineLayout does for NoWrap. Build that ONE paragraph uncached.
    if (!composition_.empty() && p == lines_.LineFromOffset(caret_)) {
        auto [paraStart, paraEnd] = lines_.LineRange(p);
        if (paraStart > text_.size()) return nullptr;
        if (paraEnd > text_.size()) paraEnd = text_.size();
        compositionLine_.assign(text_, paraStart, paraEnd - paraStart);
        const size_t insertAt = (caret_ >= paraStart && caret_ <= paraEnd)
                              ? caret_ - paraStart : compositionLine_.size();
        compositionLine_.insert(insertAt, composition_);

        const float wrapWidth = lineLayouts_.LayoutWidth();
        // Re-enter guard: if the composed text + width + paragraph match what
        // compositionLayout_ already holds, return it — rebuilding would free the pointer
        // a caller is still holding (the reported crash when typing Chinese in Wrap mode).
        const bool alreadyBuilt = (compositionLayoutPara_ == p) &&
                                  (std::fabs(compositionLayoutWidth_ - wrapWidth) < 0.1f) &&
                                  (compositionLayoutText_ == compositionLine_);
        if (alreadyBuilt && compositionLayout_) {
            return compositionLayout_.Get();
        }

        compositionLayout_.Reset();
        if (FAILED(Dwrite()->Factory()->CreateTextLayout(
                compositionLine_.c_str(), static_cast<UINT32>(compositionLine_.size()),
                fmt, wrapWidth, LineLayoutCache::kUnboundedHeightDip,
                compositionLayout_.GetAddressOf())))
            return nullptr;

        // Cache the inputs so a re-entrant call (MeasureWrapParagraph from WrapCaretMetrics)
        // returns the SAME pointer instead of freeing it.
        compositionLayoutPara_ = p;
        compositionLayoutWidth_ = wrapWidth;
        compositionLayoutText_ = compositionLine_;
        return compositionLayout_.Get();
    }

    const unsigned gen = Context().theme ? Context().theme->generation : 0u;
    // The cache is keyed by paragraph number and its layout box is the wrap width, both
    // set in EnsureWrapExtent. minCover is deliberately kFullLine: the §1.5b-3 prefix
    // clip is a NOWRAP optimization (there, characters past the visible columns are off
    // to the right and genuinely unneeded). Under Wrap those same characters flow onto
    // the NEXT visual line and are on screen, so clipping the layout would delete visible
    // text.
    IDWriteTextLayout* layout =
        lineLayouts_.Get(p, LineSliceOfText(p), Dwrite()->Factory(), fmt,
                         Context().dpiScale, gen);
    return layout;
}

uint32_t TextArea::MeasureWrapParagraph(size_t p) const {
    IDWriteTextLayout* layout = WrapParagraphLayout(p);
    if (!layout) return 0;
    const float lh = LineHeight();
    if (!(lh > 0.0f)) return 0;

    // GetLineMetrics would give the exact visual line count, but it needs a second call
    // to size the array and allocates per paragraph. The height divided by the line
    // height is the same number for uniform text, and this runs per visible paragraph per
    // frame — so measure the height and round.
    DWRITE_TEXT_METRICS tm{};
    if (FAILED(layout->GetMetrics(&tm))) return 0;
    uint32_t visualLines = tm.lineCount > 0 ? tm.lineCount
                                            : static_cast<uint32_t>(tm.height / lh + 0.5f);
    if (visualLines < 1u) visualLines = 1u;

    auto [start, end] = lines_.LineRange(p);
    wrapExtent_.SetMeasured(p, visualLines);
    wrapExtent_.NoteMeasuredCharsPerLine(end - start, visualLines);
    return visualLines;
}

LineSpan TextArea::WrapParagraphSpanForBand(float bandTopDip, float bandHeightDip) const {
    if (wrapMode_ != TextWrapMode::Wrap) return {};
    EnsureWrapExtent();
    return wrapExtent_.ParagraphSpanForBand(bandTopDip, bandHeightDip, LineHeight());
}

size_t TextArea::MeasureParagraphsInBand(float bandTopDip, float bandHeightDip) const {
    if (wrapMode_ != TextWrapMode::Wrap) return 0;
    EnsureWrapExtent();
    const float lh = LineHeight();
    if (!(lh > 0.0f)) return 0;

    const LineSpan span = wrapExtent_.ParagraphSpanForBand(bandTopDip, bandHeightDip, lh);
    if (span.Empty()) return 0;

    const float bandBottom = bandTopDip + bandHeightDip;
    size_t visited = 0;
    for (size_t p = span.first; p < wrapExtent_.ParagraphCount(); ++p) {
        // Re-read the top rather than accumulating: a paragraph measured earlier in THIS
        // walk may have turned out taller than its estimate, which moves this one down.
        // Trusting the initial span instead leaves a wedge of blank surface below a
        // paragraph that grew, until some later frame happens to redraw it.
        if (wrapExtent_.ParagraphTopDip(p, lh) >= bandBottom) break;
        if (MeasureWrapParagraph(p) == 0) break;   // no DWrite / no width: stop cleanly
        ++visited;
    }
    return visited;
}

float TextArea::WrapParagraphTopDip(size_t paragraph) const {
    if (wrapMode_ != TextWrapMode::Wrap) return 0.0f;
    EnsureWrapExtent();
    return wrapExtent_.ParagraphTopDip(paragraph, LineHeight());
}

float TextArea::MeasuredContentHeightDip() const {
    if (!LayoutWidthKnown()) return 0.0f;
    if (wrapMode_ == TextWrapMode::NoWrap) {
        EnsureLineIndex();
        return lines_.LineCount() * LineHeight();
    }
    EnsureWrapExtent();
    return wrapExtent_.TotalHeightDip(LineHeight());
}

// Caret geometry under Wrap: locate the paragraph through the index, then resolve the
// position inside that ONE paragraph's layout. Never needs a whole-document layout, which
// is what makes the caret affordable in a large wrapped document.
//
// The paragraph is measured on the way through. That matters for correctness, not just
// for the extent: the caret's paragraph may be one nobody has drawn (Ctrl+End into
// unmeasured territory), and its Y depends on the heights of every paragraph above it.
// Those stay estimates — which is exactly why the scrollbar shifts as the view arrives
// there, and why EnsureCaretVisible re-runs after the band is drawn.
void TextArea::WrapCaretMetrics(UINT32 index, float& x, float& y, float& height) const {
    x = 0.0f; y = 0.0f; height = LineHeight();
    EnsureWrapExtent();

    // Composition characters live at the caret and the index knows nothing about them.
    // When composition exists, WrapParagraphLayout includes it in the layout, so we need
    // to determine whether `index` points into text_ or into the appended composition.
    const size_t composeLen = composition_.size();
    const bool haveCompose = (composeLen > 0);
    const UINT32 caretPos = caret_;

    // If index is past the composition insertion point, map back to text_-space.
    // Otherwise it's already in text_-space (before the caret).
    const UINT32 textIndex = (haveCompose && index >= caretPos + composeLen)
                           ? static_cast<UINT32>(index - composeLen)
                           : (index > caretPos ? caretPos : index);

    const size_t p = lines_.LineFromOffset(textIndex);
    IDWriteTextLayout* layout = WrapParagraphLayout(p);
    if (!layout) { y = WrapParagraphTopDip(p); return; }
    MeasureWrapParagraph(p);
    y = wrapExtent_.ParagraphTopDip(p, LineHeight());

    auto [start, end] = lines_.LineRange(p);

    // Layout coordinates: if this paragraph contains the caret and composition exists,
    // the layout includes composition, so positions within or after composition must use
    // layout-space offsets.
    const bool isCaretPara = (p == lines_.LineFromOffset(caretPos));
    UINT32 layoutPos = 0;
    if (isCaretPara && haveCompose) {
        // Layout = text_[start..end) + composition at (caret - start).
        // If index is at or past caret, it maps to layout position (index - start).
        // This works because composition is inserted at (caret - start) in the layout.
        if (index >= caretPos) {
            layoutPos = static_cast<UINT32>(index - start);
        } else {
            layoutPos = static_cast<UINT32>(textIndex > start ? textIndex - start : 0);
        }
    } else {
        // No composition or not the caret paragraph: layout = text_[start..end).
        layoutPos = static_cast<UINT32>(textIndex > start ? textIndex - start : 0);
    }

    DWRITE_HIT_TEST_METRICS hm{};
    float lx = 0, ly = 0;
    if (SUCCEEDED(layout->HitTestTextPosition(layoutPos, FALSE, &lx, &ly, &hm))) {
        x = lx;
        y += ly;
        if (hm.height > 0) height = hm.height;
    }
}

UINT32 TextArea::WrapHitIndex(float localX, float localY) const {
    EnsureWrapExtent();
    if (lines_.LineCount() == 0) return 0;
    const size_t p = wrapExtent_.ParagraphFromYDip(localY, LineHeight());
    IDWriteTextLayout* layout = WrapParagraphLayout(p);
    auto [start, end] = lines_.LineRange(p);
    if (!layout) return static_cast<UINT32>(start);
    MeasureWrapParagraph(p);

    // Y within the paragraph, so a click on its second wrapped line hits that line.
    const float top = wrapExtent_.ParagraphTopDip(p, LineHeight());
    const float inParagraphY = std::max(0.0f, localY - top);
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestPoint(localX, inParagraphY, &trailing, &inside, &hm);
    UINT32 layoutPos = hm.textPosition + (trailing ? hm.length : 0);

    // If this paragraph contains the caret and composition exists, the layout includes
    // composition. Map layout-space position back to text_-space index.
    const size_t composeLen = composition_.size();
    const bool isCaretPara = (composeLen > 0 && p == lines_.LineFromOffset(caret_));
    if (isCaretPara) {
        const UINT32 caretLocal = static_cast<UINT32>(caret_ - start);
        // Layout = text_[start..caret) + composition + text_[caret..end).
        // If layoutPos is within composition (at or after caret, before caret+composeLen),
        // map to the caret position in text_.
        if (layoutPos >= caretLocal && layoutPos < caretLocal + composeLen) {
            return caret_;  // click within composition maps to caret
        } else if (layoutPos >= caretLocal + composeLen) {
            // Past composition: subtract composition length to get text_ index.
            layoutPos -= static_cast<UINT32>(composeLen);
        }
    }

    return static_cast<UINT32>(start) +
           std::min<UINT32>(layoutPos, static_cast<UINT32>(end - start));
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

float TextArea::ContentLeft() const { return kPadX; }
float TextArea::ContentTop() const { return kPadY; }
float TextArea::ContentWidth() const {
    return std::max(1.0f, bounds_.w - kPadX - kScrollReserve);
}
float TextArea::ContentHeight() const {
    return std::max(1.0f, bounds_.h - kPadY * 2.0f);
}

std::wstring TextArea::SanitizeInput(std::wstring s) const {
    // Normalize CRLF / lone CR to '\n' so the buffer has a single newline form.
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            out.push_back(L'\n');
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;  // skip the LF of CRLF
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool TextArea::LayoutWidthKnown() const {
    // Has a layout pass given this control a real width yet?
    //
    // Before the first Arrange, bounds_ is all zeros and ContentWidth() clamps to its
    // 1 DIP floor. Wrapping at 1 DIP is not merely useless, it is the most expensive
    // wrap that exists: every character becomes its own line, so DWrite allocates
    // per-line metrics for the entire document. Seeding a large document with SetText
    // before layout therefore paid for a full pass at the worst possible width, and
    // then paid again at the real width when OnBoundsChanged arrived.
    //
    // The threshold is the padding + gutter, i.e. the point at which ContentWidth()
    // stops clamping and starts reporting a width that came from the layout system.
    return bounds_.w > kPadX + kScrollReserve;
}

// NOTE: EnsureLayout() is GONE, and its absence is the point of §3.
//
// It used to build one IDWriteTextLayout over the whole buffer, which is what gave Wrap
// its two hard ceilings: content past maxHeight (100000 DIP, ~5000 wrapped lines) was
// clipped away and simply invisible, and every resize frame re-wrapped the entire
// document. Both modes now build layouts per line (NoWrap) or per paragraph (Wrap)
// through lineLayouts_, so neither ceiling exists and nothing here is O(document) except
// the newline scan in EnsureLineIndex.
//
// If a future change seems to need a document-wide layout, it is reintroducing both
// ceilings — see controls/TextArea.h and text/WrapExtentMap.h for what that costs.

float TextArea::LineHeight() const {
    // Memoized: this builds a throwaway probe layout, and the compositor tick path
    // asks for it every frame. Only the font size can change it (DWrite line spacing
    // is in DIPs, so DPI does not enter).
    if (lineHeightCache_ > 0.0f && lineHeightForFont_ == EffectiveFontSize())
        return lineHeightCache_;

    float result = EffectiveFontSize() * 1.4f;
    if (Dwrite()) {
        // Line spacing of an empty layout at this font size.
        IDWriteTextFormat* fmt = Dwrite()->Format(
            EffectiveFontSize(), EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL), DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP);
        if (fmt) {
            ComPtr<IDWriteTextLayout> probe;
            if (SUCCEEDED(Dwrite()->Factory()->CreateTextLayout(
                    L"Ag", 2, fmt, 1e5f, 1e5f, probe.GetAddressOf()))) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(probe->GetMetrics(&tm)) && tm.height > 0)
                    result = tm.height;
            }
        }
    }
    // Only cache once DWrite is available; before attach the fallback is a guess.
    if (Dwrite()) {
        lineHeightCache_ = result;
        lineHeightForFont_ = EffectiveFontSize();
    }
    return result;
}

// Both modes resolve the caret through ONE line's or ONE paragraph's layout — neither
// needs a document-wide one. The split is only about what a "row" is.
void TextArea::CaretMetrics(UINT32 index, float& x, float& y, float& height) const {
    if (wrapMode_ == TextWrapMode::NoWrap) {
        NoWrapCaretMetrics(index, x, y, height);
        return;
    }
    WrapCaretMetrics(index, x, y, height);
}

UINT32 TextArea::HitIndex(float dipX, float dipY) const {
    // Subtract the horizontal offset so the click coordinates are in content space.
    // CurrentOffsetX() returns scroll_.OffsetX() directly (no tween), so a click lands
    // on the column the user sees regardless of whether composition is active.
    const float localX = dipX - (bounds_.x + ContentLeft()) + CurrentOffsetX();
    const float localY = dipY - (bounds_.y + ContentTop()) + CurrentOffset();
    if (wrapMode_ == TextWrapMode::NoWrap) return NoWrapHitIndex(localX, localY);
    return WrapHitIndex(localX, localY);
}

// ---------------------------------------------------------------------------
// Vertical navigation (all work in text-layout-local coordinates: X relative
// to the text origin, Y relative to the top of the whole layout, pre-scroll).
// ---------------------------------------------------------------------------

int TextArea::VisibleLineCount() const {
    float lh = LineHeight();
    if (lh <= 0.0f) return 1;
    return std::max(1, static_cast<int>(ContentHeight() / lh));
}

UINT32 TextArea::IndexByLineStep(int deltaLines) const {
    if (wrapMode_ == TextWrapMode::NoWrap) {
        // A logical line IS a visual line here, so the target line is arithmetic and
        // the only layout work is resolving the X within that one line. Under Wrap the
        // same question needs a hit-test against the whole document.
        EnsureLineIndex();
        const size_t from = lines_.LineFromOffset(caret_);
        // Signed arithmetic first, then clamp: `from + deltaLines` on size_t would wrap
        // to a huge value when stepping up from line 0, which clamps to the LAST line —
        // i.e. Up at the top of the document would jump to the bottom.
        const long long target = static_cast<long long>(from) + deltaLines;
        const long long last = static_cast<long long>(lines_.LineCount()) - 1;
        const size_t targetLine = static_cast<size_t>(std::clamp(target, 0LL, last));
        // Keep the caret's X: probe that X inside the target line's own layout.
        float x = 0, y = 0, h = 0;
        NoWrapCaretMetrics(caret_, x, y, h);
        auto [start, end] = lines_.LineRange(targetLine);
        IDWriteTextLayout* lineLayout = LineLayout(targetLine);
        if (!lineLayout) return static_cast<UINT32>(start);
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS hm{};
        lineLayout->HitTestPoint(x, 0.0f, &trailing, &inside, &hm);
        const UINT32 local = hm.textPosition + (trailing ? hm.length : 0);
        return static_cast<UINT32>(start) +
               std::min<UINT32>(local, static_cast<UINT32>(end - start));
    }
    // Wrap: a VISUAL line step, which may stay inside the current paragraph (a paragraph
    // that wrapped onto several lines) or cross into the next one. Both cases fall out of
    // going through content space — find where the caret is, move one line height, and ask
    // what is there. WrapHitIndex resolves that through whichever paragraph owns the Y, so
    // this needs no whole-document layout and no special case for the paragraph boundary.
    float x = 0, y = 0, h = 0;
    WrapCaretMetrics(caret_, x, y, h);
    if (!(h > 0.0f)) h = LineHeight();
    const float targetY = y + deltaLines * h + h * 0.5f;
    if (targetY < 0.0f) return 0;
    return WrapHitIndex(x, targetY);
}

UINT32 TextArea::IndexByPageStep(int deltaPages) const {
    return IndexByLineStep(deltaPages * std::max(1, VisibleLineCount() - 1));
}

UINT32 TextArea::LineEdgeIndex(UINT32 from, bool toEnd) const {
    if (wrapMode_ == TextWrapMode::NoWrap) {
        // Home/End are exactly the line's range under NoWrap — no layout involved.
        EnsureLineIndex();
        auto [start, end] = lines_.LineRange(lines_.LineFromOffset(from));
        return static_cast<UINT32>(toEnd ? end : start);
    }
    // Wrap: Home/End address the ends of the VISUAL line, not the paragraph — pressing
    // End on the second wrapped line of a paragraph must stop at that line's break, which
    // is what every editor does. So probe the far left / far right at the caret's own Y
    // and let the paragraph's layout resolve which visual line that is.
    float x = 0, y = 0, h = 0;
    WrapCaretMetrics(from, x, y, h);
    if (!(h > 0.0f)) h = LineHeight();
    return WrapHitIndex(toEnd ? 1.0e6f : 0.0f, y + h * 0.5f);
}

// ---------------------------------------------------------------------------
// Scroll
// ---------------------------------------------------------------------------

float TextArea::ScrollExtentDip() const {
    // Nothing is scrollable until a width is known: with no layout there is no measured
    // text height, and reporting the bare padding as an extent would make a zero-sized
    // control look scrollable by 2 * kPadY (its region height clamps to 0, so the
    // padding alone becomes a positive MaxOffset).
    if (!LayoutWidthKnown()) return 0.0f;
    if (wrapMode_ == TextWrapMode::NoWrap) {
        // Arithmetic: line count times line height. Nothing is laid out, which is what
        // makes opening a large document cheap, and it is EXACT — so the scrollbar never
        // shifts as lines are laid out lazily.
        EnsureLineIndex();
        return lines_.LineCount() * LineHeight() + kPadY * 2.0f;
    }
    // Wrap: the estimated-and-corrected extent. Also arithmetic (visual lines times line
    // height), but the visual line counts are exact only for paragraphs that have been
    // drawn — the rest are estimates, so THIS NUMBER MOVES as scrolling reaches new
    // regions. That is the accepted trade of wrapped virtualization; see
    // text/WrapExtentMap.h for why the alternatives are worse.
    return MeasuredContentHeightDip() + kPadY * 2.0f;
}

float TextArea::ScrollExtentXDip() const {
    // Wrap has no horizontal extent by construction: the layout is built AT the content
    // width, so nothing can stick out sideways. Reporting zero keeps the horizontal rail
    // and every horizontal input path inert in that mode.
    if (wrapMode_ != TextWrapMode::NoWrap) return 0.0f;
    if (!LayoutWidthKnown()) return 0.0f;

    // Warm the mark with the CARET's line before answering. Without this, the sequence
    // "edit -> RefreshComposition -> clamp offsetX" runs before any line has been drawn
    // at the new text, so the mark would be 0, MaxOffsetX() would be 0, and the offset
    // would be clamped to 0 — snapping the view back to column 0 on every keystroke
    // typed at column 300. The caret's line is the one line guaranteed to matter, and
    // measuring exactly one line is O(1).
    if (Dwrite()) {
        EnsureLineIndex();
        NoteLineWidth(LineLayout(lines_.LineFromOffset(caret_)));
    }
    if (maxSeenLineWidth_ <= 0.0f) return 0.0f;
    // Mirror the vertical extent's shape: content + the gutters that frame it, so
    // MaxOffsetX() (extent - bounds_.w) leaves the last glyph clear of the scrollbar
    // reserve instead of hard against it.
    return maxSeenLineWidth_ + kPadX + kScrollReserve;
}

void TextArea::SyncScroll() {
    // No layout build here in either mode any more. Wrap used to call EnsureLayout to get
    // a measured document height; ScrollExtentDip now derives it from the extent map,
    // which is arithmetic over paragraph counts.
    scroll_.SetBounds(bounds_);
    scroll_.SetContentHeight(ScrollExtentDip());
    scroll_.SetContentWidth(ScrollExtentXDip());
}

float TextArea::MaxOffsetDip() const {
    // Mirrors what the active scroll path uses as its own bound, so a test asking
    // "is the view at the tail" gets the same number the control clamps against.
    if (CompositionActive()) return content_->MaxOffset();
    return scroll_.MaxOffset();
}

// ---------------------------------------------------------------------------
// High-throughput log append (§2)
// ---------------------------------------------------------------------------

void TextArea::AppendText(std::wstring_view s) {
    if (s.empty()) return;

    // Newlines must be normalized to '\n' — LineIndex scans for '\n' and nothing else,
    // and every other input path guarantees it via SanitizeInput.
    //
    // SanitizeInput takes and returns a wstring, so routing through it unconditionally
    // would cost two copies of the added text on every batch. Most log sources emit '\n'
    // only, so check first and take the copy only when there is actually a '\r' to fix.
    // `added` then aliases either the caller's view (no copy) or the scratch buffer.
    std::wstring normalized;
    std::wstring_view added = s;
    if (s.find(L'\r') != std::wstring_view::npos) {
        normalized = SanitizeInput(std::wstring(s));
        added = normalized;
    }
    if (added.empty()) return;

    const size_t oldLen = text_.size();

    if (wrapMode_ == TextWrapMode::NoWrap) {
        // The line whose CONTENT is about to change. Appending "c" to a buffer ending in
        // "b" makes the last line "bc" — same line number, different characters — so its
        // cached layout is the only stale one in the cache. Captured BEFORE the append,
        // while the index still describes the old buffer.
        //
        // Only meaningful if the index is currently clean; if it is dirty the cache was
        // already flushed by whatever dirtied it, and there is nothing to invalidate.
        const bool indexWasClean = !lineIndexDirty_;
        size_t oldLastLine = 0;
        if (indexWasClean) oldLastLine = lines_.LineCount() - 1;

        text_.append(added);

        if (indexWasClean) {
            // Append-only index update: O(added), and every existing line keeps its
            // number. If the index has diverged from the buffer (Append returns false)
            // fall back to a rebuild rather than emitting offsets into the wrong
            // characters — a correctness backstop, not an expected path.
            if (lines_.Append(added, oldLen)) {
                lineLayouts_.Erase(oldLastLine);
            } else {
                lineIndexDirty_ = true;
                lineLayouts_.Clear();
                ++lineCacheClears_;
            }
        }
        // maxSeenLineWidth_ is deliberately NOT reset — see the header. An append can
        // only make lines longer, so the mark stays a valid lower bound, and resetting
        // it would shrink the horizontal range on every batch.
    } else {
        // Wrap has no append fast path, and cannot have one: appending to the last
        // paragraph changes how it wraps, and a paragraph's visual line count is the unit
        // the extent map is built from. So take the ordinary full invalidation.
        //
        // OnTextLayoutDirty rather than InvalidateLayout(): the latter only sets
        // layoutDirty_, which since §3 no longer covers the line index or the paragraph
        // extent map. Appending under Wrap with only that flag set left the paragraph
        // model describing the PREVIOUS text — the paragraph count did not grow, so the
        // new text was outside the document as far as scrolling and hit-testing were
        // concerned. (Caught by AppendUnderWrapExtendsTheParagraphModel.)
        text_.append(added);
        OnTextLayoutDirty();
    }

    const size_t dropped = TrimToMaxLines();

    // Publish. SyncScroll pushes the new extent (line count grew, so the scrollbar
    // lengthened); the tail follow reads the flag rather than recomputing it, because
    // the extent just changed underneath it.
    SyncScroll();
    (void)dropped;   // the trim already compensated the offset; nothing else needs it
    if (autoScrollEnabled_ && followingTail_) {
        ScrollToTail();   // re-rasterizes and commits on its own
    } else if (CompositionActive()) {
        // New text at the bottom of the document is new pixels on the surface whenever
        // the drawn band reaches it, and a trim moved the whole band's content. Both need
        // the surface re-rasterized; the offset itself was already handled above.
        RefreshComposition(/*redrawText*/ true, /*overlayForce*/ true);
    }
    Invalidate();

    // The event payload is the entire buffer BY VALUE. At log sizes that is the most
    // expensive thing here by a wide margin, so it is skipped entirely when nobody is
    // listening — which is the normal case for a log view. A subscriber pays the copy per
    // batch and that is the honest cost of asking for the full text on every change.
    if (textChanged_.HasSubscribers()) {
        std::wstring snapshot = text_;
        textChanged_.Raise(*this, snapshot);
    }
}

void TextArea::SetMaxLines(size_t n) {
    if (maxLines_ == n) return;
    maxLines_ = n;
    // Lowering the cap must take effect now, not at the next append: a caller that caps
    // a already-huge buffer is capping it precisely to release the memory.
    if (TrimToMaxLines() > 0) {
        SyncScroll();
        if (CompositionActive()) RefreshComposition(true, true);
        Invalidate();
    }
}

size_t TextArea::TrimToMaxLines() {
    if (maxLines_ == 0) return 0;

    size_t drop = 0;      // lines to discard from the front
    size_t bytes = 0;     // characters those lines occupy, including terminators

    if (wrapMode_ == TextWrapMode::NoWrap) {
        EnsureLineIndex();
        const size_t count = lines_.LineCount();
        if (count <= maxLines_) return 0;
        drop = count - maxLines_;
        // The first surviving line's start IS the number of characters to erase — the
        // index already knows it, so no scan is needed.
        bytes = lines_.LineRange(drop).first;
    } else {
        // No index in Wrap mode, so count newlines. O(document), which is the same order
        // Wrap already pays to re-lay-out after any change — it does not alter this
        // mode's cost profile, and the alternative (a cap that silently does nothing
        // under Wrap) is an API that lies.
        size_t total = 1;
        for (wchar_t c : text_) if (c == L'\n') ++total;
        if (total <= maxLines_) return 0;
        drop = total - maxLines_;
        size_t seen = 0;
        for (size_t i = 0; i < text_.size(); ++i) {
            if (text_[i] != L'\n') continue;
            if (++seen == drop) { bytes = i + 1; break; }
        }
    }
    if (drop == 0 || bytes == 0) return 0;

    text_.erase(0, bytes);

    if (wrapMode_ == TextWrapMode::NoWrap) {
        // TrimFront derives the shift from the index itself; the byte count is passed for
        // the desync check documented on LineIndex::TrimFront.
        lines_.TrimFront(drop, bytes);
        // MANDATORY: line numbers just shifted by `drop`, so every cached layout now maps
        // to the wrong text. The cache cannot detect this (the numbers it holds look
        // perfectly valid), which is why LineLayoutCache's header names the trim as the
        // caller's responsibility. This is the one place in the log path that pays
        // O(visible) instead of O(1) — once per `maxLines_` lines appended.
        lineLayouts_.Clear();
        ++lineCacheClears_;
    }

    // Caret and selection are absolute offsets into the buffer; everything before `bytes`
    // is gone. Shift them back, clamping the part that fell inside the discarded range to
    // the new start rather than letting it underflow to a huge UINT32.
    const UINT32 shift = static_cast<UINT32>(bytes);
    caret_ = caret_ > shift ? caret_ - shift : 0;
    selAnchor_ = selAnchor_ > shift ? selAnchor_ - shift : 0;

    // Offset compensation. Under NoWrap the height that vanished is exactly
    // drop * lineHeight, so subtracting it holds the visible text still — the whole
    // point, since a log capped at 10k lines trims constantly and a reader looking at
    // history must not see the text lurch upward on every trim. Under Wrap there is no
    // exact value (one logical line is an unknown number of visual lines), so the offset
    // is left to be re-clamped by the shrinking extent and the view can jump.
    if (wrapMode_ == TextWrapMode::NoWrap) {
        const float gone = drop * LineHeight();
        const float extent = ScrollExtentDip();
        if (CompositionActive()) {
            const float target = std::max(0.0f, content_->EffectiveOffset() - gone);
            content_->SetContentHeight(extent);
            content_->SetOffsetImmediate(target,
                [this](ID2D1DeviceContext* dc, float o, float h) {
                    DrawTextToSurface(dc, o, h);
                });
            // CRITICAL: sync scroll_ to match. Same reasoning as ScrollToTail — scroll_ is
            // the UI-thread scrollbar model that wheel/drag/tail-follow all read to judge
            // where the user is. Leaving it stale after the trim compensation makes the
            // scrollbar thumb render at the wrong position (stuck wherever it was before
            // the trim), and UpdateTailFollow reads an outdated offset.
            //
            // Use content_'s compensated target directly, NOT recomputing from scroll_:
            // the two MaxOffset values differ by 2*kPadY, and recomputing scroll_.Offset()
            // from scroll_.MaxOffset() would land 16 DIP off from where content_ actually is.
            scroll_.SetContentHeight(extent);
            scroll_.SetOffset(target);  // target from content_, not scroll_.Offset() - gone
        } else {
            scroll_.SetContentHeight(extent);
            scroll_.SetOffset(std::max(0.0f, scroll_.Offset() - gone));
        }
    }
    return drop;
}

void TextArea::ScrollToTail() {
    // Push the new extent before asking for the bound: the append that called this grew
    // the content, and MaxOffset() is derived from the extent the host was last told.
    const float extent = ScrollExtentDip();
    if (CompositionActive()) {
        content_->SetContentHeight(extent);
        // Immediate, not animated: a tween would be permanently chasing a target that
        // moves again on the next batch, so at any real log rate the view would never
        // actually reach the bottom and would smear instead of tracking.
        const float maxOff = content_->MaxOffset();
        content_->SetOffsetImmediate(maxOff,
            [this](ID2D1DeviceContext* dc, float o, float h) {
                DrawTextToSurface(dc, o, h);
            });
        RefreshComposition(/*redrawText*/ true, /*overlayForce*/ true);
        // CRITICAL: sync scroll_ to match. scroll_ is the UI-thread scrollbar model;
        // wheel/drag/tail-follow all read scroll_.TargetOffset() to decide whether the
        // user is at the bottom, so leaving it stale (at some old offset from a wheel
        // scroll before the tail following started) causes UpdateTailFollow to judge the
        // user as scrolled-away and detach incorrectly.
        //
        // Use content_'s MaxOffset directly, NOT scroll_.MaxOffset(): the two differ by
        // 2*kPadY (16 DIP) because content_ scrolls within the text inset while scroll_
        // measures from bounds.top. Using scroll_.MaxOffset() here put scroll_.Offset()
        // 16 DIP SHORT of where content_ actually is, breaking tail-follow detection.
        scroll_.SetContentHeight(extent);
        scroll_.SetOffset(maxOff);  // maxOff from content_, not scroll_.MaxOffset()
    } else {
        scroll_.SetContentHeight(extent);
        const float maxOff = scroll_.MaxOffset();
        scroll_.SetOffset(maxOff);
    }
    followingTail_ = true;
}

void TextArea::JumpToTail() {
    ScrollToTail();
}

void TextArea::SetAutoScrollToTail(bool enable) {
    if (autoScrollEnabled_ == enable) return;
    autoScrollEnabled_ = enable;
    if (!enable) return;
    // Arming it mid-session should take effect immediately if the view is already at the
    // bottom; if the user is reading history, respect that and wait until they scroll
    // back down. So: adopt the CURRENT position as the follow state rather than forcing
    // either answer.
    UpdateTailFollow();
    if (followingTail_) ScrollToTail();
}

void TextArea::UpdateTailFollow() {
    if (!autoScrollEnabled_) return;
    const float current = CurrentOffset();
    const float maxOff = MaxOffsetDip();
    const float lineH = LineHeight();
    const bool atTail = AtTailOffset(current, maxOff, lineH);
    followingTail_ = atTail;
}

void TextArea::ScrollHorizontallyBy(float deltaDip) {
    const float before = scroll_.OffsetX();
    scroll_.SetOffsetX(before + deltaDip);
    if (scroll_.OffsetX() == before) return;   // clamped: nothing moved, nothing to redraw
    // The horizontal offset is baked into the content surface's pixels (there is no
    // compositor OffsetX in this design — see the header), so a horizontal move must
    // force a text re-rasterize. The caret's surface position also shifts with it.
    if (CompositionActive()) RefreshComposition(/*redrawText*/ true, /*overlayForce*/ true);
    Invalidate();
}

void TextArea::EnsureCaretVisible() {
    SyncScroll();
    float x = 0, y = 0, h = 0;
    CaretMetrics(caret_ + static_cast<UINT32>(composition_.size()), x, y, h);

    // Horizontal first, and only under NoWrap (Wrap cannot scroll sideways). Reuses the
    // same pure helper the vertical axis and TreeView use, with the caret's own width as
    // the "item": a caret pressed against the right edge must bring its full 1 DIP into
    // view, not just its left edge, or typing at the edge leaves the bar half-clipped.
    //
    // Done BEFORE the vertical block so the single RefreshComposition below publishes
    // both axes in one re-rasterize instead of two.
    if (wrapMode_ == TextWrapMode::NoWrap) {
        const float targetX = EnsureVisibleOffset(x, kCaretW, scroll_.OffsetX(),
                                                  ContentWidth());
        scroll_.SetOffsetX(targetX);
    }
    // Caret Y is relative to the text origin; the viewport spans
    // [Offset, Offset + ContentHeight) in that same space.
    float top = y;
    float bottom = y + h;
    float viewTop = CurrentOffset();
    float viewBottom = viewTop + ContentHeight();
    float target = viewTop;
    if (top < viewTop) target = top;
    else if (bottom > viewBottom) target = bottom - ContentHeight();

    if (CompositionActive()) {
        // Typing / keyboard nav jumps immediately (no glide), matching TreeView.
        if (std::fabs(target - content_->EffectiveOffset()) > 0.5f) {
            content_->SetOffsetImmediate(target,
                [this](ID2D1DeviceContext* dc, float o, float h2) {
                    DrawTextToSurface(dc, o, h2);
                });
        }
        // The caret moved even when the offset did not, and the text surface must
        // re-rasterize because the caret's old cell may have held a selection edge.
        RefreshComposition(/*redrawText*/ true, /*overlayForce*/ true);
    } else if (target != scroll_.Offset()) {
        scroll_.SetOffset(target);
    }
    // Keyboard navigation is user-driven scrolling too, so it decides the follow state:
    // Ctrl+End re-attaches, Ctrl+Home / PageUp detach. Placed here rather than in
    // OnNavigationKey so every caret-driven scroll is covered by one call, including the
    // drag-select auto-scroll that routes through here.
    UpdateTailFollow();
    Invalidate();
}

void TextArea::OnBoundsChanged() {
    LayoutCostProbe::Scope probe(LayoutCostKey::TextAreaBoundsChanged);
    InvalidateLayout();  // wrap width depends on bounds
    // The wrap width changed, which invalidates every measured paragraph height (a
    // paragraph breaks differently at a new width). EnsureWrapExtent detects the width
    // change itself and resets, so nothing is flagged here — but note what this costs
    // now versus before: a Reset is O(paragraphs) of integer arithmetic, where the old
    // whole-document layout re-wrapped the entire buffer on every resize frame.
    SyncScroll();
    // redrawText=true is NOT known to be necessary here, and measurement says it is
    // most of the resize cost (DComp:ourDrawing was 5.5-6.5ms across a page's twelve
    // composited surfaces, against 0.4-0.55ms for the surface allocation). A
    // conditional second pass was tried and deleted: by the time the first
    // RefreshComposition returns, Rebase has already rasterized and set contentDrawn_,
    // so any "did the pixels survive" test after it is always false — dead code that
    // reads as a working optimization. Skipping the redraw for real needs the geometry
    // and the pixels to be separable (push viewport/clip/extent now, rasterize on
    // WM_EXITSIZEMOVE), which is a change to ScrollContentHost, not a flag here.
    if (CompositionActive()) RefreshComposition(true, true);  // reposition + reclip
}

void TextArea::UpdateContextModalResize(bool inModalResize) {
    UIElement::UpdateContextModalResize(inModalResize);
    // On the falling edge (modal resize ends), force a composition refresh even if
    // bounds didn't change: the DComp surfaces were scaled/clipped to a placeholder
    // size during the drag, and now need to be rasterized at the final resolution.
    if (!inModalResize && CompositionActive()) {
        RefreshComposition(/*redrawText*/ true, /*overlayForce*/ true);
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

bool TextArea::OnNavigationKey(UINT vk, bool shift) {
    const UINT32 len = static_cast<UINT32>(text_.size());
    switch (vk) {
        case VK_LEFT:
            if (HasSelection() && !shift) MoveCaret(std::min(caret_, selAnchor_), false);
            else MoveCaret(caret_ > 0 ? caret_ - 1 : 0, shift);
            return true;
        case VK_RIGHT:
            if (HasSelection() && !shift) MoveCaret(std::max(caret_, selAnchor_), false);
            else MoveCaret(caret_ + 1, shift);
            return true;
        case VK_UP:    MoveCaret(IndexByLineStep(-1), shift); return true;
        case VK_DOWN:  MoveCaret(IndexByLineStep(+1), shift); return true;
        case VK_PRIOR: MoveCaret(IndexByPageStep(-1), shift); return true;
        case VK_NEXT:  MoveCaret(IndexByPageStep(+1), shift); return true;
        case VK_HOME: {
            // Ctrl+Home -> document start; else start of the current visual line.
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            MoveCaret(ctrl ? 0 : LineEdgeIndex(caret_, /*toEnd*/ false), shift);
            return true;
        }
        case VK_END: {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            MoveCaret(ctrl ? len : LineEdgeIndex(caret_, /*toEnd*/ true), shift);
            return true;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void TextArea::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    float dipX = e.position.x, dipY = e.position.y;
    // Vertical thumb first (right edge), horizontal thumb second (bottom edge).
    // The two hit regions are disjoint when HTrackLength() reserves the corner, so
    // at most one branch fires.
    // Press anywhere in a rail's hover strip (16 DIP), not just on the visible thumb
    // (3-7 DIP): the press target must match the region that gives hover feedback,
    // or the rail lights up under the pointer and then ignores the click. Off-thumb
    // presses first center the thumb on the pointer so the drag does not leap on the
    // first move (DragTo is relative to where the press landed).
    if (scroll_.HitBarRegion(dipX, dipY)) {
        if (!scroll_.HitThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
        }
        scroll_.BeginDrag(dipY);
        if (Context().input) Context().input->CapturePointer(this);
        if (CompositionActive()) RefreshComposition(false, true);
        e.handled = true;
        return;
    }
    if (scroll_.HitHBarRegion(dipX, dipY)) {
        if (!scroll_.HitHThumb(dipX, dipY)) {
            // Track range from bounds_.w rather than the private HTrackLength():
            // the difference is only the corner reserve when both rails are live,
            // which shifts the jump target by a few DIP at most — imperceptible for
            // a click-to-jump, and not worth widening ScrollViewer's public surface.
            const RectDip hthumb = scroll_.HThumbRect();
            const float htrackRange = std::max(1.0f, bounds_.w - hthumb.w);
            const float wantLeft = (dipX - hthumb.w * 0.5f) - bounds_.x;
            scroll_.SetOffsetX(wantLeft / htrackRange * scroll_.MaxOffsetX());
        }
        scroll_.BeginHDrag(dipX);
        if (Context().input) Context().input->CapturePointer(this);
        if (CompositionActive()) RefreshComposition(false, true);
        e.handled = true;
        return;
    }
    const UINT32 index = HitIndex(dipX, dipY);
    // Double-click selects the word, triple selects the logical line. Both also record
    // the drag granularity so OnPointerMoved extends by whole units.
    if (!ApplyMultiClickSelection(index, e.clickCount)) {
        caret_ = selAnchor_ = index;
        BeginCharacterDrag(index);
    }
    selecting_ = true;
    if (Context().input) Context().input->CapturePointer(this);
    ResetBlink();
    if (CompositionActive()) RefreshComposition(true, false);
    Invalidate();
    e.handled = true;
}

void TextArea::AutoScrollForDragSelect(float dipY) {
    // Dragging a selection past the top/bottom of the text region scrolls toward the
    // pointer, so a selection can extend beyond one screenful. Speed grows with how
    // far outside the pointer is (capped), which is the familiar editor feel.
    dragPointerY_ = dipY;
    const float top = bounds_.y + ContentTop();
    const float bottom = bounds_.y + ContentTop() + ContentHeight();
    float overshoot = 0.0f;
    if (dipY < top) overshoot = dipY - top;            // negative: scroll up
    else if (dipY > bottom) overshoot = dipY - bottom;  // positive: scroll down
    dragOutsideEdge_ = overshoot != 0.0f;
    if (!dragOutsideEdge_) return;

    const float step = std::clamp(overshoot, -LineHeight() * 3.0f, LineHeight() * 3.0f);
    if (CompositionActive()) {
        content_->SetOffsetImmediate(content_->EffectiveOffset() + step,
            [this](ID2D1DeviceContext* dc, float o, float h) {
                DrawTextToSurface(dc, o, h);
            });
    } else {
        scroll_.SetOffset(scroll_.Offset() + step);
    }
}

void TextArea::OnPointerMoved(PointerEventArgs& e) {
    float dipX = e.position.x, dipY = e.position.y;
    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        if (CompositionActive()) {
            content_->SetOffsetImmediate(scroll_.Offset(),
                [this](ID2D1DeviceContext* dc, float o, float h) {
                    DrawTextToSurface(dc, o, h);
                });
            RefreshComposition(false, true);
        }
        // A thumb drag is a user-driven scroll, so it is one of the places that decides
        // whether the view is still following the tail (dragging to the bottom re-attaches,
        // dragging away detaches). Appends must never make this call — see UpdateTailFollow.
        UpdateTailFollow();
        Invalidate(); e.handled = true; return;
    }
    if (scroll_.IsHDragging()) {
        scroll_.HDragTo(dipX);
        // Horizontal motion bakes into pixels, so force a content redraw. No tail-follow
        // update: the horizontal axis has nothing to do with being at the bottom.
        if (CompositionActive()) RefreshComposition(true, true);
        Invalidate(); e.handled = true; return;
    }
    if (selecting_) {
        AutoScrollForDragSelect(dipY);
        ExtendDragSelection(HitIndex(dipX, dipY));
        EnsureCaretVisible();
        Invalidate();
        e.handled = true;
        return;
    }
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));
    scroll_.SetHBarHover(scroll_.HitHBarRegion(dipX, dipY));
    if (CompositionActive()) RefreshComposition(false, false);
}

void TextArea::OnPointerLeft() {
    scroll_.SetBarHover(false);
    scroll_.SetHBarHover(false);
}

void TextArea::OnPointerReleased(PointerEventArgs& e) {
    bool active = scroll_.IsDragging() || scroll_.IsHDragging() || selecting_;
    if (scroll_.IsDragging()) scroll_.EndDrag();
    if (scroll_.IsHDragging()) scroll_.EndHDrag();
    selecting_ = false;
    dragOutsideEdge_ = false;
    if (active && Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    if (active) {
        e.handled = true;
        if (CompositionActive()) RefreshComposition(false, true);
    }
}

void TextArea::OnPointerWheelChanged(PointerEventArgs& e) {
    // Shift+Wheel scrolls horizontally (NoWrap only: Wrap cannot scroll sideways by
    // construction). Unmodified Wheel scrolls vertically regardless of mode.
    const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
    if (shift && wrapMode_ == TextWrapMode::NoWrap) {
        if (scroll_.MaxOffsetX() <= 0.0f) return;  // nothing to scroll (bubbles up)
        float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
        ScrollHorizontallyBy(lines * LineHeight());
        scroll_.Wake();   // fade the scrollbar in so the user sees horizontal motion
        e.handled = true;
        return;
    }

    // Vertical scrolling: same path both modes.
    if (CompositionActive()) {
        if (content_->MaxOffset() <= 0.0f) return;
        float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
        content_->AnimateBy(lines * LineHeight(),
            [this](ID2D1DeviceContext* dc, float o, float h) {
                DrawTextToSurface(dc, o, h);
            });
        scroll_.Wake();
        RefreshComposition(false, true);
        // Detach from the tail as soon as the wheel aims away from the bottom. Judged on
        // the TARGET, not the effective offset: a wheel scroll is a tween, so the
        // effective offset is still at the bottom on this frame and the user would have to
        // keep scrolling to make it stick. The fling's landing is re-judged on each tick
        // (OnAnimationTick), which is what re-attaches a scroll back down to the end.
        if (autoScrollEnabled_)
            followingTail_ = AtTailOffset(content_->TargetOffset(), content_->MaxOffset(),
                                         LineHeight());
        Invalidate();
        e.handled = true;
        return;
    }
    if (scroll_.MaxOffset() <= 0.0f) return;
    float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
    scroll_.AnimateBy(lines * LineHeight());
    // Target, not Offset, for the same reason as the composited branch above.
    if (autoScrollEnabled_)
        followingTail_ = AtTailOffset(scroll_.TargetOffset(), scroll_.MaxOffset(),
                                     LineHeight());
    Invalidate();
    e.handled = true;
}

// ---------------------------------------------------------------------------
// Caret rect (for IME candidate placement)
// ---------------------------------------------------------------------------

bool TextArea::CaretRectDip(RectDip& out) const {
    float x = 0, y = 0, h = 0;
    CaretMetrics(caret_ + static_cast<UINT32>(composition_.size()), x, y, h);
    float cx = bounds_.x + ContentLeft() + x;
    // EFFECTIVE offset: the IME candidate window must sit at the caret the user can
    // SEE, so mid-tween this has to be the compositor's interpolated value.
    float cy = bounds_.y + ContentTop() + y - CurrentOffset();
    out = {cx, cy, kCaretW, h};
    return true;
}

// ---------------------------------------------------------------------------
// Compositor scrolling (Phase 4). Every path is a no-op when CompositionActive()
// is false — the control then uses the UI-thread scroll_ path unchanged.
// ---------------------------------------------------------------------------

float TextArea::CurrentOffset() const {
    return CompositionActive() ? content_->EffectiveOffset() : scroll_.Offset();
}

// The horizontal offset is always read from scroll_, because there is no compositor
// OffsetX tween — horizontal motion is baked into the content surface pixels, so
// what scroll_ holds IS what's on screen (no interpolated position to reconcile).
float TextArea::CurrentOffsetX() const {
    return scroll_.OffsetX();
}

ScrollContentHost::Inset TextArea::TextInset() const {
    // The text scrolls inside the padding and left of the scrollbar gutter; the
    // scrollbar and the box frame paint over the full bounds (so they are NOT inset).
    ScrollContentHost::Inset in;
    in.left = ContentLeft();
    in.top = ContentTop();
    in.right = kScrollReserve;
    in.bottom = ContentTop();
    return in;
}

bool TextArea::WantsAnimationTick() const {
    if (!IsEffectivelyVisible()) return false;
    if (CompositionActive()) {
        // The text tween runs on the compositor (no UI tick needed for it), but we
        // still tick to advance the scrollbar fade, to track the thumb during a fling,
        // and to keep auto-scrolling while a drag-select holds outside the edge.
        return content_->IsAnimating() || scroll_.NeedsTick() || dragOutsideEdge_;
    }
    return scroll_.NeedsTick();
}

void TextArea::OnAnimationTick(float dtSec) {
    if (!IsEffectivelyVisible()) return;
    // Runs in PumpAnimations, OUTSIDE the window content frame — safe to refill
    // surfaces here. The host renders once after ticking, so don't self-invalidate.
    scroll_.Tick(dtSec);
    if (dragOutsideEdge_ && selecting_) {
        // Keep pulling the view while the pointer sits outside the text region. Goes
        // through ExtendDragSelection, not a bare caret_ assignment: a double-click drag
        // that runs off the edge must keep selecting whole words, and this tick path is
        // what drives the selection once the pointer stops moving.
        AutoScrollForDragSelect(dragPointerY_);
        ExtendDragSelection(HitIndex(bounds_.x + ContentLeft(), dragPointerY_));
        if (CompositionActive()) { RefreshComposition(true, true); return; }
    }
    // A fling settles over several frames, so its LANDING is only known here. Re-judging
    // each tick is what lets "scroll back down to the bottom" re-attach tail following
    // without the user having to nudge the wheel again once it stops.
    UpdateTailFollow();
    // Ticks never change the TEXT, so never force a text redraw here; the overlay is
    // redrawn only when its signature actually moved (a static idle-visible scrollbar
    // must not be re-rasterized every frame — that shimmers its edges).
    if (CompositionActive()) RefreshComposition(false, false);
    else Invalidate();  // fallback path: scroll_ is a member whose Invalidate() cannot
                        // reach the tree, so fade/expand steps need this to repaint
}

void TextArea::OnBlink() {
    // Composition mode never gets here (WantsBlink() is false — the compositor owns
    // the blink). This is the UI-thread fallback.
    caretVisible_ = !caretVisible_;
    Invalidate();
}

void TextArea::OnTextLayoutDirty() {
    layoutDirty_ = true;
    lineIndexDirty_ = true;
    // The paragraph structure changed, so every visual-line count — measured or estimated
    // — describes text that may no longer be there. Rebuilding from estimates is the only
    // correct move; keeping measurements keyed by paragraph number after an edit that
    // inserted a newline would attribute one paragraph's height to another.
    wrapExtentDirty_ = true;
    lineLayouts_.Clear();
    ++lineCacheClears_;
    // The text changed, so any line that was widest before may be gone, and a new
    // longest line may have arrived. The high-water mark is therefore stale; reset it
    // so it builds back up honestly as lines are drawn. The alternative — keeping the
    // old mark — would let a user delete a long line and still see MaxOffsetX() report
    // the deleted line's width, with no way to scroll back there.
    maxSeenLineWidth_ = 0.0f;
    // Composition layout cache is stale (it's keyed by paragraph number + width, but
    // the paragraph structure/width may have changed).
    compositionLayoutPara_ = static_cast<size_t>(-1);
    if (CompositionActive()) RefreshComposition(true, true);
}

// An IME composition changed, but text_ did not. Under NoWrap that means the line
// index is still valid (it is built over text_) and so is every cached line layout
// except the caret's — and the caret's line is not cached at all while composing,
// LineLayout builds it fresh each time. So there is nothing to invalidate here beyond
// re-rasterizing, which is what makes composing in a 28 MB document cost the same as
// composing in an empty one.
//
// Wrap has no per-line model to preserve, so it takes the full path unchanged.
void TextArea::OnCompositionDirty() {
    if (wrapMode_ != TextWrapMode::NoWrap) { OnTextLayoutDirty(); return; }
    if (CompositionActive()) RefreshComposition(/*redrawText*/ true, /*overlayForce*/ true);
}

void TextArea::OnSelectionChanged() {
    // redrawText: the highlight is part of the text surface's pixels.
    // overlayForce false: the scrollbar and frame did not move, so let the signature
    // gate decide — re-rasterizing a static overlay shimmers its antialiased edges.
    if (CompositionActive()) RefreshComposition(/*redrawText*/ true, /*overlayForce*/ false);
    Invalidate();
}

void TextArea::DrawTextToSurface(ID2D1DeviceContext* dc, float surfaceOriginDip,
                                 float surfaceHeightDip) {
    if (!dc) return;
    const ColorTokens& pal = Theme().colors;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf())))
        return;
    DrawingContext rc{dc, brush.Get(), Context().dpiScale};

    // The dc arrives scaled to DIPs with (0,0) at the text region's top-left (the host
    // put the padding on the clip node). Layout-local Y maps to local y = Y -
    // surfaceOriginDip; X is layout-local already.
    const float originX = 0.0f;
    const float originY = -surfaceOriginDip;

    // Only emptiness matters here (placeholder vs real text); DisplayEmpty() answers it
    // without composing the string.
    if (DisplayEmpty() && !placeholder_.empty()) {
        if (IDWriteTextFormat* fmt = Dwrite() ? Dwrite()->Format(
                EffectiveFontSize(), EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL), DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP) : nullptr) {
            rc.DrawText(placeholder_.c_str(), static_cast<UINT32>(placeholder_.size()),
                        fmt, D2D1::RectF(originX, originY, originX + ContentWidth(),
                                         originY + ContentHeight()),
                        pal.textSecondary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        return;
    }

    // Both modes draw only the band, from per-row cached layouts. The rows differ (a
    // NoWrap row is a logical line of constant height; a Wrap row is a paragraph of
    // measured height) but the shape does not — and neither path builds anything
    // document-sized.
    //
    // NOTE: no caret in either — it is its own visual so it can blink on the compositor
    // without re-rasterizing this surface (see ScrollContentHost::SetCaret).
    // originX/originY 故意不传：DrawLinesToSurface 内部已经按
    // localY = originY + lineTop - surfaceOriginDip 做过带偏移的换算，
    // 这里再传 originY = -surfaceOriginDip 会把 surfaceOriginDip 减两次，
    // 文字落到负坐标（surface 外），看起来就是"黑框里什么都没有"。
    if (wrapMode_ == TextWrapMode::NoWrap)
        DrawLinesToSurface(rc, surfaceOriginDip, surfaceHeightDip);
    else
        DrawParagraphsToSurface(rc, surfaceOriginDip, surfaceHeightDip);
}

// The virtualized draw loop. Same shape as TreeView::DrawRowsToSurface: derive the
// first visible row from the surface origin, walk forward until past the surface's
// bottom edge, stop. Cost is O(rows in the band), independent of document size — and
// that is only possible because a NoWrap line height is constant and the line count
// is known without laying anything out.
//
// SELECTION IS THE HARD PART, and it is worth being explicit about why. The Wrap path
// asks the whole-document layout for the selection rectangles in one call
// (HitTestTextRange), which quietly handles the three cases: a selection inside one
// line, one spanning several, and one covering whole lines in the middle. With per-line
// layouts, no single object knows about more than one line, so this code has to
// intersect the selection with each line itself. Getting that intersection wrong shows
// up as a highlight that stops short of a line end, or one that bleeds into a line the
// user did not select.
LineSpan TextArea::VisibleLineSpanForBand(float bandTopDip, float bandHeightDip) const {
    if (wrapMode_ != TextWrapMode::NoWrap) return {};
    EnsureLineIndex();
    return VisibleLineSpan(bandTopDip, bandHeightDip, LineHeight(), lines_.LineCount());
}

size_t TextArea::DrawMinCoverForLine(size_t lineNumber) const {
    // In Wrap mode there is no per-line clip (the whole-document layout is used) and
    // the question has no meaning — return full line as an honest "clip does not apply".
    if (wrapMode_ != TextWrapMode::NoWrap) return LineLayoutCache::kFullLine;
    EnsureLineIndex();
    if (lineNumber >= lines_.LineCount()) return 0;

    auto [lineStart, lineEnd] = lines_.LineRange(lineNumber);
    const size_t lineLen = lineEnd - lineStart;

    static constexpr size_t kClipThreshold = 4000;
    if (lineLen <= kClipThreshold) return lineLen;  // below threshold: full layout

    // Mirror the logic in DrawLinesToSurface exactly, but without the draw state
    // (surfaceOriginDip / selection from the loop). The draw loop passes the current
    // selection range from its outer scope; here we read the same fields directly.
    const float avgGlyphW = std::max(1.0f, EffectiveFontSize() * 0.5f);
    const float visRight = scroll_.OffsetX() + ContentWidth() * 2.0f;
    const size_t estCover = static_cast<size_t>(visRight / avgGlyphW) + 50;

    size_t caretCover = 0;
    if (caret_ >= lineStart && caret_ <= lineEnd)
        caretCover = (caret_ - lineStart) + composition_.size() + 1;

    size_t selCover = 0;
    if (IsFocused() && HasSelection() && composition_.empty()) {
        UINT32 selStart = 0, selLen = 0;
        SelectionRange(selStart, selLen);
        const UINT32 selEnd = selStart + selLen;
        if (selEnd > lineStart && selStart <= lineEnd) {
            const UINT32 selClip = std::min(selEnd, static_cast<UINT32>(lineEnd));
            selCover = selClip - static_cast<UINT32>(lineStart);
        }
    }

    size_t maxNeeded = estCover > caretCover ? estCover : caretCover;
    if (selCover > maxNeeded) maxNeeded = selCover;
    if (maxNeeded < 1) maxNeeded = 1;
    return lineLen < maxNeeded ? lineLen : maxNeeded;
}

void TextArea::DrawLinesToSurface(DrawingContext& rc, float surfaceOriginDip,
                                  float surfaceHeightDip, float originX,
                                  float originY) {
    const ColorTokens& pal = Theme().colors;
    EnsureLineIndex();

    const float lh = LineHeight();
    if (lh <= 0.0f) return;

    // Horizontal offset (NoWrap only, Wrap returns 0): shift all drawing left by the
    // scroll position so the content-space column at offsetX maps to pixel column 0.
    // This is intentionally different from the vertical axis, where the compositor
    // owns OffsetY and this function only provides per-surface-band coordinates. There
    // is no compositor OffsetX (see the design notes), so the shift is baked here.
    const float offsetX = CurrentOffsetX();

    std::wstring scratch;
    const std::wstring& disp = DisplayText(scratch);

    // Selection, in absolute character offsets. Drawn per line below.
    UINT32 selStart = 0, selLen = 0;
    const bool drawSelection = IsFocused() && HasSelection() && composition_.empty();
    if (drawSelection) SelectionRange(selStart, selLen);
    const UINT32 selEnd = selStart + selLen;

    // IME composition range, likewise absolute.
    const UINT32 imeStart = caret_;
    const UINT32 imeEnd = caret_ + static_cast<UINT32>(composition_.size());

    const LineSpan span = VisibleLineSpanForBand(surfaceOriginDip, surfaceHeightDip);

    for (size_t i = span.first; i < span.last; ++i) {
        const float lineTop = LineTopDip(i);
        const float localY = originY + lineTop - surfaceOriginDip;
        auto [lineStart, lineEnd] = lines_.LineRange(i);
        const size_t lineLen = lineEnd - lineStart;

        // Prefix-clip long lines: build a layout only wide enough to cover the
        // visible horizontal window plus the safety margins. Characters scrolled
        // off to the left are still addressed at offset 0 (the layout always starts
        // at the line's first character), so hit-test coordinates are unchanged.
        //
        // The threshold is 4 000 characters — a line that long at any reasonable font
        // is several screen-widths wide, so the visible prefix is always much shorter.
        // Lines below the threshold bypass this entirely and get the full layout, which
        // is the steady state for every real-world log file.
        //
        const size_t minCover = DrawMinCoverForLine(i);

        IDWriteTextLayout* layout = LineLayout(i, minCover);
        if (!layout) continue;

        const float lineX = originX - offsetX;

        // --- Selection highlight -----------------------------------------------
        if (drawSelection && selEnd > lineStart && selStart <= lineEnd) {
            const UINT32 clipStart = std::max(selStart, static_cast<UINT32>(lineStart));
            const UINT32 clipEnd = std::min(selEnd, static_cast<UINT32>(lineEnd));
            const UINT32 localStart = clipStart - static_cast<UINT32>(lineStart);
            const UINT32 localLen = (clipEnd > clipStart) ? clipEnd - clipStart : 0u;
            const D2D1_COLOR_F sel =
                D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.30f);
            if (localLen > 0) {
                UINT32 actual = 0;
                layout->HitTestTextRange(localStart, localLen, 0.0f, localY,
                                         nullptr, 0, &actual);
                if (actual > 0) {
                    std::vector<DWRITE_HIT_TEST_METRICS> m(actual);
                    if (SUCCEEDED(layout->HitTestTextRange(localStart, localLen, 0.0f,
                                                           localY, m.data(), actual,
                                                           &actual))) {
                        for (UINT32 k = 0; k < actual; ++k)
                            rc.FillRect(D2D1::RectF(lineX + m[k].left, m[k].top,
                                                    lineX + m[k].left + m[k].width,
                                                    m[k].top + m[k].height), sel);
                    }
                }
            } else if (selStart <= lineStart && selEnd > lineEnd) {
                rc.FillRect(D2D1::RectF(lineX, localY, lineX + lh * 0.5f,
                                        localY + lh), sel);
            }
        }

        // --- IME composition underline -----------------------------------------
        if (!composition_.empty()) {
            const size_t caretLine = lines_.LineFromOffset(caret_);
            const bool isCaretLine = (i == caretLine);

            if (isCaretLine) {
                // Caret line: layout includes composition, use layout-space coordinates.
                // Composition insertion position in layout: caret_ - lineStart
                const UINT32 layoutCompStart = (caret_ >= lineStart && caret_ <= lineEnd)
                                                   ? static_cast<UINT32>(caret_ - lineStart)
                                                   : static_cast<UINT32>(lineLen);
                const UINT32 layoutCompLen = static_cast<UINT32>(composition_.size());

                if (layoutCompLen > 0) {
                    UINT32 actual = 0;
                    layout->HitTestTextRange(layoutCompStart, layoutCompLen, 0.0f, localY,
                                             nullptr, 0, &actual);
                    if (actual > 0) {
                        std::vector<DWRITE_HIT_TEST_METRICS> m(actual);
                        if (SUCCEEDED(layout->HitTestTextRange(layoutCompStart, layoutCompLen,
                                                               0.0f, localY, m.data(), actual,
                                                               &actual))) {
                            for (UINT32 k = 0; k < actual; ++k)
                                rc.FillRect(D2D1::RectF(lineX + m[k].left,
                                                        m[k].top + m[k].height - 2.0f,
                                                        lineX + m[k].left + m[k].width,
                                                        m[k].top + m[k].height), pal.accent);
                        }
                    }
                }
            }
        }

        D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
        rc.DrawTextLayout(D2D1::Point2F(lineX, localY), layout, textColor,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

// The wrapped draw loop. Same shape as DrawLinesToSurface — walk the rows the band
// covers, ask the cache for each row's layout, draw selection then text — with two
// differences that follow entirely from rows being variable-height:
//
// 1. EACH PARAGRAPH IS MEASURED AS IT IS DRAWN, and the measurement replaces the
//    estimate in the extent map. That is where the scrollbar's convergence comes from:
//    drawing IS measuring, so a region becomes exact exactly when the user looks at it.
//    It also means this loop MUTATES layout state, which is unusual for a draw path and
//    is why the extent is a mutable member.
//
// 2. THE LOOP BOUND IS RECOMPUTED AS IT GOES. A paragraph estimated at 2 visual lines
//    that turns out to be 9 pushes everything below it down, so the span computed before
//    the first measurement can be short by several paragraphs. Re-asking the extent map
//    for each row's top means a paragraph that grew is still drawn at the right Y and the
//    loop keeps going until the band is genuinely covered. The alternative — trusting the
//    initial span — leaves a wedge of blank surface below a paragraph that grew, until
//    some later frame happens to redraw it.
//
// Selection is intersected per paragraph, exactly as the NoWrap path does it per line;
// the same reasoning applies (no single layout knows about more than one row) and so do
// the same failure modes if the intersection is wrong.
void TextArea::DrawParagraphsToSurface(DrawingContext& rc, float surfaceOriginDip,
                                       float surfaceHeightDip, float originX,
                                       float originY) {
    const ColorTokens& pal = Theme().colors;
    EnsureWrapExtent();

    const float lh = LineHeight();
    if (!(lh > 0.0f)) return;
    const float bandBottom = surfaceOriginDip + surfaceHeightDip;

    // Selection and IME range in absolute offsets, clipped per paragraph below.
    UINT32 selStart = 0, selLen = 0;
    const bool drawSelection = IsFocused() && HasSelection() && composition_.empty();
    if (drawSelection) SelectionRange(selStart, selLen);
    const UINT32 selEnd = selStart + selLen;
    const UINT32 imeStart = caret_;
    const UINT32 imeEnd = caret_ + static_cast<UINT32>(composition_.size());

    // Measure the whole band FIRST, through the same method a test drives, then draw from
    // the settled geometry. Two reasons this is a separate pass rather than measure-as-you-
    // draw:
    //
    //   * every paragraph's Y is final before anything is painted, so a paragraph that
    //     measured taller than its estimate cannot leave the ones below it drawn at a
    //     stale Y within this same frame;
    //   * the measuring walk is then one method with one caller shape, so a test entering
    //     through MeasureParagraphsInBand exercises the loop's real bound instead of a
    //     lookalike. Measuring inline here would put that decision back inside a function
    //     no headless test can reach past its null-DC guard.
    if (MeasureParagraphsInBand(surfaceOriginDip, surfaceHeightDip) == 0) return;

    const LineSpan span = WrapParagraphSpanForBand(surfaceOriginDip, surfaceHeightDip);
    if (span.Empty()) return;

    for (size_t p = span.first; p < wrapExtent_.ParagraphCount(); ++p) {
        const float top = wrapExtent_.ParagraphTopDip(p, lh);
        if (top >= bandBottom) break;

        IDWriteTextLayout* layout = WrapParagraphLayout(p);
        if (!layout) break;

        const float localY = originY + top - surfaceOriginDip;
        auto [paraStart, paraEnd] = lines_.LineRange(p);

        // --- Selection highlight ------------------------------------------------
        if (drawSelection && selEnd > paraStart && selStart <= paraEnd) {
            const UINT32 clipStart = std::max(selStart, static_cast<UINT32>(paraStart));
            const UINT32 clipEnd = std::min(selEnd, static_cast<UINT32>(paraEnd));
            const UINT32 localStart = clipStart - static_cast<UINT32>(paraStart);
            const UINT32 localLen = (clipEnd > clipStart) ? clipEnd - clipStart : 0u;
            const D2D1_COLOR_F sel =
                D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.30f);
            if (localLen > 0) {
                UINT32 actual = 0;
                layout->HitTestTextRange(localStart, localLen, originX, localY,
                                         nullptr, 0, &actual);
                if (actual > 0) {
                    std::vector<DWRITE_HIT_TEST_METRICS> m(actual);
                    if (SUCCEEDED(layout->HitTestTextRange(localStart, localLen, originX,
                                                           localY, m.data(), actual,
                                                           &actual))) {
                        for (UINT32 k = 0; k < actual; ++k)
                            rc.FillRect(D2D1::RectF(m[k].left, m[k].top,
                                                    m[k].left + m[k].width,
                                                    m[k].top + m[k].height), sel);
                    }
                }
            } else if (selStart <= paraStart && selEnd > paraEnd) {
                // An empty paragraph fully inside the selection still needs a visible
                // sliver, or a multi-paragraph selection appears to skip blank lines.
                rc.FillRect(D2D1::RectF(originX, localY, originX + lh * 0.5f,
                                        localY + lh), sel);
            }
        }

        // --- IME composition underline -------------------------------------------
        if (!composition_.empty()) {
            const size_t caretPara = lines_.LineFromOffset(caret_);
            const bool isCaretPara = (p == caretPara);

            if (isCaretPara) {
                // Caret paragraph: layout includes composition, use layout-space coordinates.
                const UINT32 layoutCompStart = (caret_ >= paraStart && caret_ <= paraEnd)
                                                   ? static_cast<UINT32>(caret_ - paraStart)
                                                   : static_cast<UINT32>(paraEnd - paraStart);
                const UINT32 layoutCompLen = static_cast<UINT32>(composition_.size());

                if (layoutCompLen > 0) {
                    UINT32 actual = 0;
                    layout->HitTestTextRange(layoutCompStart, layoutCompLen, originX, localY,
                                             nullptr, 0, &actual);
                    if (actual > 0) {
                        std::vector<DWRITE_HIT_TEST_METRICS> m(actual);
                        if (SUCCEEDED(layout->HitTestTextRange(layoutCompStart, layoutCompLen,
                                                               originX, localY, m.data(), actual,
                                                               &actual))) {
                            for (UINT32 k = 0; k < actual; ++k)
                                rc.FillRect(D2D1::RectF(m[k].left,
                                                        m[k].top + m[k].height - 2.0f,
                                                        m[k].left + m[k].width,
                                                        m[k].top + m[k].height), pal.accent);
                        }
                    }
                }
            }
        }

        D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
        rc.DrawTextLayout(D2D1::Point2F(originX, localY), layout, textColor,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void TextArea::DrawOverlayToSurface(ID2D1DeviceContext* dc, float /*wDip*/,
                                    float /*hDip*/) {
    if (!dc) return;
    const float s = Context().dpiScale;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf())))
        return;
    // scroll_.Render draws in WINDOW DIPs (bounds_-relative). PREMULTIPLY onto the
    // incoming transform — it carries the surface's atlas tile origin, which moves
    // between draws; replacing it puts the drawing outside our tile (it flickers).
    D2D1_MATRIX_3X2_F base;
    dc->GetTransform(&base);
    dc->SetTransform(SurfaceTransformFromWindowDip(base, bounds_.x, bounds_.y, s));
    DrawingContext rc{dc, brush.Get(), s};
    scroll_.Render(rc);
}

void TextArea::SyncCaretVisual() {
    if (!CompositionActive()) return;
    const bool show = IsFocused();
    if (!show) { content_->SetCaretVisible(false); return; }

    float x = 0, y = 0, h = 0;
    CaretMetrics(caret_ + static_cast<UINT32>(composition_.size()), x, y, h);
    if (h <= 0.0f) h = LineHeight();
    // The caret's parent is the content visual, whose left edge is the surface's left.
    // The surface draws glyphs at (lineX - offsetX) = content-space X minus the scroll
    // offset, so the caret must be placed at the same shifted X to sit ON the cursor
    // position rather than n columns to its right.
    const float caretSurfaceX = x - CurrentOffsetX();
    content_->SetCaret({caretSurfaceX, y, kCaretW, h}, Theme().colors.textPrimary);
    content_->SetCaretVisible(true);
    content_->StartCaretBlink(CaretBlinkHalfPeriodSec());
}

OverlaySignature TextArea::CurrentOverlaySig() const {
    OverlaySignature s;
    s.visibility = scroll_.Visibility();
    s.expand     = scroll_.ExpandFactor();
    s.thumbY     = scroll_.ThumbRect().y;
    s.focused    = IsFocused();
    s.dragging   = scroll_.IsDragging();
    s.expandX    = scroll_.HExpandFactor();
    s.thumbX     = scroll_.HThumbRect().x;
    s.draggingX  = scroll_.IsHDragging();
    return s;
}

void TextArea::RefreshComposition(bool redrawText, bool overlayForce) {
    {
        char buf[192];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[TA::Refresh] %p redrawText=%d overlayForce=%d ctxModal=%d bounds=%.0fx%.0f\n",
                    (void*)this, redrawText ? 1 : 0, overlayForce ? 1 : 0,
                    Context().inModalResize ? 1 : 0, bounds_.w, bounds_.h);
        OutputDebugStringA(buf);
    }
    if (!CompositionActive()) return;
    if (!IsEffectivelyVisible()) {
        content_->SetTreeVisible(false);
        return;
    }
    content_->SetTreeVisible(true);
    // No document-wide layout build in either mode: ScrollExtentDip derives the Wrap
    // extent from the paragraph map, which is arithmetic. This line used to be
    // EnsureLayout(), and it was the reason a wrapped resize drag re-wrapped the whole
    // buffer on every frame — RefreshComposition runs on bounds changes.
    const float extent = ScrollExtentDip();
    // Visibility is handled by root membership above; opacity remains an independent
    // appearance property and is only pushed while the tree is visible.
    content_->SetOpacity(EffectiveOpacity());
    // Full bounds as the viewport, ancestor clip supplied SEPARATELY: inside a
    // scrolling container this control is arranged with bounds past the container's
    // viewport, and a D2D clip cannot reach a DComp visual. The host needs the clip
    // as its own operand to express it relative to this control's true position (see
    // ScrollContentHost::SetAncestorClip).
    RectDip ancestorClip;
    if (AncestorViewportClip(ancestorClip)) content_->SetAncestorClip(ancestorClip);
    else content_->ClearAncestorClip();
    // Defer surface rasterization while a resize border is held: BeginDraw is a GPU
    // sync point and dominated the worst measured resize frame. Geometry below still
    // goes through, so the compositor keeps showing correctly placed pixels.
    content_->SetModalResize(Context().inModalResize);
    content_->SetViewport(bounds_);
    content_->SetContentInset(TextInset());
    content_->SetContentHeight(extent);
    // The text surface is its own DComp visual, so the themed box Render() painted into
    // the window frame is NOT under these pixels — the compositor blends the surface
    // onto whatever the window shows there. Clearing to transparent therefore gave the
    // glyphs no base to sit on, and dark theme's controlFillDefault (white at alpha
    // 0.06) over a dark Mica window rendered light text as dark-on-dark: the code
    // blocks looked empty while still being selectable. Hand the host the same resolved
    // fill Render() uses so both paths put the glyphs on an identical base.
    content_->SetSurfaceClearColor(EffectiveBackground(Theme().colors.controlFillDefault));
    scroll_.SetBounds(bounds_);
    scroll_.SetContentHeight(extent);
    // Horizontal extent: warm the mark first (ScrollExtentXDip does this), then push.
    // Must come before EnsureContent so the scrollbar is sized correctly even on the
    // first frame after a text change.
    scroll_.SetContentWidth(ScrollExtentXDip());
    scroll_.SetOffset(content_->EffectiveOffset());

    content_->EnsureContent(
        [this](ID2D1DeviceContext* dc, float o, float h) { DrawTextToSurface(dc, o, h); },
        /*forceRedraw=*/redrawText);
    if (redrawText) SyncCaretVisual();

    const OverlaySignature sig = CurrentOverlaySig();
    if (overlayForce || sig.Differs(lastOverlaySig_)) {
        content_->RedrawOverlay([this](ID2D1DeviceContext* dc, float w, float h) {
            DrawOverlayToSurface(dc, w, h);
        });
        lastOverlaySig_ = sig;
    }
    if (WindowServices* win = Window())
        if (ICompositionBackend* comp = win->Composition()) comp->RequestCommit();
}

void TextArea::OnAttachedToTree() {
    TextEditBase::OnAttachedToTree();   // marks the layout dirty + invalidates
    lineHeightCache_ = 0.0f;            // DWrite is available now; re-probe
    // The scrollable extent needs a DWrite layout, and DWrite only arrives with the
    // context — so bounds set before attach measured nothing. Re-sync now rather than
    // relying on a later layout pass to happen to fix it up.
    SyncScroll();

    // scroll_ is an embedded model, not a tree node, so nothing else would ever give
    // it a UIContext — and UIElement::Theme() falls back to a LIGHT default snapshot
    // when context_.theme is null. The rail was therefore drawn with light-theme ink
    // (textPrimary = near-black) regardless of the real theme, which is invisible on
    // a dark background. Forwarding the context is what puts it on the actual theme,
    // and it keeps working across a theme switch because the host overwrites its
    // snapshot in place. Same fix as ScrollPanel and TreeView.
    scroll_.AttachToContext(Context());
    // Match ScrollPanel: a rail that never fully disappears while the content
    // overflows, so "there is more to see" stays discoverable instead of fading out.
    scroll_.SetKeepVisibleWhenOverflow(true);

    if (WindowServices* win = Window()) {
        if (ICompositionBackend* comp = win->Composition()) {
            content_ = std::make_unique<ScrollContentHost>();
            content_->SetTreeVisible(IsEffectivelyVisible());
            if (SUCCEEDED(content_->Create(comp, Context().dpiScale))) {
                RefreshComposition(true, true);
            } else {
                content_.reset();       // creation failed → UI-thread fallback
            }
        }
    }
    Invalidate();
}

void TextArea::OnDetachedFromTree() {
    if (content_) { content_->Destroy(); content_.reset(); }
    // Clear the scroll model's context — it is no longer valid.
    scroll_.DetachFromContext();
}

void TextArea::OnFocusChanged() {
    TextEditBase::OnFocusChanged();
    if (CompositionActive()) {
        SyncCaretVisual();                  // show/hide + (re)arm the blink
        RefreshComposition(true, true);     // selection highlight depends on focus
    }
    Invalidate();
}

void TextArea::OnThemeChanged() {
    if (CompositionActive()) RefreshComposition(true, true);  // recolor both surfaces
    Invalidate();
}

void TextArea::OnOpacityChanged(float) {
    // Composition path only: one visual property, no surface redraw. In fallback
    // mode the Render-dirty flag from the base class suffices (the host repaints
    // through a faded DrawingContext).
    if (CompositionActive()) {
        // Hidden hosts cache this value without committing and apply it on reveal.
        content_->SetOpacity(EffectiveOpacity());
    }
}

void TextArea::OnVisibilityChanged(bool visible) {
    UNREFERENCED_PARAMETER(visible);
    UpdateCompositionVisibility();
}

void TextArea::OnAncestorVisibilityChanged() {
    UpdateCompositionVisibility();
}

void TextArea::UpdateCompositionVisibility() {
    if (!CompositionActive()) return;
    if (!IsEffectivelyVisible()) {
        content_->SetTreeVisible(false);
        return;
    }
    content_->SetTreeVisible(true);
    RefreshComposition(true, true);
}

void TextArea::OnDpiChanged(float dpiScale) {
    lineHeightCache_ = 0.0f;
    if (CompositionActive()) {
        content_->SetDpiScale(dpiScale);
        RefreshComposition(true, true);
    }
    Invalidate();
}

void TextArea::OnDeviceLost() {
    if (content_) content_->OnDeviceLost();  // drop visuals, keep scroll position
}

void TextArea::OnDeviceRestored() {
    if (content_) {
        if (WindowServices* win = Window()) {
            if (ICompositionBackend* comp = win->Composition()) {
                content_->OnDeviceRestored(comp, Context().dpiScale);
                RefreshComposition(true, true);
            }
        }
    }
    Invalidate();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void TextArea::Render(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;
    D2D1_COLOR_F bg = EffectiveBackground(pal.controlFillDefault);

    D2D1_RECT_F box = D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom());
    const float corner = EffectiveCornerRadius();
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box, corner, corner);
    dc.FillRoundedRect(rr, bg);
    // Focused draws the accent ring at 1.5 DIP; a user BorderBrush/Thickness wins
    // in either state (an override that vanished on focus would look like a bug).
    dc.DrawRoundedRect(rr,
                       EffectiveBorderBrush(IsFocused() ? EffectiveAccentColor(pal.accent)
                                                        : pal.controlStrokeDefault),
                       EffectiveBorderThickness(IsFocused() ? 1.5f : 1.0f));

    // Composition mode: the box above stays in the WINDOW content frame — the
    // composition visuals are parented above it, so it reads as the background behind
    // the text. Everything that scrolls (text, selection, IME underline), the caret and
    // the scrollbar live on surfaces the compositor owns, and those are refilled
    // OUTSIDE this frame (a nested BeginDraw would fail here). So stop after the box.
    if (CompositionActive()) return;

    const float originX = bounds_.x + ContentLeft();
    const float originY = bounds_.y + ContentTop() - scroll_.Offset();

    // Clip to the text region (inside padding, left of the scroll gutter). The
    // guard is scoped to this block so the clip pops before the scrollbar draws.
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(bounds_.x + ContentLeft(), bounds_.y + ContentTop(),
                        bounds_.x + ContentLeft() + ContentWidth(),
                        bounds_.bottom() - ContentTop()));

        // Placeholder when empty and not composing. DisplayEmpty() decides this from the
        // buffer sizes without composing the string.
        if (DisplayEmpty() && !placeholder_.empty()) {
            if (IDWriteTextFormat* fmt = Dwrite() ? Dwrite()->Format(
                    EffectiveFontSize(), EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL),
                    DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                    DWRITE_WORD_WRAPPING_WRAP) : nullptr) {
                dc.DrawText(placeholder_.c_str(), static_cast<UINT32>(placeholder_.size()),
                            fmt, D2D1::RectF(originX, bounds_.y + ContentTop(),
                                             originX + ContentWidth(), bounds_.bottom()),
                            pal.textSecondary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        } else {
            // Both modes take the virtualized per-row path. The band is the visible text
            // region; there is no overscan on the UI-thread fallback, so the band is
            // exactly what is on screen.
            const float bandTop = scroll_.Offset();
            DrawingContext rc = dc;
            if (wrapMode_ == TextWrapMode::NoWrap) {
                DrawLinesToSurface(rc, bandTop, ContentHeight(), originX,
                                   bounds_.y + ContentTop());
            } else {
                DrawParagraphsToSurface(rc, bandTop, ContentHeight(), originX,
                                        bounds_.y + ContentTop());
            }

            // Caret. X is in content space, so subtract the horizontal offset to get
            // window-local X — the same shift the draw loops apply to every glyph. (Under
            // Wrap that offset is always 0, but going through the same expression keeps
            // the two modes from drifting apart.)
        }

        // Caret, OUTSIDE the placeholder/text branch: an empty TextArea that has a
        // placeholder takes the first branch, and while the caret lived inside the else
        // it drew nothing there -- clicking an empty field looked like it never focused.
        // Same defect as TextBox had, same cause, fixed the same way.
        //
        // X is in content space, so subtract the horizontal offset to get window-local X
        // -- the same shift the draw loops apply to every glyph. (Under Wrap that offset
        // is always 0, but going through the same expression keeps the two modes from
        // drifting apart.) bandTop is read here too; when the placeholder branch ran there
        // is no scroll, so it is 0 and the caret lands at the first line.
        if (IsFocused() && caretVisible_) {
            const float bandTopForCaret = scroll_.Offset();
            float x = 0, y = 0, h = 0;
            CaretMetrics(caret_ + static_cast<UINT32>(composition_.size()), x, y, h);
            const float cx = originX + x - CurrentOffsetX();
            const float cy = bounds_.y + ContentTop() + y - bandTopForCaret;
            dc.FillRect(D2D1::RectF(cx, cy, cx + kCaretW, cy + h), pal.textPrimary);
        }
    }

    scroll_.Render(dc);
}

} // namespace fluent
