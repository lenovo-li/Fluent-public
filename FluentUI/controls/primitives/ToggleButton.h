// ToggleButton.h — a ButtonBase whose activation flips a boolean (roadmap §WP-06).
//
// CheckBox and ToggleSwitch are the same control underneath: a focusable,
// clickable element holding one bool that activation (click / Space / Enter)
// flips, raising a "new state" notification and repainting. They differed only in
// how they draw it (a check-in stroke vs a sliding knob) and in the public names
// they expose (IsChecked/Checked vs IsOn/Toggled). ToggleButton owns the shared
// state machine — the bool, SetChecked (no-op when unchanged), and Toggle() wired
// to ButtonBase::OnActivate — and calls the virtual OnToggle() after a flip so the
// concrete control can raise its own typed Event with its own name. The subclass
// keeps its public getters/setters as thin aliases over IsChecked()/SetChecked(),
// so no caller changes (roadmap acceptance: no duplicated checked logic, public
// API preserved).
//
// RadioButton is intentionally NOT a ToggleButton: its "checked" is derived from
// shared group state (not an owned bool) and activation *selects* rather than
// toggles, so it derives ButtonBase directly.
#pragma once

#include "ButtonBase.h"

namespace fluent {

class ToggleButton : public ButtonBase {
public:
    // The toggle state. SetChecked is silent when unchanged (no OnToggle, no
    // repaint) so a redundant set never schedules a frame; it does NOT raise the
    // change notification (that is reserved for a user-driven Toggle, matching the
    // old SetChecked/SetOn which only Invalidated). Subclasses expose their own
    // names (IsChecked/IsOn) as inline aliases over these.
    bool IsChecked() const { return checked_; }
    void SetChecked(bool v) {
        if (checked_ == v) return;
        checked_ = v;
        OnToggle();       // subclass repaints / re-targets its animation
    }

protected:
    // Activation flips the state (user-driven), so this raises the notification.
    void OnActivate() override { Toggle(); }

    // Flip + notify: used by OnActivate. Sets the bool, lets the subclass raise its
    // typed Event (OnToggleChanged), then runs the shared OnToggle repaint hook.
    void Toggle() {
        checked_ = !checked_;
        OnToggleChanged(checked_);  // subclass raises its Event<...,bool>
        OnToggle();
    }

    // Fired only on a user toggle, carrying the new state — the subclass raises its
    // public Event here (CheckBox::Checked / ToggleSwitch::Toggled). Default no-op.
    virtual void OnToggleChanged(bool /*newState*/) {}

    // Called after the state changes by any path (Toggle or SetChecked): the
    // subclass repaints and re-targets its animation. Default repaints.
    virtual void OnToggle() { Invalidate(); }

    bool checked_ = false;
};

} // namespace fluent
