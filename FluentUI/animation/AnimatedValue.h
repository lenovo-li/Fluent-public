// AnimatedValue.h — a scalar eased on the UI-thread animation tick (roadmap §26).
//
// Every self-drawn control that animates a 0..1 (or value-space) quantity on the
// host's per-frame tick repeated the same three lines: an exponential approach
// toward a state-driven target and a snap once it is within a small epsilon
//
//     float f = 1 - exp(-dt / tau);
//     current += (target - current) * f;
//     if (fabs(target - current) <= snapEps) current = target;
//
// Button's tint, CheckBox's check-in, RadioButton's dot, ToggleSwitch's knob
// slide, Slider's fill and ProgressBar's determinate fill all carried their own
// copy (some with a local kAnimTau constant left over from before the motion
// token). AnimatedValue holds just the current scalar and the shared math, so a
// control keeps its own target/tau (read from the theme's MotionTokens) and its
// own WantsAnimationTick epsilon — the *policy* stays in the control, only the
// duplicated *mechanism* moves here. It is a plain value (no allocation, trivially
// copyable) and converts to float so existing render code reads it unchanged.
#pragma once

#include <cmath>

namespace fluent {

struct AnimatedValue {
    float value = 0.0f;

    AnimatedValue() = default;
    explicit AnimatedValue(float v) : value(v) {}

    // Read like a float so `p = anim_` / `anim_ * r` in render code is unchanged.
    operator float() const { return value; }

    // Frame-rate-independent exponential approach toward `target` over the time
    // constant `tau` (seconds). Snaps exactly to the target once within snapEps so
    // the animation terminates cleanly (and WantsTick can then report false).
    // tau <= 0 means "no easing": jump straight to the target.
    void Approach(float target, float dtSec, float tau, float snapEps = 0.01f) {
        if (tau <= 0.0f) { value = target; return; }
        float f = 1.0f - std::exp(-dtSec / tau);
        value += (target - value) * f;
        if (std::fabs(target - value) <= snapEps) value = target;
    }

    // True while the value has not yet reached `target` within `eps` — the control
    // returns this from WantsAnimationTick so the host keeps ticking it. Each
    // control passes its own historical epsilon so motion feel is unchanged.
    bool Animating(float target, float eps = 0.001f) const {
        return std::fabs(value - target) > eps;
    }

    void SetImmediate(float v) { value = v; }
};

} // namespace fluent
