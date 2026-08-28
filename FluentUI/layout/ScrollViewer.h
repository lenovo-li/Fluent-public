// ScrollViewer.h — Minimal Fluent scroll container state + scrollbar renderer.
#pragma once

#include "../core/Control.h"

namespace fluent {

class ScrollViewer : public Control {
public:
    void SetContentHeight(float heightDip);
    // Set the offset immediately (no animation). Cancels any in-flight smooth
    // scroll and snaps the animation target to the new offset. Use for drag,
    // keyboard navigation, and EnsureVisible where the move should be instant.
    void SetOffset(float offsetDip);
    void ScrollBy(float deltaDip);
    float Offset() const { return offset_; }
    float MaxOffset() const;
    RectDip ThumbRect() const;
    bool HitThumb(float dipX, float dipY) const;
    void BeginDrag(float dipY);
    void DragTo(float dipY);
    // End a drag; resume hover-driven expand and the idle fade countdown.
    void EndDrag() { dragging_ = false; expandTarget_ = barHover_ ? 1.0f : 0.0f; idleSec_ = 0.0f; }
    bool IsDragging() const { return dragging_; }

    // --- Horizontal axis (NoWrap text; list controls leave it at zero) -------
    // A second, independent axis rather than a second ScrollViewer instance, because
    // the two bars must share the fade state: an editor that scrolls both ways would
    // otherwise show one rail fading while the other stays lit, and the shared idle
    // countdown is what makes them appear and disappear together. Only the EXPAND
    // (thin rail -> pill) is per-axis, since that follows which rail the pointer is
    // actually near.
    //
    // Everything here is inert until SetContentWidth reports content wider than the
    // bounds, so TreeView and a wrapping TextArea are unaffected: MaxOffsetX() is 0,
    // HThumbRect() is empty, and Render draws nothing horizontal.
    void SetContentWidth(float widthDip);
    void SetOffsetX(float offsetDip);
    float OffsetX() const { return offsetX_; }
    float MaxOffsetX() const;
    bool HorizontallyScrollable() const { return MaxOffsetX() > 0.0f; }
    RectDip HThumbRect() const;
    bool HitHThumb(float dipX, float dipY) const;
    bool HitHBarRegion(float dipX, float dipY) const;
    void SetHBarHover(bool over);
    void BeginHDrag(float dipX);
    void HDragTo(float dipX);
    void EndHDrag() { draggingX_ = false; expandXTarget_ = barHoverX_ ? 1.0f : 0.0f; idleSec_ = 0.0f; }
    bool IsHDragging() const { return draggingX_; }
    // Horizontal expand factor, for a composited overlay's change signature.
    float HExpandFactor() const { return expandX_; }

    // --- Smooth (animated) scrolling --------------------------------------
    // Animate toward an absolute offset / by a delta. Successive AnimateBy calls
    // accumulate against the pending target, so spinning the wheel keeps building
    // momentum instead of restarting from the current position. The owning
    // element drives Tick() each frame while NeedsTick() is true.
    void AnimateTo(float offsetDip);
    void AnimateBy(float deltaDip) { AnimateTo(target_ + deltaDip); }
    bool IsAnimating() const { return animating_; }
    // The smooth scroll's destination; == Offset() when settled. Mirrors
    // ScrollContentHost::TargetOffset() so a control can ask the same question of
    // whichever path is active. Needed by any decision about where a scroll is HEADED
    // rather than where it is: on the frame a wheel notch arrives, Offset() has not moved
    // yet, so judging "did the user scroll away from the bottom" against it always
    // answers "no".
    float TargetOffset() const { return animating_ ? target_ : offset_; }
    // Advance offset + fade/expand animations by dt seconds. Returns true while
    // anything is still in motion (or the idle fade countdown is running).
    bool Tick(float dtSec);
    // True while the bar needs per-frame ticks: an in-flight scroll, an unfinished
    // fade/expand, or a visible bar still counting down to auto-hide.
    bool NeedsTick() const;

    // --- Auto-fade / hover-expand (WinUI-11 style) ------------------------
    // Mark scroll activity: fade the bar in and restart the idle countdown.
    void Wake();
    // Pointer over the bar's hover strip near the right edge: fade in + expand to
    // a pill. Leaving shrinks back to a thin rail and starts the idle fade.
    void SetBarHover(bool over);
    // Wider hit strip near the right edge (for discoverability when thin).
    bool HitBarRegion(float dipX, float dipY) const;

    void Render(const DrawingContext& dc) override;

    // Current scrollbar visual state (0..1), for a compositor host that caches the
    // overlay surface and only re-rasterizes when these actually change (avoids
    // redrawing a static bar every frame during the idle-hide countdown).
    float Visibility() const { return visibility_; }
    float ExpandFactor() const { return expand_; }

    // When true and content overflows the viewport, the scrollbar never fully
    // fades out — it settles at a minimum visibility so the user can always see
    // that scrolling is available. When false (default), the bar hides completely
    // after the idle countdown, matching the classic overlay-scrollbar behavior.
    void SetKeepVisibleWhenOverflow(bool on) { keepVisibleWhenOverflow_ = on; }

    // Tune fade timing: how long the bar stays visible after the last activity
    // (seconds, default 1.2) and how fast it fades in/out (time constant in
    // seconds, default 0.06 — smaller is faster). Use longer idle timeouts for
    // controls where the bar is primarily navigational (a log view), shorter for
    // controls where it is incidental (a settings panel).
    void SetIdleHideDelay(float seconds) { idleHideDelaySec_ = seconds; }
    void SetFadeSpeed(float tauSeconds) { fadeTau_ = tauSeconds; }

protected:
    // Re-clamp BOTH axes: a resize changes each viewport extent, so an offset that
    // was legal a moment ago can now point past the end of its content.
    void OnBoundsChanged() override {
        LayoutCostProbe::Scope probe(LayoutCostKey::ScrollViewerBoundsChanged);
        SetOffset(offset_);
        SetOffsetX(offsetX_);
    }

private:
    float BarWidth() const;   // current width (DIP), lerped thin<->wide
    RectDip TrackRect() const;
    float HBarThickness() const;  // horizontal rail thickness (DIP)
    // Length of the horizontal track. Shortened by the vertical rail's footprint when
    // BOTH bars are live, so the two do not overlap in the corner — the alternative
    // (letting them cross) puts two translucent pills on top of each other, which
    // reads as a bright square exactly where the eye lands when reaching for either.
    float HTrackLength() const;

    float contentHeight_ = 0.0f;
    float offset_ = 0.0f;
    float target_ = 0.0f;      // smooth-scroll destination
    bool animating_ = false;
    bool dragging_ = false;
    float dragStartY_ = 0.0f;
    float dragStartOffset_ = 0.0f;

    // Horizontal axis. No smooth-scroll target: horizontal movement comes from
    // Shift+wheel, a thumb drag and caret-following, none of which want inertia —
    // and a tween would force hit-testing to ask "where is the horizontal scroll
    // RIGHT NOW" mid-flight, which is the one bug class the vertical axis had to
    // solve with EffectiveOffset(). Not having the tween removes the question.
    float contentWidth_ = 0.0f;
    float offsetX_ = 0.0f;
    bool draggingX_ = false;
    float dragStartX_ = 0.0f;
    float dragStartOffsetX_ = 0.0f;
    float expandX_ = 0.0f;
    float expandXTarget_ = 0.0f;
    bool barHoverX_ = false;

    // Fade + expand state (0..1). visibility_ = opacity; expand_ = thin->pill.
    float visibility_ = 0.0f;
    float visTarget_ = 0.0f;
    float expand_ = 0.0f;
    float expandTarget_ = 0.0f;
    float idleSec_ = 0.0f;     // seconds since the last activity
    bool barHover_ = false;
    bool keepVisibleWhenOverflow_ = false;  // never fully hide when content overflows
    float idleHideDelaySec_ = 1.2f;  // auto-hide after this idle time
    float fadeTau_ = 0.06f;          // fade/expand time constant (smaller = faster)
};

} // namespace fluent
