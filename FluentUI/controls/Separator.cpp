// Separator.cpp

#include "Separator.h"
#include "../styling/ThemeTokens.h"

namespace fluent {

Separator::Separator() {
    // Default to Stretch so a horizontal separator in a vertical StackPanel spans
    // the full width, and vice versa. The user can override with SetHAlign.
    SetHAlign(HAlign::Stretch);
    SetVAlign(VAlign::Stretch);
}

void Separator::SetOrientation(Orientation o) {
    SetProperty(orientation_, o, DirtyFlags::Measure);
}

void Separator::SetThickness(float t) {
    SetProperty(thickness_, t, DirtyFlags::Measure);
}

void Separator::Measure(float availW, float availH) {
    // Horizontal: thin in height, stretch width (clamp to avail when Stretch).
    // Vertical: thin in width, stretch height.
    float w, h;
    if (orientation_ == Orientation::Horizontal) {
        w = IsAuto(width_) ? availW : width_;
        h = IsAuto(height_) ? thickness_ : height_;
    } else {
        w = IsAuto(width_) ? thickness_ : width_;
        h = IsAuto(height_) ? availH : height_;
    }
    SetDesired({w, h});
}

void Separator::Render(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;
    // controlStrokeDefault is documented as "thin separators / control borders"
    D2D1_COLOR_F stroke = EffectiveForeground(pal.controlStrokeDefault);

    // Draw a line along the orientation axis, centered in the cross axis.
    if (orientation_ == Orientation::Horizontal) {
        float y = bounds_.y + bounds_.h * 0.5f;
        dc.DrawLine({bounds_.x, y}, {bounds_.right(), y}, stroke, thickness_);
    } else {
        float x = bounds_.x + bounds_.w * 0.5f;
        dc.DrawLine({x, bounds_.y}, {x, bounds_.bottom()}, stroke, thickness_);
    }
}

}  // namespace fluent
