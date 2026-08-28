// CheckBox.cpp — Fluent check box with animated check transition.

#include "CheckBox.h"
#include "ToggleVisuals.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include <algorithm>
#include <cmath>

namespace fluent {

bool CheckBox::WantsAnimationTick() const {
    // Before the first paint there is no on-screen state to animate away from, so report
    // settled. Without this the host would collect and tick this element on the very frame
    // that Render is about to snap it -- the tick runs BEFORE Render in the frame, so
    // snapping in Render alone would still leak one eased frame.
    if (!AnimationPrimed()) return false;
    return checkAnim_.Animating(IsChecked() ? 1.0f : 0.0f, 0.01f);
}

void CheckBox::OnAnimationTick(float dtSec) {
    checkAnim_.Approach(IsChecked() ? 1.0f : 0.0f, dtSec, Theme().motion.fadeTau);
    // Host renders once after ticking all elements; no self-invalidate.
}

void CheckBox::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    // Width = box + gap + label; height = the box (label centers within it).
    const float fontSize = Theme().typography.bodySize;
    const float labelGap = Theme().spacing.spacingMedium;
    // Same weight Render draws with, so SetFontWeight(SEMI_BOLD) widens the measured
    // label instead of overflowing it.
    float labelW = MeasureLabelWidth(fontSize, availW, BoxSize(),
                                     EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL));
    float w = BoxSize() + (labelW > 0.0f ? labelGap + labelW : 0.0f);
    SetDesired({IsAuto(width_) ? w : width_,
                     IsAuto(height_) ? std::max(BoxSize(), fontSize + 6.0f) : height_});
}

void CheckBox::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;
    const float boxCorner = EffectiveCornerRadius(th.spacing.cornerRadiusSmall);
    const float labelGap = th.spacing.spacingMedium;
    const float fontSize = EffectiveFontSize(th.typography.bodySize);

    const float box = BoxSize();
    const float boxX = bounds_.x;
    const float boxY = bounds_.y + (bounds_.h - box) * 0.5f;
    D2D1_RECT_F boxRect = D2D1::RectF(boxX, boxY, boxX + box, boxY + box);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(boxRect, boxCorner, boxCorner);

    const float p = std::clamp(static_cast<float>(checkAnim_), 0.0f, 1.0f);

    // Colour resolution goes through the shared pure functions (ToggleVisuals.h) so
    // it is unit-testable and cannot drift from RadioButton / ToggleSwitch, which
    // resolve the same three colours from the same inputs.
    const ToggleAppearance appearance{
        HasBackground()  ? std::optional(EffectiveBackground())  : std::nullopt,
        HasAccentColor() ? std::optional(EffectiveAccentColor()) : std::nullopt,
        HasForeground()  ? std::optional(EffectiveForeground())  : std::nullopt,
        BackgroundHover(), BackgroundPressed(),
        AccentColorHover(), AccentColorPressed(),
    };

    // Fill: interpolate from empty (control fill) to accent as the check grows.
    // The alpha ramp is the animation, not a colour decision, so it stays here.
    if (p > 0.01f) {
        D2D1_COLOR_F fill = ToggleCheckedFill(State(), appearance, pal);
        fill.a = p;
        dc.FillRoundedRect(rr, fill);
    }
    if (p < 0.99f) {
        D2D1_COLOR_F empty = ToggleUncheckedFill(State(), appearance, pal);
        empty.a *= (1.0f - p);
        dc.FillRoundedRect(rr, empty);
    }
    // Border
    // Fades with the rest when disabled: on an UNCHECKED box the outline is almost the
    // only thing drawn, so leaving it full-strength makes a disabled empty checkbox
    // indistinguishable from a live one.
    D2D1_COLOR_F borderColor = ToggleBorderColor(State(), appearance,
                                                        EffectiveBorderBrush(pal.controlStrokeDefault));
    borderColor.a = borderColor.a * (1.0f - p);
    const float borderThk = EffectiveBorderThickness(1.2f);
    dc.DrawRoundedRect(rr, borderColor, borderThk);

    // Checkmark
    if (p > 0.02f) {
        D2D1_POINT_2F a = D2D1::Point2F(boxX + box * 0.26f, boxY + box * 0.52f);
        D2D1_POINT_2F b = D2D1::Point2F(boxX + box * 0.44f, boxY + box * 0.70f);
        D2D1_POINT_2F c = D2D1::Point2F(boxX + box * 0.76f, boxY + box * 0.32f);
        float stroke = 2.0f;
        float p1 = std::clamp(p / 0.4f, 0.0f, 1.0f);
        D2D1_POINT_2F b1 = D2D1::Point2F(a.x + (b.x - a.x) * p1, a.y + (b.y - a.y) * p1);
        D2D1_COLOR_F checkColor = ToggleMarkColor(State(), appearance, pal);
        dc.DrawLine(a, b1, checkColor, stroke);
        if (p > 0.4f) {
            float p2 = std::clamp((p - 0.4f) / 0.6f, 0.0f, 1.0f);
            D2D1_POINT_2F c1 = D2D1::Point2F(b.x + (c.x - b.x) * p2, b.y + (c.y - b.y) * p2);
            dc.DrawLine(b, c1, checkColor, stroke);
        }
    }

    // Label
    if (Dwrite() && !text_.empty()) {
        auto weight = EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL);
        if (IDWriteTextFormat* fmt = Dwrite()->Format(fontSize, weight,
                                                       DWRITE_TEXT_ALIGNMENT_LEADING)) {
            D2D1_COLOR_F labelColor = ToggleLabelColor(State(), appearance, pal);
            dc.DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                        D2D1::RectF(boxX + box + labelGap, bounds_.y,
                                    bounds_.right(), bounds_.bottom()),
                        labelColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    // Focus ring
    if (IsFocused()) {
        RectDip boxDip{boxX, boxY, box, box};
        FocusRingSpec spec;
        spec.inset = kFocusRingInset;
        spec.cornerRadius = boxCorner;
        DrawFocusRing(dc, boxDip, pal, spec);
    }
}

float CheckBox::VisualOverflowDip() const {
    // The focus ring surrounds the BOX, which starts at bounds_.x and is centered
    // vertically — so it overhangs bounds_ on the left, and top/bottom too whenever
    // bounds_.h is no taller than the box. A uniform pad is a superset of the ring
    // rect and costs a few DIP.
    FocusRingSpec spec;
    spec.inset = kFocusRingInset;  // shared with Render()
    return FocusRingPadDip(spec);
}

} // namespace fluent
