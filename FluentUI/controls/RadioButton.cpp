// RadioButton.cpp

#include "RadioButton.h"
#include "ToggleVisuals.h"
#include "../styling/ThemeTokens.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kLabelGap = 10.0f;   // gap between ring and label (DIP)
} // namespace

void RadioButton::OnActivate() {
    if (!group_ || *group_ == value_) return;
    *group_ = value_;
    int v = value_;
    selected_.Raise(*this, v);
    Invalidate();
}

bool RadioButton::WantsAnimationTick() const {
    // No on-screen state to animate away from before the first paint; see
    // FrameworkElement::AnimationPrimed. Must be here as well as in Render because the
    // host ticks BEFORE rendering, so Render alone would still leak one eased frame.
    if (!AnimationPrimed()) return false;
    return dotAnim_.Animating(IsSelected() ? 1.0f : 0.0f, 0.001f);
}

void RadioButton::OnAnimationTick(float dtSec) {
    dotAnim_.Approach(IsSelected() ? 1.0f : 0.0f, dtSec, Theme().motion.fadeTau);
    // Host renders once after ticking all elements; no self-invalidate.
}

void RadioButton::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    const float fontSize = Theme().typography.bodySize;
    // Same weight Render draws with (see the label DrawText), so a user-set weight
    // widens the measurement instead of overflowing the arranged box.
    float labelW = MeasureLabelWidth(fontSize, availW, RingSize(),
                                     EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL));
    float w = RingSize() + (labelW > 0.0f ? kLabelGap + labelW : 0.0f);
    SetDesired({IsAuto(width_) ? w : width_,
                     IsAuto(height_) ? std::max(RingSize(), fontSize + 6.0f) : height_});
}

void RadioButton::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;
    const float fontSize = EffectiveFontSize(th.typography.bodySize);

    const float ring = RingSize();
    const float cx = bounds_.x + ring * 0.5f;
    const float cy = bounds_.y + bounds_.h * 0.5f;
    const float outerR = ring * 0.5f;
    const float p = std::clamp(static_cast<float>(dotAnim_), 0.0f, 1.0f);

    // Colour resolution goes through the shared pure functions (ToggleVisuals.h) —
    // identical inputs and precedence to CheckBox, only the geometry differs.
    const ToggleAppearance appearance{
        HasBackground()  ? std::optional(EffectiveBackground())  : std::nullopt,
        HasAccentColor() ? std::optional(EffectiveAccentColor()) : std::nullopt,
        HasForeground()  ? std::optional(EffectiveForeground())  : std::nullopt,
        BackgroundHover(), BackgroundPressed(),
        AccentColorHover(), AccentColorPressed(),
    };

    // Filled background: neutral -> accent
    D2D1_COLOR_F empty = ToggleUncheckedFill(State(), appearance, pal);
    dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), outerR, outerR), empty);
    if (p > 0.01f) {
        D2D1_COLOR_F fill = ToggleCheckedFill(State(), appearance, pal);
        fill.a = p;
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), outerR, outerR), fill);
    }
    // Outer ring border
    // See CheckBox: an unselected ring is mostly outline, so it must fade when disabled.
    D2D1_COLOR_F borderColor = ToggleBorderColor(State(), appearance,
                                                        EffectiveBorderBrush(pal.controlStrokeDefault));
    borderColor.a *= (1.0f - p);
    const float borderThk = EffectiveBorderThickness(1.2f);
    dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), outerR, outerR), borderColor, borderThk);

    // Inner dot
    if (p > 0.02f) {
        float dotR = outerR * 0.42f * p;
        D2D1_COLOR_F dotColor = ToggleMarkColor(State(), appearance, pal);
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), dotR, dotR), dotColor);
    }

    // Label
    if (Dwrite() && !text_.empty()) {
        auto weight = EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL);
        if (IDWriteTextFormat* fmt = Dwrite()->Format(fontSize, weight,
                                                       DWRITE_TEXT_ALIGNMENT_LEADING)) {
            D2D1_COLOR_F labelColor = ToggleLabelColor(State(), appearance, pal);
            dc.DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                        D2D1::RectF(bounds_.x + ring + kLabelGap, bounds_.y,
                                    bounds_.right(), bounds_.bottom()),
                        labelColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    // Focus ring.
    if (IsFocused()) {
        const float r = outerR + kFocusRingGap;
        dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r),
                       pal.accent, kFocusRingStroke);
    }
}


} // namespace fluent
