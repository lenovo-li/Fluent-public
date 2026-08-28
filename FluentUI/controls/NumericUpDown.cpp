// NumericUpDown.cpp — see header.

#include "NumericUpDown.h"
#include "../graphics/DWriteContext.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include "../input/InputManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fluent {

namespace {
// Chevron glyphs. Drawn from the theme's own font rather than Segoe Fluent Icons:
// the icon font is not guaranteed present (it ships with Win11), and a missing
// glyph would render as a tofu box. U+25B2/U+25BC (BLACK UP/DOWN-POINTING
// TRIANGLE) are in every Windows UI font going back further than this framework
// supports, and ComboBox already draws its chevron the same way.
constexpr wchar_t kUpGlyph = L'\x25B2';
constexpr wchar_t kDownGlyph = L'\x25BC';

constexpr float kGlyphSizeDip = 7.0f;   // triangles read as arrows at this size
constexpr float kTextPadDip = 8.0f;     // text inset inside the field
constexpr float kCornerDip = 4.0f;
} // namespace

NumericUpDown::NumericUpDown() {
    SetFocusable(true);
    // NOT SetClickable: the base click gesture fires OnClickRouted after a
    // press/release pair, which is one event per click — a spinner needs the press
    // itself (to start auto-repeat) and the release (to stop it), so it owns the
    // pointer handlers directly. Same reasoning as Slider, which also skips it.
    UpdateTextFromValue();
}

// --- RangeBase hooks -------------------------------------------------------

float NumericUpDown::CoerceValue(float v) const {
    if (max_ <= min_) return min_;
    v = std::clamp(v, min_, max_);
    // Snap to the step grid measured FROM min_, not from zero: a range of
    // [0.5, 10] with step 1 should offer 0.5/1.5/2.5, not 1/2/3 — the minimum is
    // by definition a legal value, so the grid has to include it.
    if (step_ > 0.0001f) {
        const float snapped = min_ + std::round((v - min_) / step_) * step_;
        v = std::clamp(snapped, min_, max_);
    }
    return v;
}

void NumericUpDown::OnValueChanged(float /*old*/, float newVal) {
    // Re-render the text from the coerced value. This is what makes a typed "abc"
    // or an out-of-range "999" snap back to something legal on commit.
    UpdateTextFromValue();
    valueChanged_.Raise(*this, newVal);
}

// --- Geometry --------------------------------------------------------------

RectDip NumericUpDown::UpButtonRect() const {
    const float h = bounds_.h * 0.5f;
    return {bounds_.right() - kButtonWidth, bounds_.y, kButtonWidth, h};
}

RectDip NumericUpDown::DownButtonRect() const {
    const float h = bounds_.h * 0.5f;
    return {bounds_.right() - kButtonWidth, bounds_.y + h, kButtonWidth, bounds_.h - h};
}

RectDip NumericUpDown::TextFieldRect() const {
    return {bounds_.x, bounds_.y, std::max(0.0f, bounds_.w - kButtonWidth), bounds_.h};
}

NumericUpDown::ButtonPart NumericUpDown::HitTestButton(float dipX, float dipY) const {
    if (UpButtonRect().contains(dipX, dipY)) return ButtonPart::Up;
    if (DownButtonRect().contains(dipX, dipY)) return ButtonPart::Down;
    return ButtonPart::None;
}

void NumericUpDown::UpdateButtonState(float dipX, float dipY) {
    const ButtonPart next = HitTestButton(dipX, dipY);
    if (next == hoveredButton_) return;
    hoveredButton_ = next;
    Invalidate();
}

// --- Layout ----------------------------------------------------------------

void NumericUpDown::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    // 32 DIP tall matches TextBox (WinUI's control height) so a spinner lines up
    // with text fields on the same row. Width defaults to 120: enough for the
    // buttons plus roughly six digits.
    SetDesired({IsAuto(width_)  ? (availW > 0.0f ? std::min(availW, 120.0f) : 120.0f) : width_,
                IsAuto(height_) ? Theme().spacing.controlHeightNormal : height_});
}

// --- Render ----------------------------------------------------------------

void NumericUpDown::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;
    const bool disabled = !enabled_;
    const bool focused = IsFocused();

    const RectDip field = TextFieldRect();
    const RectDip up = UpButtonRect();
    const RectDip down = DownButtonRect();

    // Focus ring first: it is stroked OUTSIDE bounds_ and a later content clip
    // would cut it away (the ordering constraint in FocusVisual.h).
    //
    // NAMED-FIELD INIT IS LOAD-BEARING HERE. This used to read
    // `FocusRingSpec{kCornerDip}`, which is positional and so set the FIRST member —
    // `inset` — to 4.0 rather than the intended `cornerRadius`. Two consequences, both
    // real: the ring was stroked 2 DIP further out than the default (inset 4 vs 2),
    // putting its outer edge at 4 + 0.75 = 4.75 DIP while VisualOverflowDip() below
    // declares only 3.75 — an under-declaration, which is the residue bug class this
    // codebase already fixed for Hyperlink and Expander. And the corner radius stayed
    // at the struct default, so the extra inset was not matched by rounder corners and
    // the ring's straight edges pulled visibly away from the field.
    //
    // Caught by FocusRingClipMatrixTests: probing the ring lines found 6 accent pixels
    // per edge where Button showed ~240, i.e. the probe was sampling 2 DIP inside a
    // ring that had moved outward. Keep the field name.
    if (focused) DrawFocusRing(dc, bounds_, pal, FocusRingSpec{.cornerRadius = kCornerDip});

    // Field fill + border. EffectiveBackground/EffectiveBorderBrush honor a
    // per-instance override before falling back to the token.
    const D2D1_ROUNDED_RECT fieldRr = D2D1::RoundedRect(
        D2D1::RectF(field.x, field.y, field.right(), field.bottom()),
        kCornerDip, kCornerDip);
    dc.FillRoundedRect(fieldRr, EffectiveBackground(pal.controlFillDefault));

    const float stroke = EffectiveBorderThickness(th.spacing.borderWidth);
    if (stroke > 0.0f) {
        // Focused fields take the accent border, which is the standard Fluent cue
        // that the field is the one receiving keystrokes.
        const D2D1_COLOR_F border = focused ? EffectiveAccentColor(pal.accent)
                                            : EffectiveBorderBrush(pal.controlStrokeDefault);
        const float h = stroke * 0.5f;
        dc.DrawRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(field.x + h, field.y + h,
                                          field.right() - h, field.bottom() - h),
                              kCornerDip, kCornerDip),
            border, stroke);
    }

    // Button fills: transparent at rest (the Subtle look), so only the hovered or
    // pressed arrow shows a plate.
    auto buttonFill = [&](ButtonPart part, const RectDip& r) {
        if (disabled) return;
        const bool pressed = pressedButton_ == part;
        const bool hovered = hoveredButton_ == part && pressedButton_ == ButtonPart::None;
        if (!pressed && !hovered) return;
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(r.x, r.y, r.right(), r.bottom()),
                              kCornerDip, kCornerDip),
            pressed ? pal.controlFillPressed : pal.controlFillHover);
    };
    buttonFill(ButtonPart::Up, up);
    buttonFill(ButtonPart::Down, down);

    DWriteContext* dw = Dwrite();
    if (!dw) return;

    // Arrow glyphs. Centered in each button rect by the format's own alignment.
    if (IDWriteTextFormat* glyphFmt =
            dw->Format(kGlyphSizeDip, DWRITE_FONT_WEIGHT_NORMAL,
                       DWRITE_TEXT_ALIGNMENT_CENTER)) {
        // An arrow is dimmed when its direction is exhausted, so "already at the
        // maximum" is visible without trying to click.
        const bool upDead = disabled || value_ >= max_;
        const bool downDead = disabled || value_ <= min_;
        dc.DrawText(&kUpGlyph, 1, glyphFmt,
                    D2D1::RectF(up.x, up.y, up.right(), up.bottom()),
                    upDead ? pal.textSecondary : pal.textPrimary);
        dc.DrawText(&kDownGlyph, 1, glyphFmt,
                    D2D1::RectF(down.x, down.y, down.right(), down.bottom()),
                    downDead ? pal.textSecondary : pal.textPrimary);
    }

    // Value text, left-aligned inside the field's padding.
    const float fontSize = EffectiveFontSize(th.typography.bodySize);
    IDWriteTextFormat* textFmt =
        dw->Format(fontSize, EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL),
                   DWRITE_TEXT_ALIGNMENT_LEADING);
    if (!textFmt) return;

    const float textLeft = field.x + kTextPadDip;
    const float textRight = std::max(textLeft, field.right() - kTextPadDip);
    dc.DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), textFmt,
                D2D1::RectF(textLeft, field.y, textRight, field.bottom()),
                disabled ? pal.textSecondary : EffectiveForeground(pal.textPrimary),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Caret. Measured with a throwaway layout rather than a cached one: this only
    // runs on a blink tick for the single focused spinner, and the string is a
    // handful of digits, so a cache would cost more (invalidation on every value
    // change) than it saves.
    if (focused && caretVisible_ && !disabled) {
        float caretX = textLeft;
        if (caret_ > 0 && dw->Factory()) {
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dw->Factory()->CreateTextLayout(
                    text_.c_str(), static_cast<UINT32>(text_.size()), textFmt,
                    textRight - textLeft, field.h, layout.GetAddressOf())) && layout) {
                DWRITE_HIT_TEST_METRICS m{};
                float px = 0.0f, py = 0.0f;
                if (SUCCEEDED(layout->HitTestTextPosition(
                        std::min<UINT32>(caret_, static_cast<UINT32>(text_.size())),
                        FALSE, &px, &py, &m)))
                    caretX = textLeft + px;
            }
        }
        const float pad = (field.h - fontSize) * 0.5f;
        dc.DrawLine({caretX, field.y + pad}, {caretX, field.bottom() - pad},
                    EffectiveForeground(pal.textPrimary), 1.0f);
    }
}

// --- Pointer ---------------------------------------------------------------

void NumericUpDown::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left || !enabled_) return;

    const ButtonPart hit = HitTestButton(e.position.x, e.position.y);
    if (hit != ButtonPart::None) {
        pressedButton_ = hit;
        pointerCaptured_ = true;
        // Capture so a drag off the arrow still delivers the release that stops
        // the repeat — without it, releasing outside would leave it spinning.
        if (Context().input) Context().input->CapturePointer(this);
        StartRepeat(hit);
        Invalidate();
        e.handled = true;
        return;
    }

    if (TextFieldRect().contains(e.position.x, e.position.y)) {
        caret_ = static_cast<UINT32>(text_.size());
        caretVisible_ = true;
        Invalidate();
        e.handled = true;
    }
}

void NumericUpDown::OnPointerMoved(PointerEventArgs& e) {
    if (!enabled_) return;
    if (!pointerCaptured_) {
        UpdateButtonState(e.position.x, e.position.y);
        return;
    }
    // Held: dragging off the arrow suspends the repeat, dragging back resumes it.
    // Matches RepeatButton, which stops on any exit from the Pressed state.
    const bool stillOn = HitTestButton(e.position.x, e.position.y) == pressedButton_;
    if (stillOn && !repeating_) {
        StartRepeat(pressedButton_);
        Invalidate();
    } else if (!stillOn && repeating_) {
        StopRepeat();
        Invalidate();
    }
}

void NumericUpDown::OnPointerReleased(PointerEventArgs& e) {
    if (!pointerCaptured_) return;
    pointerCaptured_ = false;
    pressedButton_ = ButtonPart::None;
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    StopRepeat();
    Invalidate();
    e.handled = true;
}

// --- Keyboard --------------------------------------------------------------

void NumericUpDown::OnKeyDownRouted(KeyEventArgs& e) {
    if (!enabled_) return;

    switch (e.vk) {
        case VK_UP:     StepBy(1.0f);    break;
        case VK_DOWN:   StepBy(-1.0f);   break;
        case VK_PRIOR:  StepBy(10.0f);   break;   // Page Up
        case VK_NEXT:   StepBy(-10.0f);  break;   // Page Down
        case VK_RETURN: CommitTextToValue(); break;
        // Home/End move the CARET, not the value: this control has a text field, so
        // the text-editing meaning of those keys wins. Slider, which has no text,
        // uses them for min/max.
        case VK_HOME:   caret_ = 0; Invalidate(); break;
        case VK_END:    caret_ = static_cast<UINT32>(text_.size()); Invalidate(); break;
        case VK_LEFT:   if (caret_ > 0) { --caret_; Invalidate(); } break;
        case VK_RIGHT:  if (caret_ < text_.size()) { ++caret_; Invalidate(); } break;
        case VK_BACK:   DeleteChar(); break;
        case VK_DELETE: DeleteCharForward(); break;
        default: return;  // unhandled: keep bubbling
    }
    e.handled = true;
}

void NumericUpDown::OnTextInput(wchar_t ch) {
    if (!enabled_) return;
    // Accept only what can appear in a decimal number. Filtering per character
    // (rather than validating the whole string) is what lets a partially-typed
    // "-" or "1." exist — a string-level check would reject both and make the
    // field impossible to type into. Same reasoning as TextEditBase's InputFilter.
    const bool digit = ch >= L'0' && ch <= L'9';
    const bool sign = ch == L'-' && caret_ == 0 && text_.find(L'-') == std::wstring::npos;
    const bool point = ch == L'.' && decimalPlaces_ > 0 &&
                       text_.find(L'.') == std::wstring::npos;
    if (digit || sign || point) InsertChar(ch);
}

// --- Focus / blink ---------------------------------------------------------

void NumericUpDown::OnFocusChanged() {
    if (IsFocused()) {
        caretVisible_ = true;
    } else {
        // Commit on blur: leaving the field is a commit, so a typed value takes
        // effect without needing Enter. An unparseable string reverts.
        caretVisible_ = false;
        CommitTextToValue();
    }
    Invalidate();
}

void NumericUpDown::OnBlink() {
    if (!IsFocused()) return;
    caretVisible_ = !caretVisible_;
    Invalidate();
}

void NumericUpDown::OnAttachedToTree() {
    // Measure depends on a theme token (controlHeightNormal), which is only
    // available once attached.
    InvalidateMeasure();
}

void NumericUpDown::OnDetachedFromTree() {
    // Nothing acquired at attach, but the repeat must not survive the tree: the
    // animation tick stops being delivered, so a burst left running would resume
    // on re-attach and spin the value without an input.
    StopRepeat();
    pointerCaptured_ = false;
    pressedButton_ = ButtonPart::None;
    hoveredButton_ = ButtonPart::None;
}

// --- Text ------------------------------------------------------------------

void NumericUpDown::UpdateTextFromValue() {
    // swprintf rather than wostringstream: this runs on every value change,
    // including every frame of an auto-repeat burst, and a stream constructs and
    // destroys a locale-aware buffer each call.
    wchar_t buf[32];
    const int places = std::clamp(decimalPlaces_, 0, 6);
    _snwprintf_s(buf, _TRUNCATE, L"%.*f", places, value_);
    text_.assign(buf);
    caret_ = static_cast<UINT32>(text_.size());
    Invalidate();
}

void NumericUpDown::CommitTextToValue() {
    // Hand-rolled parse rather than std::stof: stof throws on failure, and this
    // path is reached on every focus change, so the common case of an empty or
    // half-typed field would be an exception per blur.
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(text_.c_str(), &end);
    const bool ok = end != nullptr && end != text_.c_str() && *end == L'\0' &&
                    std::isfinite(parsed);
    if (!ok) {
        UpdateTextFromValue();  // unparseable: revert to the last legal value
        return;
    }
    const float coerced = CoerceValue(static_cast<float>(parsed));
    if (coerced == value_) {
        // The value did not change, so OnValueChanged will not fire — but the TEXT
        // may still be non-canonical ("007", "3." for a 0-place spinner), so
        // normalize it here. Skipping this leaves the field showing what was typed
        // while the value is something else.
        UpdateTextFromValue();
        return;
    }
    SetValue(coerced);  // fires OnValueChanged -> UpdateTextFromValue
}

void NumericUpDown::InsertChar(wchar_t ch) {
    ClampCaret();
    text_.insert(caret_, 1, ch);
    ++caret_;
    Invalidate();
}

void NumericUpDown::DeleteChar() {
    ClampCaret();
    if (caret_ == 0) return;
    text_.erase(caret_ - 1, 1);
    --caret_;
    Invalidate();
}

void NumericUpDown::DeleteCharForward() {
    ClampCaret();
    if (caret_ >= text_.size()) return;
    text_.erase(caret_, 1);
    Invalidate();
}

void NumericUpDown::ClampCaret() {
    caret_ = std::min(caret_, static_cast<UINT32>(text_.size()));
}

void NumericUpDown::StepBy(float multiple) {
    SetValue(value_ + multiple * step_);
}

// --- Auto-repeat -----------------------------------------------------------

void NumericUpDown::StartRepeat(ButtonPart btn) {
    if (repeating_) return;
    repeating_ = true;
    armed_ = true;
    accumSec_ = 0.0f;
    // Fire once on press so a single click behaves like a plain button; the first
    // repeat is then delayed by kInitialDelay rather than being immediate.
    StepBy(btn == ButtonPart::Up ? 1.0f : -1.0f);
}

void NumericUpDown::StopRepeat() {
    repeating_ = false;
    armed_ = false;
    accumSec_ = 0.0f;
}

void NumericUpDown::OnAnimationTick(float dtSec) {
    if (!repeating_ || pressedButton_ == ButtonPart::None) return;
    accumSec_ += dtSec;

    if (armed_) {
        if (accumSec_ < kInitialDelay) return;
        armed_ = false;
        accumSec_ = 0.0f;
        StepBy(pressedButton_ == ButtonPart::Up ? 1.0f : -1.0f);
        return;
    }

    // A while loop so a long frame does not swallow the repeats due during it,
    // capped so a multi-second stall cannot deliver hundreds at once. Same shape
    // and same reasoning as RepeatButton::OnAnimationTick.
    int fired = 0;
    constexpr int kMaxPerFrame = 4;
    while (accumSec_ >= kInterval && fired < kMaxPerFrame) {
        accumSec_ -= kInterval;
        ++fired;
        StepBy(pressedButton_ == ButtonPart::Up ? 1.0f : -1.0f);
    }
    if (fired == kMaxPerFrame) accumSec_ = 0.0f;
}

} // namespace fluent
