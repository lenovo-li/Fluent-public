// CheckBox.h — Fluent check box with an animated check-in transition.
//
// A square box + label. Toggling animates the accent fill and the checkmark
// stroke (0..1 progress) via the host's per-frame animation tick — the same
// mechanism that drives smooth scrolling — so no compositor visual is needed.
// Space/Enter or a click toggles; a Checked event reports the new state.
//
// CheckBox is a ToggleButton (roadmap §WP-06): the checked bool, the click /
// Space / Enter activation, and Toggle() live in the ToggleButton / ButtonBase
// primitives; the label lives in ContentControl. CheckBox keeps its public
// IsChecked()/SetChecked()/Checked() names (inherited or thin) and only adds the
// check-in animation + its own drawing.
#pragma once

#include "primitives/ToggleButton.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"
#include <string>
#include <vector>

namespace fluent {

class CheckBox : public ToggleButton {
public:
    // IsChecked()/SetChecked() are inherited from ToggleButton unchanged.
    // SetText() is inherited from ContentControl.

    // Fired on toggle (click or Space/Enter); the payload is the new checked
    // state. Replaces SetOnChange(std::function<void(bool)>).
    Event<CheckBox, bool>& Checked() { return checked_evt_; }

    // Per-frame check-in / check-out easing.
    bool WantsAnimationTick() const override;
    // Adopt the initial state outright on first layout rather than easing into it,
    // so a control built already-set does not animate the first time it is shown.
    void SnapAnimationsToSettledState() override { checkAnim_.SetImmediate(IsChecked() ? 1.0f : 0.0f); }
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;

    // The focus ring is stroked OUTSIDE the box, which sits at the left edge of
    // bounds_ (WP-07 §S4) — see FocusRingPadDip.
    float VisualOverflowDip() const override;

protected:
    // Raise the public Checked event when the user toggles (ToggleButton hook).
    void OnToggleChanged(bool newState) override { checked_evt_.Raise(*this, newState); }
    // DWrite arrives with the tree context; re-measure once it is available so the
    // label width is computed (roadmap §6.2 — no manual SetDWrite).
    void OnAttachedToTree() override { InvalidateMeasure(); }

private:
    float BoxSize() const { return 20.0f; }  // square box edge (DIP)
    // How far outside the box the focus ring sits (historical value). Shared by
    // Render and CollectDirtyBounds so the dirty rect cannot drift from the paint.
    static constexpr float kFocusRingInset = 3.0f;

    AnimatedValue checkAnim_{0.0f};   // 0 = unchecked, 1 = fully checked (eased)
    Event<CheckBox, bool> checked_evt_;
};

} // namespace fluent
