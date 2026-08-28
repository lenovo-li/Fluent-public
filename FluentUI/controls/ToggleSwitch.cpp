// ToggleSwitch.cpp

#include "ToggleSwitch.h"
#include "ToggleVisuals.h"
#include "../styling/ThemeTokens.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kLabelGap = 12.0f;   // gap between track and label (DIP)
constexpr float kKnobPad = 3.0f;     // inset of knob from track edge (DIP)
} // namespace

bool ToggleSwitch::WantsAnimationTick() const {
    // No on-screen state to animate away from before the first paint; see
    // FrameworkElement::AnimationPrimed. Must be here as well as in Render because the
    // host ticks BEFORE rendering, so Render alone would still leak one eased frame.
    if (!AnimationPrimed()) return false;
    return slide_.Animating(IsOn() ? 1.0f : 0.0f, 0.001f);
}

void ToggleSwitch::OnAnimationTick(float dtSec) {
    slide_.Approach(IsOn() ? 1.0f : 0.0f, dtSec, Theme().motion.fadeTau);
    Invalidate();
}

void ToggleSwitch::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    UNREFERENCED_PARAMETER(availW);
    // EffectiveFontSize, not the raw theme token: Render (below) resolves the font
    // the same way, and the two passes disagreeing means the label is PAINTED at a
    // size the layout never reserved room for. SetFontSize(48) on a switch used to
    // measure at bodySize (14) and draw at 48 — a label overflowing its own control,
    // outside the reported dirty bounds. CheckBox and RadioButton always resolved it
    // this way in both passes; this site was the odd one out.
    const float fontSize = EffectiveFontSize(Theme().typography.bodySize);
    float w = TrackW();
    float h = TrackH();
    // Through the shared layout cache, not a fresh CreateTextLayout per Measure:
    // this used to build one throwaway IDWriteTextLayout every frame of a resize
    // drag and read two floats off it. Both dimensions come from ONE cached entry,
    // so reading the height costs nothing beyond the width lookup.
    //
    // Unbounded max height inside MeasureLabelSize is load-bearing here: passing
    // TrackH() would clamp the layout to 20 DIP and make DWrite clip tall glyphs
    // (descenders, CJK). Measure at natural height, then take the max.
    // Weight matches Render's EffectiveFontWeight call, for the same reason the font
    // SIZE here matches EffectiveFontSize: this control already shipped one
    // Measure/Render typography mismatch and it overflowed the label.
    const SizeDip label = MeasureLabelSize(fontSize, 1000.0f,
                                           DWRITE_TEXT_ALIGNMENT_LEADING,
                                           DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                                           EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL));
    if (label.w > 0.0f) {
        w = TrackW() + kLabelGap + label.w;
        h = std::max(TrackH(), label.h);
    }
    SetDesired({IsAuto(width_) ? w : width_, IsAuto(height_) ? h : height_});
}

void ToggleSwitch::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;
    const float fontSize = EffectiveFontSize(th.typography.bodySize);

    float p = std::clamp(static_cast<float>(slide_), 0.0f, 1.0f);

    // ToggleSwitch uses the same checked/unchecked fill resolution as CheckBox/
    // RadioButton, but ignores hover state on the track (per WinUI). The knob's
    // color lerp is unique to this control and stays inline.
    const ToggleAppearance appearance{
        HasBackground()  ? std::optional(EffectiveBackground())  : std::nullopt,
        HasAccentColor() ? std::optional(EffectiveAccentColor()) : std::nullopt,
        HasForeground()  ? std::optional(EffectiveForeground())  : std::nullopt,
        BackgroundHover(), BackgroundPressed(),
        AccentColorHover(), AccentColorPressed(),
    };

    // Vertically center the track within our bounds.
    float trackY = bounds_.y + (bounds_.h - TrackH()) * 0.5f;
    D2D1_RECT_F track = D2D1::RectF(bounds_.x, trackY,
                                    bounds_.x + TrackW(), trackY + TrackH());
    float radius = TrackH() * 0.5f;
    D2D1_ROUNDED_RECT trr = D2D1::RoundedRect(track, radius, radius);

    // Track fill crossfades: off = neutral control fill w/ border; on = accent.
    D2D1_COLOR_F offFill = ToggleUncheckedFill(State(), appearance, pal);
    dc.FillRoundedRect(trr, offFill);
    if (p > 0.0f) {
        D2D1_COLOR_F onFill = ToggleCheckedFill(State(), appearance, pal);
        onFill.a = p;
        dc.FillRoundedRect(trr, onFill);
    }
    // Border: fades out as the accent fills in (off state shows a thin border).
    // See CheckBox: an off track is mostly outline, so it must fade when disabled.
    D2D1_COLOR_F borderC = ToggleBorderColor(State(), appearance,
                                                    EffectiveBorderBrush(pal.controlStrokeDefault));
    borderC.a *= (1.0f - p);
    if (borderC.a > 0.01f)
        dc.DrawRoundedRect(trr, borderC, EffectiveBorderThickness(1.0f));

    // Knob: a circle that slides from the left inset to the right inset. On the
    // off state it is the dim text color; on the on state, on-accent.
    float knobR = radius - kKnobPad;
    float leftX = track.left + kKnobPad + knobR;
    float rightX = track.right - kKnobPad - knobR;
    float knobX = leftX + (rightX - leftX) * p;
    float knobY = trackY + TrackH() * 0.5f;

    D2D1_COLOR_F offKnob = th.dark ? D2D1::ColorF(0.75f, 0.75f, 0.75f)
                                    : D2D1::ColorF(0.35f, 0.35f, 0.35f);
    // The knob's "on" tone uses the shared mark color (follows explicit foreground).
    const D2D1_COLOR_F onKnob = ToggleMarkColor(State(), appearance, pal);
    // Lerp knob color from off tone to the on tone as it slides.
    D2D1_COLOR_F knobC = {
        offKnob.r + (onKnob.r - offKnob.r) * p,
        offKnob.g + (onKnob.g - offKnob.g) * p,
        offKnob.b + (onKnob.b - offKnob.b) * p,
        1.0f,
    };
    // The knob grows slightly at mid-slide for a lively feel (Fluent-ish).
    float grow = 1.0f + 0.12f * std::sin(p * 3.14159265f);
    dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, knobY),
                                 knobR * grow, knobR * grow), knobC);

    // Focus ring: an accent outline around the track when focused.
    if (IsFocused()) {
        const float g = kFocusRingGap;
        D2D1_ROUNDED_RECT fr = D2D1::RoundedRect(
            D2D1::RectF(track.left - g, track.top - g,
                        track.right + g, track.bottom + g),
            radius + g, radius + g);
        dc.DrawRoundedRect(fr, EffectiveAccentColor(pal.accent), kFocusRingStroke);
    }

    // Label to the right of the track.
    if (!text_.empty() && Dwrite()) {
        auto weight = EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL);
        if (IDWriteTextFormat* fmt = Dwrite()->Format(
                fontSize, weight, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP)) {
            dc.DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                        D2D1::RectF(bounds_.x + TrackW() + kLabelGap, bounds_.y,
                                    bounds_.right(), bounds_.bottom()),
                        ToggleLabelColor(State(), appearance, pal), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
}

} // namespace fluent
