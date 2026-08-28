// RangeBase.h — the shared value-range primitive (roadmap §WP-06).
//
// Slider and ProgressBar both maintain a scalar value bounded by [min, max] with
// identical clamping semantics, and both eased their fill toward the current value
// via the same exponential tick. RangeBase extracts the three members (min_, max_,
// value_) and the coerce/change pipeline so neither control duplicates them.
//
// Design choices:
//   * Non-template: both Slider and ProgressBar are float-only; no genericity needed.
//   * CoerceValue() is virtual so Slider can override to add step-snapping on top of
//     the base [min, max] clamp.
//   * OnValueChanged() hook (protected virtual) lets each subclass fire its own
//     typed Event<Sender,float> without RangeBase needing to know the sender type.
//   * animFill_ (the eased display value) stays in each subclass: Slider and
//     ProgressBar animate toward the logical value on different timescales and with
//     different semantics (Slider over the value range; ProgressBar 0..1).
//   * No keyboard or pointer input here (Slider overrides OnPointerPressed etc.;
//     ProgressBar is read-only with no input).
#pragma once

#include "../../core/Control.h"
#include <algorithm>

namespace fluent {

class RangeBase : public Control {
public:
    // --- Value management -------------------------------------------------
    void SetMinimum(float v) {
        if (min_ == v) return;
        min_ = v;
        // Re-coerce and notify if the value was pushed out of the new range.
        float coerced = CoerceValue(value_);
        if (coerced != value_) { value_ = coerced; OnValueChanged(value_, value_); }
        Invalidate();
    }
    void SetMaximum(float v) {
        if (max_ == v) return;
        max_ = v;
        float coerced = CoerceValue(value_);
        if (coerced != value_) { value_ = coerced; OnValueChanged(value_, value_); }
        Invalidate();
    }

    // Set the value; silently clamps + coerces without notification when called
    // from the subclass ctor / programmatic reset; the notification path is kept
    // in SetValue so a user-driven call still fires the event.
    void SetValue(float v) {
        float prev = value_;
        value_ = CoerceValue(v);
        if (value_ != prev) OnValueChanged(prev, value_);
        Invalidate();
    }

    float Minimum() const { return min_; }
    float Maximum() const { return max_; }
    float Value()   const { return value_; }

protected:
    // Constrain a candidate value to the legal range. Base implementation clamps
    // to [min_, max_]. Slider overrides to also snap to the nearest step grid.
    virtual float CoerceValue(float v) const {
        return std::clamp(v, min_, max_);
    }

    // Called after the value changes (only when it actually changed). Subclasses
    // fire their typed Event here. Default is a no-op.
    virtual void OnValueChanged(float /*oldVal*/, float /*newVal*/) {}

    float min_   = 0.0f;
    float max_   = 100.0f;
    float value_ = 0.0f;
};

} // namespace fluent
