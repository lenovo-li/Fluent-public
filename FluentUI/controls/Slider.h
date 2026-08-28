// Slider.h — Fluent horizontal slider with animated fill.
//
// A track + filled portion + draggable thumb. The fill width and thumb position
// animate to the current value via the host's per-frame tick. Arrow keys step
// the value; dragging sets it immediately.
//
// Slider is a RangeBase (roadmap §WP-06): RangeBase owns min_/max_/value_ and the
// coerce pipeline; Slider adds the step grid (CoerceValue override), the keyboard
// and drag input, the animated fill (animFill_), and the ValueChanged event.
#pragma once

#include "primitives/RangeBase.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"

namespace fluent {

class Slider : public RangeBase {
public:
    enum class Orientation { Horizontal, Vertical };

    Slider() { SetFocusable(true); }

    // Step is Slider-specific (ProgressBar has no concept of step).
    void SetStep(float v) { step_ = std::max(0.0001f, v); }
    float Step() const { return step_; }

    // Orientation: Horizontal (default) lays the track left-to-right with the thumb
    // moving horizontally; Vertical lays it top-to-bottom with the thumb moving
    // vertically. Affects Measure, input mapping, and rendering.
    void SetOrientation(Orientation o) { SetProperty(orientation_, o, DirtyFlags::Measure); }
    Orientation GetOrientation() const { return orientation_; }

    // Aliases for the WPF-style Min/Max names the demo uses.
    void SetMin(float v) { SetMinimum(v); }
    void SetMax(float v) { SetMaximum(v); }

    // Fired when the value changes (drag, arrow keys, or SetValue). Payload is the
    // new value. Replaces SetOnChange(std::function<void(float)>).
    Event<Slider, float>& ValueChanged() { return valueChanged_; }

    void OnKeyDownRouted(KeyEventArgs& e) override;

    bool WantsAnimationTick() const override;
    // Adopt the initial state outright on first layout rather than easing into it,
    // so a control built already-set does not animate the first time it is shown.
    void SnapAnimationsToSettledState() override { animFill_.SetImmediate(value_); }
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;

    // The thumb circle + focus ring are drawn CENTERED on the track and, while
    // dragging/focused, extend beyond the slider's layout bounds (a thin track
    // but a fat thumb+ring). Partial redraw clips + clears only the dirty rect,
    // so a bounds-sized dirty rect would clip the thumb (flat-topped "capsule")
    // and leave residue where the ring overflowed a previous frame.
    float VisualOverflowDip() const override;

    // Drag semantics (routed): press captures the pointer and jumps the value to
    // the click position; move updates while captured; release drops capture.
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

protected:
    // RangeBase hook: fire the typed event.
    void OnValueChanged(float /*old*/, float newVal) override {
        valueChanged_.Raise(*this, newVal);
    }
    // RangeBase hook: clamp + step-snap.
    float CoerceValue(float v) const override;

    void OnStateChanged() override { Invalidate(); }
    void OnFocusChanged() override { Invalidate(); }

private:
    float NormalizedAnim() const;
    float TrackLeft()  const;
    float TrackRight() const;
    float TrackY()     const;
    float TrackTop()   const;
    float TrackBottom() const;
    float TrackX()     const;
    float ValueFromX(float dipX) const;
    float ValueFromY(float dipY) const;
    void  StepBy(float delta);

    Orientation orientation_ = Orientation::Horizontal;
    float step_ = 1.0f;
    AnimatedValue animFill_{0.0f};  // eased fill, same units as value_
    bool  dragging_ = false;
    Event<Slider, float> valueChanged_;
};

} // namespace fluent
