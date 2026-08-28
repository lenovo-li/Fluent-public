// RadioButton.h — Fluent radio button (single choice within a group).
//
// An outer ring + an inner dot that scales in when selected, animated via the
// host's per-frame tick (same mechanism as CheckBox / smooth scrolling). Group
// membership is expressed by sharing an int* (the group's selected value) plus
// this button's own value: selecting one sets the shared value, and the others
// animate their dot out on the next tick. Space/Enter or a click selects.
//
// RadioButton is a ButtonBase (roadmap §WP-06): ButtonBase supplies the text
// label (via ContentControl), the focusable+clickable setup, and click/Space/Enter
// → OnActivate(); RadioButton overrides OnActivate() to run the group-select
// logic. It is NOT a ToggleButton because "checked" here is derived from the group
// state rather than an owned bool, and activation selects (not toggles).
#pragma once

#include "primitives/ButtonBase.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"
#include <string>

namespace fluent {

class RadioButton : public ButtonBase {
public:
    // SetText() is inherited from ContentControl.

    // Bind to a group: `groupValue` is shared by all buttons in the group; this
    // button is selected when *groupValue == value.
    void SetGroup(int* groupValue, int value) { group_ = groupValue; value_ = value; Invalidate(); }
    bool IsSelected() const { return group_ && *group_ == value_; }

    // Fired when this button becomes selected (click or Space/Enter); payload is
    // this button's value. Replaces SetOnSelect(std::function<void(int)>).
    Event<RadioButton, int>& Selected() { return selected_; }

    bool WantsAnimationTick() const override;
    // Adopt the initial state outright on first layout rather than easing into it,
    // so a control built already-set does not animate the first time it is shown.
    void SnapAnimationsToSettledState() override { dotAnim_.SetImmediate(IsSelected() ? 1.0f : 0.0f); }
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;

    // The focus circle is stroked OUTSIDE the ring glyph, which sits at the left edge
    // of bounds_ (WP-07 §S4).
    float VisualOverflowDip() const override {
        return kFocusRingGap + kFocusRingStroke * 0.5f + 1.0f;
    }

protected:
    // ButtonBase routes click + Space/Enter here; RadioButton selects within group.
    void OnActivate() override;
    // DWrite arrives with the tree context; re-measure once attached so the label
    // width is known (roadmap §6.2 — no manual SetDWrite).
    void OnAttachedToTree() override { InvalidateMeasure(); }

private:
    float RingSize() const { return 20.0f; }  // outer ring diameter (DIP)
    // Focus ring: drawn at outerR + kFocusRingGap with this stroke. Shared by Render
    // and CollectDirtyBounds so the dirty rect cannot drift from the paint.
    static constexpr float kFocusRingGap = 3.0f;
    static constexpr float kFocusRingStroke = 1.5f;

    int* group_ = nullptr;   // shared selected value (not owned)
    int value_ = 0;          // this button's value
    AnimatedValue dotAnim_{0.0f};  // 0 = unselected, 1 = selected dot (eased)
    Event<RadioButton, int> selected_;
};

} // namespace fluent
