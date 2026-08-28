// ToggleSwitch.h — Fluent on/off toggle with an animated sliding knob.
//
// A pill-shaped track + a circular knob that slides left/right when toggled.
// The slide and the track-color crossfade are driven by the host's per-frame
// animation tick (0..1 progress) — the same mechanism as smooth scrolling — so
// no compositor visual is needed. Space/Enter or a click toggles; an Toggled
// event reports the new state. An optional label sits to the right.
//
// ToggleSwitch is a ToggleButton (roadmap §WP-06): ToggleButton supplies the
// checked bool, Toggle(), and click/Space/Enter → OnActivate() funnelling.
// ToggleSwitch keeps its own public IsOn()/SetOn()/Toggled() names (thin aliases
// over ToggleButton's IsChecked/SetChecked, so zero caller changes) and adds the
// knob slide animation + its own drawing.
#pragma once

#include "primitives/ToggleButton.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"
#include <string>

namespace fluent {

class ToggleSwitch : public ToggleButton {
public:
    // SetText() is inherited from ContentControl.

    // Thin aliases over ToggleButton::IsChecked/SetChecked so callers see the
    // historical on/off vocabulary unchanged.
    bool IsOn() const { return IsChecked(); }
    void SetOn(bool on) { SetChecked(on); }

    // Fired on toggle (click or Space/Enter); payload is the new on/off state.
    // Replaces SetOnChange(std::function<void(bool)>).
    Event<ToggleSwitch, bool>& Toggled() { return toggled_; }

    // Space/Enter toggles when focused (routed — inherited from ButtonBase).

    // Per-frame slide / crossfade easing.
    bool WantsAnimationTick() const override;
    // Adopt the initial state outright on first layout rather than easing into it,
    // so a control built already-set does not animate the first time it is shown.
    void SnapAnimationsToSettledState() override { slide_.SetImmediate(IsOn() ? 1.0f : 0.0f); }
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;

    // The focus ring is stroked OUTSIDE the track, which sits at the left edge of
    // bounds_ (WP-07 §S4).
    float VisualOverflowDip() const override {
        return kFocusRingGap + kFocusRingStroke * 0.5f + 1.0f;
    }

protected:
    // Raise the public Toggled event when the user toggles (ToggleButton hook).
    void OnToggleChanged(bool newState) override { toggled_.Raise(*this, newState); }
    // DWrite arrives with the tree context; re-measure once attached so the label
    // width is known (roadmap §6.2 — no manual SetDWrite).
    void OnAttachedToTree() override { InvalidateMeasure(); }

private:
    float TrackW() const { return 40.0f; }  // track width (DIP)
    float TrackH() const { return 20.0f; }  // track height (DIP)
    // Focus ring: drawn around the track, inflated by this gap, with this stroke.
    // Shared by Render and CollectDirtyBounds so the two cannot drift apart.
    static constexpr float kFocusRingGap = 3.0f;
    static constexpr float kFocusRingStroke = 1.5f;

    // 0 = off (knob left), 1 = on (knob right), eased via fadeTau token.
    AnimatedValue slide_{0.0f};
    Event<ToggleSwitch, bool> toggled_;
};

} // namespace fluent
