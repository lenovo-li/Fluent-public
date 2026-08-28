// Slider.cpp

#include "Slider.h"
#include "../styling/ThemeTokens.h"
#include "../input/InputManager.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kTrackH  = 4.0f;   // track height (DIP)
constexpr float kThumbR  = 8.0f;   // thumb radius (DIP)
constexpr float kAnimTau = 0.04f;  // fill animation time constant (s) — kept local; ProgressBar WP-06
// Horizontal padding so the thumb never draws outside bounds_ — it must be at
// least the thumb's visual radius (including drag scale and focus ring), or the
// left/right edges get clipped. The old value (kThumbR = 8) was too small:
// VisualOverflowDip returns ~14.2, so the track ends need that much clearance.
constexpr float kPadX    = kThumbR * 1.15f + 3.5f + 1.5f;
constexpr float kPadY    = kPadX;  // vertical orientation needs the same padding
} // namespace

// ---------------------------------------------------------------------------
// RangeBase overrides
// ---------------------------------------------------------------------------

float Slider::CoerceValue(float v) const {
    float range = max_ - min_;
    if (range <= 0.0f) return min_;
    // Clamp first, then snap to the nearest step grid.
    v = std::clamp(v, min_, max_);
    float snapped = std::round((v - min_) / step_) * step_ + min_;
    return std::clamp(snapped, min_, max_);
}

// ---------------------------------------------------------------------------
// Step helper
// ---------------------------------------------------------------------------

void Slider::StepBy(float delta) {
    SetValue(value_ + delta * step_);
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

float Slider::TrackLeft()  const { return bounds_.x + kPadX; }
float Slider::TrackRight() const { return bounds_.right() - kPadX; }
float Slider::TrackY()     const { return bounds_.y + bounds_.h * 0.5f; }
float Slider::TrackTop()   const { return bounds_.y + kPadY; }
float Slider::TrackBottom() const { return bounds_.bottom() - kPadY; }
float Slider::TrackX()     const { return bounds_.x + bounds_.w * 0.5f; }

float Slider::ValueFromX(float dipX) const {
    float range = max_ - min_;
    if (range <= 0.0f) return min_;
    float t = std::clamp((dipX - TrackLeft()) / (TrackRight() - TrackLeft()), 0.0f, 1.0f);
    return t * range + min_;
}

float Slider::ValueFromY(float dipY) const {
    float range = max_ - min_;
    if (range <= 0.0f) return min_;
    // Vertical: top = max, bottom = min (inverted Y)
    float t = std::clamp((TrackBottom() - dipY) / (TrackBottom() - TrackTop()), 0.0f, 1.0f);
    return t * range + min_;
}

float Slider::NormalizedAnim() const {
    float range = max_ - min_;
    if (range <= 0.0f) return 0.0f;
    return std::clamp((static_cast<float>(animFill_) - min_) / range, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Animation tick
// ---------------------------------------------------------------------------

bool Slider::WantsAnimationTick() const {
    // A Slider built at value 50 must show 50 on its first frame, not sweep up from the
    // minimum; see FrameworkElement::AnimationPrimed. Also needed here and not only in
    // Render because the host ticks before it renders.
    if (!AnimationPrimed()) return false;
    return animFill_.Animating(value_, 0.001f * (max_ - min_ + 1.0f));
}

void Slider::OnAnimationTick(float dtSec) {
    animFill_.Approach(value_, dtSec, kAnimTau, 0.01f * (max_ - min_ + 1.0f));
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void Slider::OnKeyDownRouted(KeyEventArgs& e) {
    switch (e.vk) {
        case VK_LEFT:  StepBy(-1); e.handled = true; break;
        case VK_RIGHT: StepBy(+1); e.handled = true; break;
        case VK_DOWN:  StepBy(-1); e.handled = true; break;
        case VK_UP:    StepBy(+1); e.handled = true; break;
        case VK_HOME:  SetValue(min_); e.handled = true; break;
        case VK_END:   SetValue(max_); e.handled = true; break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Mouse (routed): capture on press so a drag beyond the track keeps updating.
// ---------------------------------------------------------------------------

void Slider::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    dragging_ = true;
    pointerDown_ = true;
    if (Context().input) Context().input->CapturePointer(this);
    UpdateState();
    SetValue(orientation_ == Orientation::Vertical ? ValueFromY(e.position.y)
                                                  : ValueFromX(e.position.x));
    // Phase 1B: while dragging, the fill/thumb must track the pointer 1:1. The
    // eased animFill_ (tau 0.04s) would lag behind the cursor — a rubber-band
    // stutter. Snap the animated value straight to the (coerced) target so there
    // is no easing during the drag; keyboard/programmatic SetValue still eases.
    animFill_.SetImmediate(value_);
    e.handled = true;
}

void Slider::OnPointerMoved(PointerEventArgs& e) {
    if (!dragging_) return;
    SetValue(orientation_ == Orientation::Vertical ? ValueFromY(e.position.y)
                                                  : ValueFromX(e.position.x));
    animFill_.SetImmediate(value_);  // Phase 1B: track the pointer 1:1 (no easing)
    e.handled = true;
}

void Slider::OnPointerReleased(PointerEventArgs& e) {
    if (!dragging_) return;
    dragging_ = false;
    pointerDown_ = false;
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    UpdateState();
    e.handled = true;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void Slider::Measure(float availW, float availH) {
    if (orientation_ == Orientation::Vertical) {
        SetDesired({IsAuto(width_)  ? (kThumbR * 2.0f + 4.0f) : width_,
                         IsAuto(height_) ? (availH > 0 ? availH : 120.0f) : height_});
    } else {
        SetDesired({IsAuto(width_)  ? (availW > 0 ? availW : 120.0f) : width_,
                         IsAuto(height_) ? (kThumbR * 2.0f + 4.0f) : height_});
    }
}

float Slider::VisualOverflowDip() const {
    // The thumb is drawn centered on the track at TrackY(), radius up to
    // kThumbR*1.15 while dragging, plus a focus ring at radius+3.5. That extends
    // above/below (and, at the track ends, left/right of) the layout bounds. If
    // the dirty rect were just bounds_, a partial redraw would clip the thumb
    // (the flattened "capsule" in the drag screenshot) and leave ring residue.
    return kThumbR * 1.15f + 3.5f + 1.5f;  // thumb(drag) + ring + stroke
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void Slider::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;

    if (orientation_ == Orientation::Vertical) {
        // --- Vertical orientation ---
        const float top    = TrackTop();
        const float bottom = TrackBottom();
        const float tx     = TrackX();
        const float fill   = bottom - (bottom - top) * NormalizedAnim();

        // --- Track groove (unfilled portion) ---
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(tx - kTrackH*0.5f, top,
                                          tx + kTrackH*0.5f, bottom),
                              kTrackH*0.5f, kTrackH*0.5f),
            EffectiveBackground(th.dark ? D2D1::ColorF(1,1,1, 0.18f)
                                        : D2D1::ColorF(0,0,0, 0.12f)));

        // --- Filled portion (accent) ---
        if (fill < bottom - 0.5f) {
            D2D1_COLOR_F ac = EffectiveAccentColor(
                (State() == VisualState::Pressed) ? pal.accentPressed : pal.accent);
            dc.FillRoundedRect(
                D2D1::RoundedRect(D2D1::RectF(tx - kTrackH*0.5f, fill,
                                              tx + kTrackH*0.5f, bottom),
                                  kTrackH*0.5f, kTrackH*0.5f), ac);
        }

        // --- Thumb circle ---
        float thumbY = bottom - (bottom - top) * NormalizedAnim();
        bool hover = State() == VisualState::Hover || State() == VisualState::Pressed;
        float thumbR = dragging_ ? kThumbR * 1.15f : (hover ? kThumbR * 1.08f : kThumbR);

        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, thumbY), thumbR, thumbR),
                       EffectiveForeground(D2D1::ColorF(1,1,1)));

        D2D1_COLOR_F thumbBorder = EffectiveBorderBrush(pal.accent);
        thumbBorder.a = dragging_ ? 1.0f : 0.85f;
        dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(tx, thumbY), thumbR, thumbR),
                       thumbBorder, EffectiveBorderThickness(1.5f));

        // --- Focus ring ---
        if (IsFocused()) {
            const D2D1_COLOR_F ring = EffectiveAccentColor(pal.accent);
            dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(tx, thumbY),
                                         thumbR + 3.5f, thumbR + 3.5f),
                           D2D1::ColorF(ring.r, ring.g, ring.b, 0.6f), 1.5f);
        }
    } else {
        // --- Horizontal orientation ---
        const float left  = TrackLeft();
        const float right = TrackRight();
        const float ty    = TrackY();
        const float fill  = left + (right - left) * NormalizedAnim();

        // --- Track groove (unfilled portion) ---
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(left, ty - kTrackH*0.5f,
                                          right, ty + kTrackH*0.5f),
                              kTrackH*0.5f, kTrackH*0.5f),
            EffectiveBackground(th.dark ? D2D1::ColorF(1,1,1, 0.18f)
                                        : D2D1::ColorF(0,0,0, 0.12f)));

        // --- Filled portion (accent) ---
        if (fill > left + 0.5f) {
            D2D1_COLOR_F ac = EffectiveAccentColor(
                (State() == VisualState::Pressed) ? pal.accentPressed : pal.accent);
            dc.FillRoundedRect(
                D2D1::RoundedRect(D2D1::RectF(left, ty - kTrackH*0.5f,
                                              fill, ty + kTrackH*0.5f),
                                  kTrackH*0.5f, kTrackH*0.5f), ac);
        }

        // --- Thumb circle ---
        float thumbX = left + (right - left) * NormalizedAnim();
        bool hover = State() == VisualState::Hover || State() == VisualState::Pressed;
        float thumbR = dragging_ ? kThumbR * 1.15f : (hover ? kThumbR * 1.08f : kThumbR);

        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, ty), thumbR, thumbR),
                       EffectiveForeground(D2D1::ColorF(1,1,1)));

        D2D1_COLOR_F thumbBorder = EffectiveBorderBrush(pal.accent);
        thumbBorder.a = dragging_ ? 1.0f : 0.85f;
        dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, ty), thumbR, thumbR),
                       thumbBorder, EffectiveBorderThickness(1.5f));

        // --- Focus ring ---
        if (IsFocused()) {
            const D2D1_COLOR_F ring = EffectiveAccentColor(pal.accent);
            dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, ty),
                                         thumbR + 3.5f, thumbR + 3.5f),
                           D2D1::ColorF(ring.r, ring.g, ring.b, 0.6f), 1.5f);
        }
    }
}

} // namespace fluent
