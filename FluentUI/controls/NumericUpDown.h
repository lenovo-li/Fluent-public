// NumericUpDown.h — Fluent numeric spinner control.
//
// A RangeBase with inline up/down RepeatButtons and an editable text field, all
// drawn into the window content surface (no child elements). The text field is
// self-drawn with DWrite rather than embedding a TextBox — an embedded TextBox
// would bring a second Tab stop (see ComboBox.h rationale), plus its own attach
// lifecycle, caret blink, and context menu, all for a single line with no
// selection. What editing actually needs is a caret index, Backspace/Delete,
// and arrow keys, which is small enough to own inline.
//
// Geometry: [       TextBox       ] [↑]
//                                   [↓]
//
// The buttons are 20 DIP wide, stacked vertically on the right. The text field
// takes the remaining width. Clicking a button (or holding it) steps the value
// by Step; typing commits on Enter or focus loss; arrow keys step by ±Step,
// Page Up/Down by ±10×Step.
#pragma once

#include "primitives/RangeBase.h"
#include "../base/Event.h"
#include "../core/Subscription.h"
#include "../input/RoutedEvent.h"
#include <string>
#include <algorithm>

namespace fluent {

class NumericUpDown : public RangeBase {
public:
    NumericUpDown();

    // Min/Max/Value inherited from RangeBase. Value is clamped to [Min, Max] and
    // snapped to the step grid measured from Min.

    // The delta applied by one arrow click or one arrow key. Page Up/Down apply
    // 10x this. Step lives here rather than in RangeBase for the same reason it
    // lives in Slider: ProgressBar has no concept of one.
    void SetStep(float v) { step_ = std::max(0.0001f, v); }
    float Step() const { return step_; }

    // Aliases for the WPF-style names, matching Slider's.
    void SetMin(float v) { SetMinimum(v); }
    void SetMax(float v) { SetMaximum(v); }

    // Number of decimal places to show. Default 0 (integers).
    void SetDecimalPlaces(int places) { decimalPlaces_ = places; UpdateTextFromValue(); }
    int DecimalPlaces() const { return decimalPlaces_; }

    // Fired when the value changes (button click, keyboard step, or text commit).
    Event<NumericUpDown, float>& ValueChanged() { return valueChanged_; }

    // Control overrides.
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnTextInput(wchar_t ch) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    bool WantsBlink() const override { return IsFocused(); }
    void OnBlink() override;
    float VisualOverflowDip() const override { return 3.75f; }  // focus ring

protected:
    void OnFocusChanged() override;
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnValueChanged(float old, float newVal) override;
    float CoerceValue(float v) const override;

private:
    // Button geometry (DIP, relative to bounds_).
    static constexpr float kButtonWidth = 20.0f;
    RectDip UpButtonRect() const;
    RectDip DownButtonRect() const;
    RectDip TextFieldRect() const;

    // Button state tracking (for hover/press visuals).
    enum class ButtonPart { None, Up, Down };
    ButtonPart HitTestButton(float dipX, float dipY) const;
    void UpdateButtonState(float dipX, float dipY);
    void StartRepeat(ButtonPart btn);
    void StopRepeat();

    // RepeatButton logic (auto-repeat while held).
    bool WantsAnimationTick() const override { return repeating_; }
    void OnAnimationTick(float dtSec) override;

    // Text editing.
    void UpdateTextFromValue();  // Value → text_ (respects decimalPlaces_)
    void CommitTextToValue();    // text_ → Value (parse, coerce, update)
    void InsertChar(wchar_t ch);
    void DeleteChar();           // Backspace
    void DeleteCharForward();    // Delete
    void ClampCaret();
    void StepBy(float delta);    // Change value by delta

    std::wstring text_;          // The displayed/edited text
    UINT32 caret_ = 0;           // Caret position (code-unit offset)
    float step_ = 1.0f;
    int decimalPlaces_ = 0;
    bool caretVisible_ = false;

    // Button state (for hover/press rendering).
    ButtonPart hoveredButton_ = ButtonPart::None;
    ButtonPart pressedButton_ = ButtonPart::None;
    bool pointerCaptured_ = false;

    // RepeatButton state.
    bool repeating_ = false;
    bool armed_ = false;         // Initial delay phase
    float accumSec_ = 0.0f;
    static constexpr float kInitialDelay = 0.5f;  // seconds before first repeat
    static constexpr float kInterval = 0.05f;     // 20 Hz repeat rate

    Event<NumericUpDown, float> valueChanged_;
};

} // namespace fluent
