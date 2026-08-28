// WrapPanel.h — wraps children into rows (Horizontal) or columns (Vertical).
//
// The panel measures children sequentially along the main axis. When the
// cumulative size exceeds the available space, the panel starts a new line.
// Each line is as tall (Horizontal) or wide (Vertical) as its tallest/widest
// child.
//
// ItemWidth/ItemHeight override the child's measured size on that axis,
// making all children uniform (useful for tile grids). When not set
// (std::nullopt), children measure naturally.
#pragma once

#include "Panel.h"
#include <optional>

namespace fluent {

class WrapPanel : public Panel {
public:
    enum class Orientation { Horizontal, Vertical };

    void SetOrientation(Orientation o);
    Orientation GetOrientation() const { return orientation_; }

    void SetItemWidth(float dip);
    void ClearItemWidth() { itemWidth_ = std::nullopt; }
    std::optional<float> GetItemWidth() const { return itemWidth_; }

    void SetItemHeight(float dip);
    void ClearItemHeight() { itemHeight_ = std::nullopt; }
    std::optional<float> GetItemHeight() const { return itemHeight_; }

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

private:
    struct Line {
        int startIndex;  // first child in this line
        int endIndex;    // one-past-last child index (can span invisible children)
        float mainSize;  // width (Horizontal) or height (Vertical) of the line
        float crossSize; // height (Horizontal) or width (Vertical) of the line
    };

    // Compute line-break positions during Measure. Returns the total extent
    // (sum of line cross sizes) and fills `lines_`.
    float ComputeLines(float availMain, float availCross);

    bool horizontal() const { return orientation_ == Orientation::Horizontal; }

    Orientation orientation_ = Orientation::Horizontal;
    std::optional<float> itemWidth_;
    std::optional<float> itemHeight_;

    // Cached line-break result from the last Measure, reused by Arrange.
    std::vector<Line> lines_;
};

} // namespace fluent
