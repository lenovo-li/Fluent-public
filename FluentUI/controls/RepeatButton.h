// RepeatButton.h — a Button that fires Click repeatedly while held down.
//
// WHY IT DERIVES FROM Button RATHER THAN ButtonBase. The auto-repeat is the only
// difference; everything else — the three Kinds, the tint animation, the focus
// ring, the content layout — is Button's, and duplicating that to "stay closer to
// the primitive" would mean a second copy of the paint path that drifts from the
// first. The cost is that RepeatButton inherits Button's Kind enum, which is a
// feature here: a repeat arrow wants Kind::Subtle.
//
// TIMING MODEL. Press fires once immediately (so a single click behaves exactly
// like a Button), then waits InitialDelay before the first repeat, then fires every
// Interval. Defaults mirror the Win32 keyboard-repeat feel a user already has in
// their fingers: 250ms then ~30Hz. The repeat is driven by OnAnimationTick, the
// framework's existing per-frame callback — no timer objects, no thread, and it
// stops being called the moment WantsAnimationTick() goes false.
//
// The repeat STOPS on release or pointer-exit (dragging off a held arrow must not
// keep incrementing) via OnStateChanged: leaving Pressed for any reason ends the
// burst. It deliberately does NOT auto-repeat on keyboard Space/Enter: Win32
// already delivers key-repeat as separate WM_KEYDOWN messages, so ButtonBase's
// handler fires per repeat on its own.
#pragma once

#include "Button.h"

namespace fluent {

class RepeatButton : public Button {
public:
    RepeatButton() = default;

    // Time held before the first repeat, and the gap between repeats (seconds).
    // Clamped to sane floors so a caller cannot request a busy-loop.
    void SetInitialDelay(float sec) { initialDelaySec_ = std::max(0.0f, sec); }
    void SetInterval(float sec) { intervalSec_ = std::max(0.008f, sec); }
    float InitialDelay() const { return initialDelaySec_; }
    float Interval() const { return intervalSec_; }

    // True while the button is held and auto-repeat is running. Exposed for tests
    // and for a host that wants to suppress other work during a repeat burst.
    bool IsRepeating() const { return repeating_; }

    // --- Animation tick (repeat driver) ------------------------------------
    // Public for unit tests: headless tests drive time by calling OnAnimationTick
    // directly with synthetic dt, rather than wiring a full AnimationRegistry.
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:

    // State machine: entering Pressed starts the burst, leaving it stops.
    void OnStateChanged() override;

    // Activation: the first fire on press AND every repeat tick call this.
    void OnActivate() override;

private:
    // Ends a repeat burst. Safe to call when not repeating.
    void StopRepeat();

    float initialDelaySec_ = 0.25f;   // Win32 default keyboard delay
    float intervalSec_ = 0.033f;      // ~30 Hz, Win32 default keyboard speed
    bool repeating_ = false;
    bool armed_ = false;              // true until the initial delay has elapsed
    float accumSec_ = 0.0f;           // time since press (armed) or since last fire
};

} // namespace fluent
