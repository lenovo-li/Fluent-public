// Rating.h — Fluent star-rating control.
//
// A row of N star glyphs the user clicks to rate something. Hovering previews the
// value under the pointer; clicking commits it. Arrow keys step by one, Home/End
// jump to the ends. A read-only Rating is pure display (not focusable, no hover).
//
// Rating derives Control rather than RangeBase: RangeBase's min_/max_/value_ are
// floats with a coerce pipeline built for continuous ranges, whereas a rating is a
// small integer count of discrete symbols whose *count* is also its maximum. The
// one field a rating shares with RangeBase (a clamped value) is cheaper to own
// than the whole min/max/coerce machinery is to bend, and MaxValue has to dirty
// Measure — RangeBase's Maximum deliberately does not.
//
// Fractional values are supported for DISPLAY (a value from a server average like
// 3.7), and render as a half-filled glyph when the fraction reaches 0.5. User
// interaction always commits whole stars — half-star input is a separate feature
// and is not implied by the fractional display.
//
// Cost is O(MaxValue) DrawText calls per paint, which is right for the 5-glyph
// default. A list showing hundreds of ratings should render a cached text form
// rather than hundreds of these.
#pragma once

#include "../core/Control.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../styling/FocusVisual.h"

namespace fluent {

class Rating : public Control {
public:
    Rating() {
        SetFocusable(true);
        SetClickable(true);
    }

    // The current rating, clamped to [0, MaxValue]. SetValue is programmatic and
    // does NOT raise ValueChanged (same contract as ToggleButton::SetChecked —
    // the event reports user intent, not every mutation).
    float Value() const { return value_; }
    void SetValue(float v);

    // Number of star glyphs, and therefore the maximum value. Clamped to >= 1.
    // Dirties Measure: the control's width is a function of this.
    int MaxValue() const { return maxValue_; }
    void SetMaxValue(int n);

    // Glyph size in DIPs (square). Clamped to [12, 48]. Dirties Measure.
    float IconSize() const { return iconSize_; }
    void SetIconSize(float dip);

    // Read-only: display only. Drops focusability and clears any hover preview,
    // so a read-only Rating is skipped by Tab and never previews.
    bool IsReadOnly() const { return readOnly_; }
    void SetReadOnly(bool ro);

    // Fired when the USER changes the value (click or key). Payload is the new value.
    Event<Rating, float>& ValueChanged() { return valueChanged_; }

    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;

    // Hover preview follows the pointer; leaving restores the committed value.
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;

    // The focus ring is stroked outside bounds_, so the dirty rect must cover it
    // (WP-07 §S4) — declared unconditionally, including while unfocused, so the
    // frame where focus LEAVES still clears those pixels.
    float VisualOverflowDip() const override {
        return FocusRingPadDip(FocusRingSpec{});
    }

protected:
    // A completed click commits the star under the pointer. Using the click gesture
    // (rather than OnPointerPressed) means a press that drags off and releases
    // outside does not commit, which is what every other button here does.
    void OnClickRouted(PointerEventArgs& e) override;

    void OnStateChanged() override { Invalidate(); }
    void OnFocusChanged() override { Invalidate(); }

private:
    // The value the glyph under `dipX` represents: 1-based star index, or 0 when the
    // pointer is left of the first glyph. Window DIPs in, rating out.
    float HitTestValue(float dipX) const;

    // Horizontal gap between glyphs. A method rather than a constant because it
    // reads a theme token, which is only valid while attached.
    float Gap() const;

    // Commit a user-driven change: store, raise ValueChanged, repaint. No-ops when
    // the value is unchanged so a redundant click never schedules a frame.
    void CommitValue(float newValue);

    float value_ = 0.0f;
    int maxValue_ = 5;
    float iconSize_ = 20.0f;
    bool readOnly_ = false;

    // The value under the pointer while hovering, or a negative sentinel when not
    // hovering. A float rather than std::optional<float> to keep the class a plain
    // aggregate of scalars; -1 is unreachable as a rating (values are >= 0).
    static constexpr float kNoHover = -1.0f;
    float hoverValue_ = kNoHover;

    Event<Rating, float> valueChanged_;
};

} // namespace fluent
