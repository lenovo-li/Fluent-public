// Control.cpp — Control property layer implementations (2026-08-08).
//
// All EffectiveX() implementations live here so they can see the complete ThemeSnapshot.
// The pattern is uniform: user override wins, fallback (theme token or control default)
// is used when the optional is empty.

#include "Control.h"
#include "../styling/ThemeTokens.h"

namespace fluent {

// Helper for color comparison (D2D1_COLOR_F has no operator==)
static bool ColorEquals(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// ===== Foreground =====

void Control::SetForeground(const D2D1_COLOR_F& c) {
    if (!foreground_.has_value() || !ColorEquals(foreground_.value(), c)) {
        foreground_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearForeground() {
    if (foreground_.has_value()) {
        foreground_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

D2D1_COLOR_F Control::EffectiveForeground() const {
    return foreground_.value_or(Theme().colors.textPrimary);
}

D2D1_COLOR_F Control::EffectiveForeground(const D2D1_COLOR_F& fallback) const {
    return foreground_.value_or(fallback);
}

// ===== Background =====

void Control::SetBackground(const D2D1_COLOR_F& c) {
    if (!background_.has_value() || !ColorEquals(background_.value(), c)) {
        background_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearBackground() {
    if (background_.has_value()) {
        background_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

D2D1_COLOR_F Control::EffectiveBackground() const {
    return background_.value_or(Theme().colors.controlFillDefault);
}

D2D1_COLOR_F Control::EffectiveBackground(const D2D1_COLOR_F& fallback) const {
    return background_.value_or(fallback);
}

// ===== Interaction-state colours =====
//
// Four near-identical setter/clearer pairs. Each is Render-level: a state colour cannot
// change a desired size, only what gets painted when the pointer is over the control.
// The resolution logic (explicit -> derived -> theme) lives in the per-control colour
// functions, not here, because only the control knows which of its parts each slot
// applies to (a Button's fill vs a CheckBox's box).

void Control::SetBackgroundHover(const D2D1_COLOR_F& c) {
    if (!backgroundHover_.has_value() || !ColorEquals(backgroundHover_.value(), c)) {
        backgroundHover_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearBackgroundHover() {
    if (backgroundHover_.has_value()) {
        backgroundHover_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::SetBackgroundPressed(const D2D1_COLOR_F& c) {
    if (!backgroundPressed_.has_value() || !ColorEquals(backgroundPressed_.value(), c)) {
        backgroundPressed_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearBackgroundPressed() {
    if (backgroundPressed_.has_value()) {
        backgroundPressed_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::SetAccentColorHover(const D2D1_COLOR_F& c) {
    if (!accentHover_.has_value() || !ColorEquals(accentHover_.value(), c)) {
        accentHover_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearAccentColorHover() {
    if (accentHover_.has_value()) {
        accentHover_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::SetAccentColorPressed(const D2D1_COLOR_F& c) {
    if (!accentPressed_.has_value() || !ColorEquals(accentPressed_.value(), c)) {
        accentPressed_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearAccentColorPressed() {
    if (accentPressed_.has_value()) {
        accentPressed_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

// ===== BorderBrush =====

void Control::SetBorderBrush(const D2D1_COLOR_F& c) {
    if (!borderBrush_.has_value() || !ColorEquals(borderBrush_.value(), c)) {
        borderBrush_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearBorderBrush() {
    if (borderBrush_.has_value()) {
        borderBrush_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

D2D1_COLOR_F Control::EffectiveBorderBrush() const {
    return borderBrush_.value_or(Theme().colors.controlStrokeDefault);
}

D2D1_COLOR_F Control::EffectiveBorderBrush(const D2D1_COLOR_F& fallback) const {
    return borderBrush_.value_or(fallback);
}

// ===== AccentColor =====

void Control::SetAccentColor(const D2D1_COLOR_F& c) {
    if (!accentColor_.has_value() || !ColorEquals(accentColor_.value(), c)) {
        accentColor_ = c;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearAccentColor() {
    if (accentColor_.has_value()) {
        accentColor_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

D2D1_COLOR_F Control::EffectiveAccentColor() const {
    return accentColor_.value_or(Theme().colors.accent);
}

D2D1_COLOR_F Control::EffectiveAccentColor(const D2D1_COLOR_F& fallback) const {
    return accentColor_.value_or(fallback);
}

// ===== CornerRadius =====

void Control::SetCornerRadius(float dip) {
    if (!cornerRadius_.has_value() || cornerRadius_.value() != dip) {
        cornerRadius_ = dip;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearCornerRadius() {
    if (cornerRadius_.has_value()) {
        cornerRadius_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

float Control::EffectiveCornerRadius() const {
    return cornerRadius_.value_or(Theme().spacing.cornerRadiusSmall);
}

float Control::EffectiveCornerRadius(float fallback) const {
    return cornerRadius_.value_or(fallback);
}

// ===== BorderThickness =====

void Control::SetBorderThickness(float dip) {
    if (!borderThickness_.has_value() || borderThickness_.value() != dip) {
        borderThickness_ = dip;
        InvalidateDirty(DirtyFlags::Render);
    }
}

void Control::ClearBorderThickness() {
    if (borderThickness_.has_value()) {
        borderThickness_.reset();
        InvalidateDirty(DirtyFlags::Render);
    }
}

float Control::EffectiveBorderThickness(float fallback) const {
    return borderThickness_.value_or(fallback);
}

float Control::EffectiveBorderThickness() const {
    return borderThickness_.value_or(Theme().spacing.borderWidth);
}

// ===== FontSize =====

void Control::SetFontSize(float dip) {
    if (!fontSize_.has_value() || fontSize_.value() != dip) {
        fontSize_ = dip;
        InvalidateDirty(DirtyFlags::Measure);  // text size affects layout
    }
}

void Control::ClearFontSize() {
    if (fontSize_.has_value()) {
        fontSize_.reset();
        InvalidateDirty(DirtyFlags::Measure);
    }
}

float Control::EffectiveFontSize() const {
    return fontSize_.value_or(Theme().typography.bodySize);
}

float Control::EffectiveFontSize(float fallback) const {
    return fontSize_.value_or(fallback);
}

// ===== FontWeight =====

void Control::SetFontWeight(DWRITE_FONT_WEIGHT w) {
    if (!fontWeight_.has_value() || fontWeight_.value() != w) {
        fontWeight_ = w;
        InvalidateDirty(DirtyFlags::Measure);  // bolder face measures wider
    }
}

void Control::ClearFontWeight() {
    if (fontWeight_.has_value()) {
        fontWeight_.reset();
        InvalidateDirty(DirtyFlags::Measure);
    }
}

DWRITE_FONT_WEIGHT Control::EffectiveFontWeight(DWRITE_FONT_WEIGHT fallback) const {
    return fontWeight_.value_or(fallback);
}

} // namespace fluent
