// Rating.cpp — see header for the design rationale.

#include "Rating.h"
#include "../graphics/DWriteContext.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include <algorithm>

namespace fluent {

namespace {
// U+2605 BLACK STAR / U+2606 WHITE STAR. Drawn as text rather than as a vector
// path: the two glyphs already differ by exactly the fill/outline distinction the
// control needs, and Segoe UI (the theme's default family) ships both. A geometry
// version would need its own path data plus a stroke/fill decision per star, for
// the same pixels.
constexpr wchar_t kFilledStar = L'\x2605';
constexpr wchar_t kEmptyStar  = L'\x2606';
} // namespace

void Rating::SetValue(float v) {
    SetProperty(value_, std::clamp(v, 0.0f, static_cast<float>(maxValue_)),
                DirtyFlags::Render);
}

void Rating::SetMaxValue(int n) {
    const int clamped = std::max(1, n);
    if (maxValue_ == clamped) return;
    maxValue_ = clamped;
    // Shrinking the star count must not leave a value above the new maximum.
    value_ = std::clamp(value_, 0.0f, static_cast<float>(maxValue_));
    hoverValue_ = kNoHover;  // the hover was computed against the old glyph count
    InvalidateMeasure();
}

void Rating::SetIconSize(float dip) {
    SetProperty(iconSize_, std::clamp(dip, 12.0f, 48.0f), DirtyFlags::Measure);
}

void Rating::SetReadOnly(bool ro) {
    if (readOnly_ == ro) return;
    readOnly_ = ro;
    // A read-only Rating is display only: drop out of the Tab order and clear any
    // preview that was on screen when the mode flipped.
    SetFocusable(!ro);
    hoverValue_ = kNoHover;
    Invalidate();
}

float Rating::Gap() const {
    return Theme().spacing.spacingXSmall;
}

void Rating::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availW);
    UNREFERENCED_PARAMETER(availH);
    const float w = maxValue_ * iconSize_ + (maxValue_ - 1) * Gap();
    SetDesired({IsAuto(width_)  ? w : width_,
                IsAuto(height_) ? iconSize_ : height_});
}

void Rating::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;

    // The hover preview replaces the committed value while the pointer is over the
    // control, so the user sees the rating they are about to commit.
    const bool previewing = hoverValue_ >= 0.0f;
    const float shown = previewing ? hoverValue_ : value_;

    // Filled stars take the accent; a preview uses accentHover so the difference
    // between "this is your rating" and "this is what you would get" is visible.
    // Disabled collapses both to textSecondary — the shape still reads, the
    // affordance does not.
    const D2D1_COLOR_F filled = enabled_
        ? (previewing ? pal.accentHover
                      : EffectiveAccentColor(pal.accent))
        : pal.textSecondary;
    const D2D1_COLOR_F empty = pal.controlStrokeDefault;

    DWriteContext* dw = Dwrite();
    if (!dw) return;
    IDWriteTextFormat* fmt = dw->Format(iconSize_,
                                        EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL),
                                        DWRITE_TEXT_ALIGNMENT_CENTER);
    if (!fmt) return;

    // A fraction of at least 0.5 fills the boundary glyph. Below that it stays
    // empty, so a 3.4 average shows three stars and a 3.6 shows four.
    const int whole = static_cast<int>(shown);
    const bool halfFills = (shown - static_cast<float>(whole)) >= 0.5f;

    const float gap = Gap();
    float x = bounds_.x;
    for (int i = 0; i < maxValue_; ++i) {
        const bool isFilled = (i < whole) || (i == whole && halfFills);
        const wchar_t glyph = isFilled ? kFilledStar : kEmptyStar;
        dc.DrawText(&glyph, 1, fmt,
                    D2D1::RectF(x, bounds_.y, x + iconSize_, bounds_.y + iconSize_),
                    isFilled ? filled : empty);
        x += iconSize_ + gap;
    }

    // One ring around the whole row rather than per glyph: the control is a single
    // focus target. Drawn last but outside bounds_, which VisualOverflowDip covers.
    if (IsFocused() && !readOnly_)
        DrawFocusRing(dc, bounds_, pal, FocusRingSpec{});
}

float Rating::HitTestValue(float dipX) const {
    const float local = dipX - bounds_.x;
    if (local < 0.0f) return 0.0f;

    const float stride = iconSize_ + Gap();
    const int index = static_cast<int>(local / stride);
    if (index >= maxValue_) return static_cast<float>(maxValue_);

    // Inside the glyph: that star's 1-based value. In the gap after it: the same
    // value, because the gap belongs to the glyph the pointer just left — treating
    // it as the NEXT star would let a click land one higher than the star under
    // the cursor, which reads as an off-by-one to the user.
    return static_cast<float>(index + 1);
}

void Rating::OnPointerMoved(PointerEventArgs& e) {
    if (readOnly_ || !enabled_) return;
    SetProperty(hoverValue_, HitTestValue(e.position.x), DirtyFlags::Render);
}

void Rating::OnPointerLeft() {
    // Not gated on readOnly_: the flag can flip while the pointer is inside, and
    // the preview still has to be cleared on the way out.
    SetProperty(hoverValue_, kNoHover, DirtyFlags::Render);
}

void Rating::OnClickRouted(PointerEventArgs& e) {
    if (readOnly_ || !enabled_) return;
    const float clicked = HitTestValue(e.position.x);
    // Clicking the star that already represents the current value clears the
    // rating. Without this there is no way to get back to "unrated" with the
    // mouse, since every glyph maps to a value of 1 or more.
    CommitValue(clicked == value_ ? 0.0f : clicked);
    e.handled = true;
}

void Rating::OnKeyDownRouted(KeyEventArgs& e) {
    if (readOnly_ || !enabled_) return;

    float next = value_;
    switch (e.vk) {
        case VK_LEFT:
        case VK_DOWN:  next = std::max(0.0f, value_ - 1.0f); break;
        case VK_RIGHT:
        case VK_UP:    next = std::min(static_cast<float>(maxValue_), value_ + 1.0f); break;
        case VK_HOME:  next = 0.0f; break;
        case VK_END:   next = static_cast<float>(maxValue_); break;
        default: return;  // unhandled: let the event keep bubbling
    }
    CommitValue(next);
    e.handled = true;
}

void Rating::CommitValue(float newValue) {
    if (value_ == newValue) return;
    value_ = newValue;
    Invalidate();
    valueChanged_.Raise(*this, newValue);
}

} // namespace fluent
