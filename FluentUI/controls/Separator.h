// Separator.h — semantic divider for menus, toolbars, and forms.
//
// WPF has this as a first-class control; without it every divider is a manually
// sized Border or a TextBlock full of dashes. A Separator is orientation-aware
// (horizontal draws a horizontal line, vertical a vertical one) and inherits the
// semantic "this is a divider" meaning that CSS `<hr>` or `role="separator"` carry.
//
// Default size: 1 DIP thick, stretches to fill the cross axis (HAlign::Stretch for
// a horizontal separator, VAlign::Stretch for vertical). The user can override with
// explicit Width/Height, or set the thickness via SetThickness(). Colour comes from
// Theme().colors.controlStrokeDefault, whose own comment names "thin separators" as
// its purpose — there is no separate divider token to add.
//
// Orientation is set via SetOrientation(Horizontal | Vertical), matching StackPanel.

#pragma once
#include "../core/Control.h"

namespace fluent {

class Separator : public Control {
public:
    enum class Orientation { Horizontal, Vertical };

    Separator();

    Orientation GetOrientation() const { return orientation_; }
    void SetOrientation(Orientation o);

    float Thickness() const { return thickness_; }
    void SetThickness(float t);

    // --- Element contract -------------------------------------------------
    // Public, matching every other element in this codebase (Border, GroupBox,
    // Button…). Panels call Measure on children through MeasureCached, and headless
    // tests drive it directly.
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;

private:
    Orientation orientation_ = Orientation::Horizontal;
    float thickness_ = 1.0f;
};

}  // namespace fluent
