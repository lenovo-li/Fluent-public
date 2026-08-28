// StackPanel.h — lays children out in a single row or column.
//
// Children are stacked along the main axis (with `spacing` between them and
// each child's margin); the cross axis offers the full panel extent so a child
// can stretch or align within it.
#pragma once

#include "Panel.h"

namespace fluent {

class StackPanel : public Panel {
public:
    enum class Orientation { Vertical, Horizontal };

    void SetOrientation(Orientation o) { orientation_ = o; }
    Orientation GetOrientation() const { return orientation_; }
    void SetSpacing(float dip) { spacing_ = dip; }
    float Spacing() const { return spacing_; }

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

private:
    bool horizontal() const { return orientation_ == Orientation::Horizontal; }

    Orientation orientation_ = Orientation::Vertical;
    float spacing_ = 0.0f;
};

} // namespace fluent
