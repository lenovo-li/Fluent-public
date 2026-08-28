// TextArea.h — multi-line, self-drawn Fluent text input.
//
// A multi-line editor built on TextEditBase (text model, selection, IME,
// clipboard, blink, editing keys). TextArea adds the multi-line specifics: a
// word-wrapping layout at the content width, vertical scrolling (reusing
// ScrollViewer for the thumb, like TreeView), up/down + PageUp/Down + line
// Home/End navigation, Enter inserting a newline, and 2D caret geometry.
//
// Fully DWrite-rendered, no child HWND. Where those pixels land depends on the mode:
// with a composition backend the text / selection / IME underline go on a scrolled
// overscan surface, the caret on its own visual and the scrollbar on an overlay (only
// the rounded box stays in the window content surface); with no backend everything is
// drawn in the window content surface and scrolled on the UI thread.
// Geometry is in DIPs; indices are UTF-16 code-unit offsets. Newlines in the
// buffer are normalized to '\n'.
#pragma once

#include "TextEditBase.h"
#include "../layout/ScrollViewer.h"
#include "../composition/ScrollContentHost.h"
#include "../composition/OverlaySignature.h"
#include "../text/LineIndex.h"
#include "../text/LineLayoutCache.h"
#include "../text/VisibleLines.h"
#include "../text/WrapExtentMap.h"
#include <memory>

namespace fluent {

// How text flows at the content width.
//
//   Wrap   (default) reflows each logical line to fill the available width, so one
//          logical line becomes N visual lines -- and N depends on the width, so
//          every N changes when the window resizes.
//   NoWrap leaves every logical line as exactly one visual line, scrolling
//          horizontally instead of reflowing.
//
// BOTH MODES ARE NOW VIRTUALIZED, but by different means, and the difference is worth
// understanding before touching either.
//
// NoWrap has three properties that make virtualization straightforward: the line count
// is the newline count (O(1) to know), the line height is constant (so a scroll offset
// maps arithmetically to a line number), and a resize changes no line breaks at all. It
// is the mode that reaches tens of MB, and the one a log view wants.
//
// Wrap has none of those. A paragraph's visual line count depends on where glyphs break
// at this exact width, so the document's height is unknowable without wrapping all of
// it, and every height changes when the window resizes. It is virtualized anyway, by
// ESTIMATING the height of paragraphs nobody has looked at and correcting each estimate
// when the paragraph is actually drawn (see text/WrapExtentMap.h). The visible cost is
// that the scrollbar shifts slightly while scrolling through never-measured regions —
// accepted deliberately, because the alternatives are a multi-second stall on open or a
// background thread that stalls on contact instead. VS Code and Sublime make the same
// trade.
//
// What this replaced: a single IDWriteTextLayout over the entire buffer, which had two
// hard ceilings (content past ~5000 wrapped lines was clipped away and invisible, and
// every resize frame re-wrapped the whole document). Both are gone. Neither mode now
// has a document-sized layout anywhere.
enum class TextWrapMode { Wrap, NoWrap };

class TextArea : public TextEditBase {
public:
    // Element overrides.
    void Render(const DrawingContext& dc) override;
    bool CaretRectDip(RectDip& out) const override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;
    // Composition mode runs the blink as a compositor opacity animation, so the
    // window's 530 ms blink timer (which repaints the WHOLE window each tick) is not
    // needed. The UI-thread fallback keeps using it.
    bool WantsBlink() const override { return !CompositionActive(); }
    void OnBlink() override;

protected:
    // TextEditBase hooks.
    void OnTextLayoutDirty() override;
    void OnCompositionDirty() override;
    // The selection moved but the text did not. In composited mode Render() returns
    // after the box, so the highlight lives on the content surface and needs an explicit
    // re-rasterize — an Invalidate() alone would repaint the window frame and leave the
    // selection looking unchanged until something else refilled the surface.
    //
    // Text is unchanged, so the line index and every cached line layout stay valid; this
    // costs one surface redraw of the visible band, not a relayout.
    void OnSelectionChanged() override;
    void EnsureCaretVisible() override;
    bool OnNavigationKey(UINT vk, bool shift) override;
    std::wstring SanitizeInput(std::wstring s) const override;
    // Accept Enter/newline (base rejects everything < space); SanitizeInput
    // normalizes it to '\n'. Tab is still excluded so it drives focus.
    bool AcceptsChar(wchar_t ch) const override {
        return ch == L'\r' || ch == L'\n' || (ch >= 0x20 && ch != 0x7F);
    }

    // Element input.
    void OnBoundsChanged() override;
    void OnPointerLeft() override;
    void UpdateContextModalResize(bool inModalResize) override;

    // Compositor lifecycle (mirrors TreeView): build the host on attach, tear it down
    // on detach, and keep the surfaces current across focus / theme / DPI / device loss.
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnFocusChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnDeviceLost() override;
    void OnDeviceRestored() override;
    // Composited mode draws nothing in Render(), so opacity is pushed onto the
    // composition sub-tree rather than folded into the host's DrawingContext.
    void OnOpacityChanged(float opacity) override;

    // Same reason as OnOpacityChanged, for the collapsed state: skipping Render
    // cannot hide a DComp visual, so hiding has to reach the compositor. Without
    // this, a collapsed TextArea keeps its text on screen over whatever replaced it.
    void OnVisibilityChanged(bool visible) override;
    void OnAncestorVisibilityChanged() override;

public:
    // Routed pointer input: scrollbar drag + click-to-place + drag-select (both
    // captured so the pointer can leave the control mid-gesture).
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

    // --- Wrap mode -----------------------------------------------------------
    // Switching modes discards the cached layout and rebuilds the line index: the two
    // paths share no cached state, and a stale layout from the other mode would draw
    // with the wrong line breaks. Routed through SetProperty so a redundant set costs
    // nothing and does not schedule a frame. Measure-level dirty, because the wrap
    // width stops constraining the desired size.
    void SetWrapMode(TextWrapMode mode);
    TextWrapMode WrapMode() const { return wrapMode_; }

    // Has a layout pass given this control a width wide enough for ContentWidth() to be
    // meaningful? While false the text layout is deliberately NOT built: wrapping would
    // happen at ContentWidth()'s 1 DIP clamp floor, which puts one visual line per
    // character — the most expensive wrap that exists, and discarded the moment real
    // bounds arrive. Public because it is also the honest answer to "does this control
    // know its own text extent yet", which a caller sizing around it may need, and
    // because it is the one observable that distinguishes deferral from a wasted pass.
    bool LayoutWidthKnown() const;
    // The height of the text content (DIPs), or 0 before LayoutWidthKnown().
    //
    // Under Wrap this is now the ESTIMATED-AND-CORRECTED extent from WrapExtentMap, not
    // a measurement of one whole-document layout: exact for paragraphs that have been
    // drawn, estimated for the rest, and therefore subject to small adjustments as
    // scrolling reaches new regions. That is the documented trade of wrapped
    // virtualization, not an inaccuracy to be fixed.
    float MeasuredContentHeightDip() const;
    // Test / diagnostic surface:
    // Line count as seen by the NoWrap index, or 0 when in Wrap mode.
    size_t NoWrapLineCount() const {
        if (wrapMode_ != TextWrapMode::NoWrap) return 0;
        EnsureLineIndex();
        return lines_.LineCount();
    }

    // --- Wrapped virtualization surface (§3) ---------------------------------
    // How many paragraphs have had their real wrapped height measured, versus how many
    // exist. Exposed because "cost is O(visible), not O(document)" is a claim about work
    // NOT done, and nothing in the rendered output can show it: a version that measured
    // the whole document on open would look identical and merely take seconds. This is
    // the counter that makes the difference observable, in the same spirit as
    // LineIndexRebuildCount.
    // Forces the pending rebuild before answering, like every other query here. Without
    // that this can report measurements from BEFORE an invalidation: the map is rebuilt
    // lazily, and an edit or a mode switch only sets the dirty flag — so a caller asking
    // right after one would be told about paragraphs that no longer exist. Every other
    // accessor on this path (MeasuredContentHeightDip, WrapParagraphSpanForBand) already
    // goes through EnsureWrapExtent, and a counter that does not is the one observable
    // that can disagree with the geometry it is supposed to describe.
    size_t WrapMeasuredParagraphs() const {
        if (wrapMode_ != TextWrapMode::Wrap) return wrapExtent_.MeasuredCount();
        EnsureWrapExtent();
        return wrapExtent_.MeasuredCount();
    }
    size_t WrapParagraphCount() const {
        if (wrapMode_ != TextWrapMode::Wrap) return 0;
        EnsureWrapExtent();
        return wrapExtent_.ParagraphCount();
    }
    // The paragraphs the Wrap draw loop would visit for a band of content space. THIS IS
    // THE ACTUAL BOUND the loop uses, exposed for the same reason VisibleLineSpanForBand
    // is: headless the draw callback gets a null DC and returns before the loop, so a
    // test that drives a draw never reaches the decision. Empty in NoWrap mode.
    LineSpan WrapParagraphSpanForBand(float bandTopDip, float bandHeightDip) const;

    // Measure every paragraph intersecting the band, replacing estimates with real
    // wrapped heights, and return how many were visited.
    //
    // THE DRAW LOOP CALLS THIS — it is not a parallel implementation for tests to poke at,
    // which is the distinction that matters. Headless, DrawParagraphsToSurface returns at
    // its null-DC guard before reaching any of its own logic, so a test that drives a draw
    // exercises nothing; a test that reimplemented the walk would assert about its own
    // copy rather than about the shipping loop. Extracting the walk and having both the
    // loop and the test enter through here is what makes "drawing a paragraph measures it"
    // and "the walk is bounded by the band" checkable at all.
    //
    // This mattered concretely: deleting the SetMeasured call inside the measure step left
    // all 806 tests green, because every Wrap assertion was satisfied by estimates alone.
    //
    // Also the loop's forward bound. The walk cannot trust the span computed up front: a
    // paragraph estimated at 2 visual lines that measures 9 pushes everything below it
    // down, so the band covers fewer paragraphs than the first estimate suggested. Both
    // the loop and this method therefore re-read each paragraph's top as they advance.
    // Returns 0 in NoWrap mode, and before DWrite or a real width.
    size_t MeasureParagraphsInBand(float bandTopDip, float bandHeightDip) const;
    // Content-space Y of a paragraph's top (DIPs), Wrap mode only. 0 in NoWrap.
    float WrapParagraphTopDip(size_t paragraph) const;
    // Visual lines a paragraph occupies — measured if it has been drawn, estimated
    // otherwise. Pair with WrapParagraphIsMeasured to tell which.
    uint32_t WrapParagraphVisualLines(size_t paragraph) const {
        return wrapExtent_.VisualLinesAt(paragraph);
    }
    bool WrapParagraphIsMeasured(size_t paragraph) const {
        return wrapExtent_.IsMeasured(paragraph);
    }
    // The line height in DIPs (font metric, DPI-independent). Always > 0.
    float LineHeightDip() const { return LineHeight(); }

    // --- High-throughput log append (§2) -------------------------------------
    // Append to the end of the buffer without touching anything that describes the
    // text already there. This is the whole log path: a hardware source producing
    // thousands of lines a second calls this (via LogSink, off-thread), and the cost
    // per call is O(added characters) plus O(visible lines) of drawing — never
    // O(document), at any document size.
    //
    // WHAT IT DELIBERATELY DOES NOT DO, because each of these is what makes SetText
    // O(document) and each would put that cost back:
    //   * no LineIndex::Rebuild — LineIndex::Append scans only the new characters and
    //     leaves every existing line's number intact;
    //   * no layout-cache flush — line numbers are unchanged, so every cached layout is
    //     still valid except the one line whose text grew (the old last line), which is
    //     invalidated individually via LineLayoutCache::Erase;
    //   * no caret / selection move — a log reader scrolled back through history, or
    //     mid-selection, must not have the caret yanked to the end under them;
    //   * no horizontal high-water-mark reset — an append can only lengthen a line or
    //     introduce a longer one, so the mark is still a valid lower bound. Resetting it
    //     (which OnTextLayoutDirty correctly does for an edit) would shrink the
    //     horizontal scroll range on every batch and jerk the thumb backwards.
    //   * no synchronous paint — it requests a frame like every other mutation, so a
    //     burst of appends between two frames coalesces into one redraw. Frame rate is
    //     decoupled from data rate by the existing FrameScheduler; this path only has to
    //     avoid defeating it.
    //
    // TextChanged fires only if something is subscribed (the event payload is the whole
    // buffer by value, which at log sizes is the single most expensive thing this method
    // could do). Subscribers on a high-rate log are therefore paying for a full copy per
    // batch — deliberate and visible, rather than silently skipped.
    //
    // Newlines are normalized to '\n' through the same SanitizeInput every other input
    // path uses. Empty input is a no-op that schedules no frame.
    void AppendText(std::wstring_view s);

    // Cap the buffer at `n` lines, discarding whole lines from the FRONT once it is
    // exceeded (the classic log ring buffer). 0 (default) means no cap.
    //
    // Applied on every AppendText and immediately when the cap itself is lowered, so
    // memory settles at a bound the caller chose instead of growing with uptime.
    //
    // The trim is not free and cannot be: dropping the first k lines renumbers every
    // remaining line, so the layout cache must be flushed (it is keyed by line number —
    // see LineLayoutCache's header for why that key is what makes append cheap in the
    // first place). That is the deliberate asymmetry of this design: appends are cheap
    // and frequent, trims are O(visible) and occur once per `n` lines appended.
    //
    // Under NoWrap the scroll offset is compensated exactly (k lines dropped == k *
    // lineHeight of content gone), so a user reading history sees the text hold still.
    // Under Wrap no exact compensation exists — a logical line is an unknown number of
    // visual lines — so the offset is merely re-clamped and the view can jump. That is
    // Wrap's pre-existing shape, not something this adds; NoWrap is the log mode.
    void SetMaxLines(size_t n);
    size_t MaxLines() const { return maxLines_; }

    // --- Tail following (§2.4) ------------------------------------------------
    // When enabled and the view is at the bottom, an append scrolls to the new bottom
    // (classic `tail -f`). Scrolling up detaches; scrolling back to the bottom
    // re-attaches. Default off, so no existing TextArea changes behaviour.
    void SetAutoScrollToTail(bool enable);
    bool AutoScrollToTail() const { return autoScrollEnabled_; }
    // Whether the view is CURRENTLY following. Distinct from AutoScrollToTail(), which
    // is only whether the feature is armed: this is the live state that scrolling up
    // clears and scrolling back down restores.
    bool FollowingTail() const { return followingTail_; }

    // Scroll immediately to the bottom and mark the view as following. The
    // complement to SetAutoScrollToTail: call this when the USER explicitly asks
    // to jump (e.g., a "Jump to tail" button), because SetAutoScrollToTail(true)
    // intentionally does NOT force-jump when the view is in the middle of history
    // (the library cannot tell an explicit-click from an initialization call).
    void JumpToTail();

    // Is `offset` close enough to `maxOffset` to count as "at the bottom"?
    //
    // A pure function, and public, for the reason the repo extracts PlanRedraw: the
    // decision is a state-machine transition that must be checkable without a window, a
    // compositor or a scroll animation in flight. The tolerance is half a line height —
    // float error in the offset arithmetic and a few pixels of overshoot must not make
    // the follow state chatter on and off while data streams in, which would show up as
    // the view intermittently refusing to follow.
    static bool AtTailOffset(float offset, float maxOffset, float lineHeightDip) {
        if (maxOffset <= 0.0f) return true;   // nothing to scroll: trivially at the end
        const float tol = lineHeightDip > 0.0f ? lineHeightDip * 0.5f : 1.0f;
        return offset >= maxOffset - tol;
    }

    // --- Horizontal scrolling (NoWrap only) ----------------------------------
    // The horizontal scroll position currently shown (DIPs). Zero under Wrap, where
    // there is nothing to scroll to by construction.
    float CurrentOffsetX() const;
    // The vertical scroll position currently shown (DIPs) — the compositor's
    // interpolated value in composited mode, scroll_'s in the fallback. Public because
    // the tail-follow state machine is only observable through it: "an append scrolled
    // to the new bottom" is a statement about this number.
    float CurrentOffsetDip() const { return CurrentOffset(); }
    // The vertical scrollable range (DIPs): extent minus one viewport, floored at 0.
    // Test surface for the same reason as above — AtTailOffset's second argument.
    float MaxOffsetDip() const;
    // The horizontal scrollable extent (DIPs): the widest line MEASURED SO FAR plus
    // the gutters, or 0 under Wrap / before a layout width is known.
    //
    // "Measured so far" is the deliberate trade this mode makes, and it is worth
    // stating where a caller will see it: measuring every line to find the true widest
    // one is O(document), which is precisely the cost NoWrap exists to remove. So the
    // extent is a high-water mark over the lines that have actually been laid out, and
    // it GROWS as scrolling reaches wider lines — the scrollbar therefore lengthens
    // slightly while scrolling through a document with uneven line lengths. VS Code
    // behaves the same way for the same reason. Growth is one-directional within a
    // document version, because a range that shrank mid-scroll would jerk the thumb
    // backwards, which is far more disturbing than one that quietly extends.
    float ScrollExtentXDip() const;
    // The widest line width measured so far (DIPs), without the gutters. Test surface:
    // it makes the high-water-mark behaviour above directly checkable.
    float MaxSeenLineWidthDip() const { return maxSeenLineWidth_; }
    // The number of characters the cached layout for `lineNumber` covers. Returns 0 if
    // the line is not cached. Public because it is the ONLY observable that proves the
    // prefix clip actually bounded the layout: the draw loop is unreachable headless
    // (null DC), so without this a regression that built the full 50 KB layout every
    // frame would leave the suite green — the exact failure mode §1.5b-3 guards.
    size_t LineLayoutCoverEnd(size_t lineNumber) const {
        return lineLayouts_.GetCoverEnd(lineNumber);
    }

    // How many characters of line `i` the draw loop would ask DWrite to lay out.
    // THIS IS THE ACTUAL BOUND the loop passes as minCover, exposed for the same
    // reason VisibleLineSpanForBand is: headless, DrawLinesToSurface returns at its
    // null-DC guard, so a test that drives a draw never reaches the decision. Without
    // an observable here, a regression that dropped the clip and laid out all 50 KB of
    // a log line every frame would leave the suite green.
    //
    // Returns the full line length in Wrap mode and for lines under the clip threshold
    // — those genuinely are laid out whole, and saying so keeps the "did the clip
    // apply" question answerable rather than merely plausible.
    size_t DrawMinCoverForLine(size_t lineNumber) const;

    // The lines the NoWrap draw loop would visit for a band of content space. THIS IS
    // THE ACTUAL BOUND the loop uses, exposed so the "cost is O(visible lines)" claim
    // is checkable: headless, DrawTextToSurface returns at its null-DC guard, so a
    // test that drives a draw never reaches the loop. Empty span in Wrap mode, where
    // the question has no answer without laying the document out.
    LineSpan VisibleLineSpanForBand(float bandTopDip, float bandHeightDip) const;

    // Diagnostic counters for the two O(document) operations on the NoWrap path.
    // Exposed because "this does not happen while composing" is a claim about work NOT
    // done, and work not done is invisible unless something counts it. A test that only
    // checked the rendered result would pass whether or not the buffer was rescanned on
    // every keystroke — which is precisely the bug that shipped here: typing Chinese
    // into a 28 MB document stalled for seconds while every test stayed green.
    unsigned LineIndexRebuildCount() const { return lineIndexRebuilds_; }
    unsigned LineCacheClearCount() const { return lineCacheClears_; }
    // The characters the index says line `i` holds. Diagnostic: it makes "the index
    // describes text_" directly checkable, which matters because the index is built
    // over text_ while an IME composition may make the DISPLAYED string longer. Build
    // it over the wrong one and every offset after the caret is shifted — the line
    // count still looks right, so nothing else would reveal it.
    std::wstring NoWrapLineText(size_t i) const {
        if (wrapMode_ != TextWrapMode::NoWrap) return {};
        EnsureLineIndex();
        if (i >= lines_.LineCount()) return {};
        const std::wstring_view v = LineSliceOfText(i);
        return std::wstring(v);
    }

private:
    // DWrite word-wrapping value matching the current mode. Used by EnsureLayout
    // so the layout is always consistent with WrapMode().
    DWRITE_WORD_WRAPPING WordWrapping() const {
        return (wrapMode_ == TextWrapMode::NoWrap)
            ? DWRITE_WORD_WRAPPING_NO_WRAP
            : DWRITE_WORD_WRAPPING_WRAP;
    }

    // Rebuild the line index if dirty. O(n) scan, but paid at most once per text
    // change (the dirty flag is set by OnTextLayoutDirty and cleared here).
    //
    // Runs in BOTH modes now: Wrap needs paragraph boundaries just as much as NoWrap
    // needs line boundaries — the difference is only that a Wrap paragraph occupies
    // several visual lines. (Before wrapped virtualization this returned early under
    // Wrap, because nothing there used the index.)
    void EnsureLineIndex() const;

    // Rebuild the wrapped extent map if dirty (Wrap only). O(paragraphs) of pure
    // arithmetic — no DWrite, which is what makes it affordable on every resize frame.
    void EnsureWrapExtent() const;
    void InvalidateLayout() { layoutDirty_ = true; }

    // The characters-per-line figure to seed estimates with at the current width, before
    // any paragraph has been measured. Deliberately crude: it only has to be the right
    // order of magnitude, because the first drawn frame replaces the estimates that
    // matter (the visible ones) with measurements and feeds the rolling average.
    float SeedCharsPerLine() const;
    // The layout box paragraphs are wrapped into under Wrap.
    float WrapLayoutWidth() const { return ContentWidth(); }
    // Ask DWrite how tall paragraph `p` really is and record it. Returns the visual line
    // count, or 0 if it could not be measured (no DWrite / no width yet).
    uint32_t MeasureWrapParagraph(size_t p) const;
    // Draw the paragraphs intersecting the band, Wrap mode. The counterpart of
    // DrawLinesToSurface, and structurally the same loop — the only difference is that a
    // row's height comes from the extent map rather than being constant.
    void DrawParagraphsToSurface(DrawingContext& rc, float surfaceOriginDip,
                                 float surfaceHeightDip, float originX = 0.0f,
                                 float originY = 0.0f);
    // Caret / hit-test under Wrap, resolved through one paragraph's layout rather than a
    // whole-document one.
    void WrapCaretMetrics(UINT32 index, float& x, float& y, float& height) const;
    UINT32 WrapHitIndex(float localX, float localY) const;
    // The layout for one paragraph, wrapped at the content width, from the cache. Null
    // before DWrite or a real width. Positions inside it are paragraph-local: add
    // WrapParagraphTopDip(p) for content space.
    IDWriteTextLayout* WrapParagraphLayout(size_t p) const;

    // Caret geometry (DIP, relative to the text origin, pre-scroll).
    void CaretMetrics(UINT32 index, float& x, float& y, float& height) const;
    UINT32 HitIndex(float dipX, float dipY) const;  // index nearest a point
    // TextEditBase hook for the context menu; both axes matter here.
    UINT32 IndexAtPoint(float dipX, float dipY) const override {
        return HitIndex(dipX, dipY);
    }
    // Vertical navigation: the index one line (or one page) up/down from the
    // caret, keeping roughly the same X. `LineEdgeIndex` returns the start/end
    // of the caret's current visual line.
    UINT32 IndexByLineStep(int deltaLines) const;
    UINT32 IndexByPageStep(int deltaPages) const;
    UINT32 LineEdgeIndex(UINT32 from, bool toEnd) const;
    int VisibleLineCount() const;                    // lines that fit in the view
    float LineHeight() const;                        // one line's height (DIP)
    float ContentLeft() const;                       // text origin X (padding)
    float ContentTop() const;                        // text origin Y (padding)
    float ContentWidth() const;                      // wrap width
    float ContentHeight() const;                     // visible text height
    // The scrollable extent (DIP): measured text height + vertical padding, or 0 while
    // no layout width is known (nothing is scrollable before the first Arrange).
    float ScrollExtentDip() const;
    void SyncScroll();                               // push content extents to scroll_
    // Fold a line layout's measured width into the horizontal high-water mark. Called
    // wherever a line layout is obtained, so the mark covers exactly the lines that
    // have been laid out — see ScrollExtentXDip for why that is the bound.
    void NoteLineWidth(IDWriteTextLayout* layout) const;
    // Drop leading lines until at most maxLines_ remain. Returns the number of lines
    // discarded (0 when under the cap or uncapped), so the caller can compensate the
    // scroll offset by exactly the height that disappeared.
    size_t TrimToMaxLines();
    // Scroll to the bottom of the current extent and mark the view as following.
    void ScrollToTail();
    // Recompute followingTail_ from where the view actually is. Called from the
    // USER-driven scroll paths only — never from AppendText. An append grows the extent,
    // so recomputing there would see "offset < new max", conclude the user had scrolled
    // up, and switch following off on the first batch of data (see the progress notes).
    void UpdateTailFollow();

    // Move the horizontal offset by a delta and re-rasterize. Separate from the
    // vertical helpers because the horizontal offset is BAKED INTO THE PIXELS (see
    // DrawLinesToSurface): there is no compositor OffsetX to slide, so a horizontal
    // move always costs one content redraw. That is the same order as one keystroke,
    // and it is why the design carries no horizontal overscan.
    void ScrollHorizontallyBy(float deltaDip);

    // --- Compositor scrolling (Phase 4) ----------------------------------------
    // Active only when the window offers a composition backend; otherwise content_ is
    // null and every path below falls back to the UI-thread scroll_.
    bool CompositionActive() const { return content_ && content_->Valid(); }
    // The scroll offset currently SHOWN (DIPs) — mid-tween this is the compositor's
    // interpolated value, so hit-test / caret / IME agree with what is on screen.
    float CurrentOffset() const;
    // Reconcile the compositor surfaces with the current state and commit. Safe only
    // OUTSIDE the window content frame (attach / bounds / theme / dpi / input / tick).
    //   redrawText: re-rasterize the text surface (text / selection / IME / theme
    //     changed its pixels). false = only refill when scrolling near a buffer edge.
    //   overlayForce: re-rasterize the scrollbar + frame overlay unconditionally.
    //     false = redraw only if its OverlaySignature changed, which is what stops the
    //     scrollbar's idle-hide countdown from re-rasterizing a static bar every frame.
    void RefreshComposition(bool redrawText, bool overlayForce);
    void UpdateCompositionVisibility();
    OverlaySignature CurrentOverlaySig() const;
    // The padded text region as a host inset (text scrolls inside it; the scrollbar
    // and frame paint over the full bounds).
    ScrollContentHost::Inset TextInset() const;
    // Draw the text / selection / IME underline spanning [surfaceOriginDip, +height)
    // into the content surface. Caret is NOT drawn here — it is its own visual.
    void DrawTextToSurface(ID2D1DeviceContext* dc, float surfaceOriginDip,
                           float surfaceHeightDip);
    // --- NoWrap virtualized drawing ------------------------------------------
    // The NoWrap counterpart of DrawTextToSurface: draws only the lines that
    // intersect [surfaceOriginDip, +surfaceHeightDip), which is what makes the cost
    // O(visible lines) rather than O(document). Structurally identical to
    // TreeView::DrawRowsToSurface, and for the same reason — a constant line height
    // makes "which lines are in this band" arithmetic instead of a layout query.
    // `originX` / `originY` place the band's top-left in whatever space `rc` draws
    // in. The composition path passes (0,0) because the host's transform already put
    // the origin at the text region; the UI-thread path passes window coordinates.
    // Parameterizing the origin rather than pushing a transform keeps every draw call
    // going through DrawingContext's typed methods, which is what makes element
    // opacity apply here without this code knowing about it.
    void DrawLinesToSurface(DrawingContext& rc, float surfaceOriginDip,
                            float surfaceHeightDip, float originX = 0.0f,
                            float originY = 0.0f);
    // The layout for one logical line, from the cache (built on miss). Null before
    // DWrite arrives. The returned layout covers ONLY that line's characters, so
    // positions inside it are line-local: add LineTopDip(i) to get content space.
    // `minCover` is the minimum character count the layout must span; pass kFullLine
    // (default) for a complete layout, or a smaller value in draw paths that only
    // need the visible prefix (§1.5b-3).
    IDWriteTextLayout* LineLayout(size_t lineNumber,
                                  size_t minCover = LineLayoutCache::kFullLine) const;
    // A line's characters as a slice of text_ — no copy, at any document size.
    std::wstring_view LineSliceOfText(size_t lineNumber) const;
    // Content-space Y of line i's top. Constant line height is the whole premise of
    // NoWrap virtualization; this is where that premise is spent.
    float LineTopDip(size_t lineNumber) const { return lineNumber * LineHeight(); }
    // Text of line i (a view into text_ / the IME scratch buffer). The view is only
    // valid until the next mutation, matching DisplayText's contract.
    std::wstring_view LineTextView(const std::wstring& display, size_t lineNumber) const;
    // The (line, x-within-line) decomposition of a character offset, and the inverse.
    // Both go through LineIndex, so neither needs a whole-document layout.
    void NoWrapCaretMetrics(UINT32 index, float& x, float& y, float& height) const;
    UINT32 NoWrapHitIndex(float localX, float localY) const;

    // Draw the box frame + scrollbar into the overlay surface (full bounds).
    void DrawOverlayToSurface(ID2D1DeviceContext* dc, float viewportWDip,
                              float viewportHDip);
    // Push the caret's rect (content space) + blink state to the compositor.
    void SyncCaretVisual();
    // Scroll while drag-selecting past the top/bottom edge of the text region.
    void AutoScrollForDragSelect(float dipY);

    // --- NoWrap state --------------------------------------------------------
    // Both are meaningless in Wrap mode and deliberately left untouched there, so the
    // default path carries no extra cost: the index is not built, and no code outside
    // the NoWrap branches reads it.
    TextWrapMode wrapMode_ = TextWrapMode::Wrap;
    // Logical-line offsets over text_. Rebuilt when the text changes; under NoWrap it
    // is what makes "which line is at this scroll offset" an O(1) question.
    mutable LineIndex lines_;
    // lines_ is rebuilt lazily (EnsureLineIndex) rather than on every mutation, so a
    // burst of keystrokes does not pay for one O(n) scan each. Wrap mode never clears
    // this flag, because nothing in that mode reads the index.
    mutable bool lineIndexDirty_ = true;
    mutable unsigned lineIndexRebuilds_ = 0;  // O(document) scans actually performed
    mutable unsigned lineCacheClears_ = 0;    // full layout-cache flushes performed
    // Widest line MEASURED so far (DIP), the horizontal extent's high-water mark. See
    // ScrollExtentXDip for why this is a running maximum rather than the true widest
    // line. Mutable because it is filled in from const draw / metrics paths, which is
    // the only place a line's width becomes known.
    mutable float maxSeenLineWidth_ = 0.0f;
    // Per-line layouts under NoWrap. Keyed by line number, which survives an append
    // untouched — see LineLayoutCache's header for why that property matters. Cleared
    // from OnTextLayoutDirty, because any edit changes what the cached lines contain.
    mutable LineLayoutCache lineLayouts_;
    // Scratch for the caret line while an IME composition is active. That one line is
    // rebuilt every keystroke and deliberately NOT cached (its content changes each
    // time), so it needs somewhere to live that is not a per-call temporary — one line
    // of text, reused, instead of a fresh allocation per frame.
    mutable std::wstring compositionLine_;
    mutable ComPtr<IDWriteTextLayout> compositionLayout_;
    // What compositionLayout_ was last built from, so a repeated request for the same
    // composed paragraph returns the SAME live object instead of rebuilding.
    //
    // WHY THIS IS A CORRECTNESS FIX, NOT A CACHE. Rebuilding released the previous
    // layout while a caller still held the raw pointer: WrapCaretMetrics obtains the
    // layout, then calls MeasureWrapParagraph, which re-enters WrapParagraphLayout for
    // the same paragraph — dropping the last reference — and the subsequent
    // HitTestTextPosition dereferenced freed memory. That was the reported crash when
    // typing Chinese in Wrap mode. Keying on (paragraph, wrap width, composed text)
    // makes the re-entrant call a no-op that hands back the identical pointer.
    mutable size_t compositionLayoutPara_ = static_cast<size_t>(-1);
    mutable float compositionLayoutWidth_ = -1.0f;
    mutable std::wstring compositionLayoutText_;

    // --- Log state (§2) ------------------------------------------------------
    // Line cap for the ring buffer; 0 = uncapped. See SetMaxLines.
    size_t maxLines_ = 0;
    // Is tail following armed at all (the caller's setting)?
    bool autoScrollEnabled_ = false;
    // Is it following RIGHT NOW? Cleared by scrolling up, restored by scrolling back to
    // the bottom. Starts true so that enabling the feature on an already-scrolled-to-top
    // empty control still follows the first data that arrives — the alternative (start
    // detached until the user scrolls to the bottom once) makes `tail -f` behaviour need
    // a manual gesture before it works, which nobody expects.
    bool followingTail_ = true;

    // --- Wrap virtualization state (§3) --------------------------------------
    // Visual-line count per paragraph: measured where a paragraph has been drawn,
    // estimated elsewhere. This is what replaced the whole-document layout's measured
    // height as the source of the scroll extent.
    mutable WrapExtentMap wrapExtent_;
    // Rebuilt lazily, like lines_. Set by OnTextLayoutDirty and by a width change.
    mutable bool wrapExtentDirty_ = true;
    // The wrap width the extent map was last built for. A change means every measured
    // height is stale (a paragraph breaks differently at a new width), so the map is
    // reset rather than patched — the one case where measurements are legitimately
    // discarded.
    mutable float wrapExtentWidth_ = 0.0f;

    ScrollViewer scroll_;
    std::unique_ptr<ScrollContentHost> content_;  // null => UI-thread fallback
    OverlaySignature lastOverlaySig_;             // last-rasterized overlay state
    mutable bool layoutDirty_ = true;
    // LineHeight() builds a probe layout, and the tick path calls it often; memoize
    // per (fontSize, dpiScale).
    mutable float lineHeightCache_ = 0.0f;
    mutable float lineHeightForFont_ = 0.0f;
    // Drag-select auto-scroll: Windows stops sending WM_MOUSEMOVE once the pointer
    // holds still, so the scroll has to be driven from the animation tick using the
    // last known pointer Y (otherwise holding the mouse outside the edge stalls).
    float dragPointerY_ = 0.0f;
    bool dragOutsideEdge_ = false;
};

} // namespace fluent
