// UniformGrid.cpp

#include "UniformGrid.h"
#include <algorithm>
#include <limits>

namespace fluent {

void UniformGrid::SetColumns(int c) {
    if (SetProperty(columns_, std::max(0, c), DirtyFlags::Measure))
        InvalidateDirty(DirtyFlags::Measure);
}

void UniformGrid::SetRows(int r) {
    if (SetProperty(rows_, std::max(0, r), DirtyFlags::Measure))
        InvalidateDirty(DirtyFlags::Measure);
}

void UniformGrid::SetFirstColumn(int fc) {
    if (SetProperty(firstColumn_, std::max(0, fc), DirtyFlags::Arrange))
        InvalidateDirty(DirtyFlags::Arrange);
}

SizeDip UniformGrid::MeasureOverride(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);

    // Step 1: measure every child unconstrained to find the natural cell size. The
    // cell is the max over all children, which is what makes the grid uniform.
    float maxChildW = 0.0f;
    float maxChildH = 0.0f;
    int childCount = 0;
    for (auto& child : children_) {
        if (!child || !child->IsVisible()) continue;
        ++childCount;
        child->MeasureCached(std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity());
        // Read the field the child just wrote — see the note in Border::Measure.
        const SizeDip& d = child->Desired();
        const Thickness& m = child->Margin();
        maxChildW = std::max(maxChildW, d.w + m.horizontal());
        maxChildH = std::max(maxChildH, d.h + m.vertical());
    }

    if (childCount == 0 || (maxChildW <= 0.0f && maxChildH <= 0.0f)) {
        return {0.0f, 0.0f};
    }

    // Step 2: determine the grid shape.
    int cols = columns_;
    int rows = rows_;

    // If columns is 0 (auto), fit as many maxChildW-wide cells as possible.
    if (cols == 0) {
        if (availW > 0.0f && maxChildW > 0.0f) {
            cols = std::max(1, static_cast<int>(availW / maxChildW));
        } else {
            cols = childCount;  // fallback: one row
        }
    }

    // If rows is 0 (auto), calculate from child count and cols.
    if (rows == 0) {
        // Account for FirstColumn: the first row has (cols - firstColumn_) slots.
        int firstRowCapacity = cols - firstColumn_;
        if (firstRowCapacity <= 0) firstRowCapacity = cols;  // clamp sanity
        int remaining = childCount - firstRowCapacity;
        rows = 1;  // first row
        if (remaining > 0) {
            rows += (remaining + cols - 1) / cols;
        }
    }

    // Cache the results so Arrange can reuse them without recomputing.
    cellW_ = maxChildW;
    cellH_ = maxChildH;
    measuredCols_ = cols;

    // Desired size: all cells occupied.
    return {cols * maxChildW, rows * maxChildH};
}

void UniformGrid::ArrangeOverride(const RectDip& finalRect) {
    // Cell size and column count both come from the last Measure rather than being
    // recomputed here. Recomputing would mean two places deriving the same numbers
    // from slightly different inputs (Measure sees availW, Arrange sees finalRect.w),
    // and any disagreement shows up as children landing a fraction of a cell off.
    if (cellW_ <= 0.0f || cellH_ <= 0.0f || measuredCols_ <= 0) return;

    int col = firstColumn_;
    int row = 0;
    for (auto& child : children_) {
        if (!child || !child->IsVisible()) continue;

        const RectDip cell{finalRect.x + col * cellW_, finalRect.y + row * cellH_,
                           cellW_, cellH_};
        // Shared arrange logic so each child's Margin/Min/Max/Align apply within its
        // cell — a child smaller than the cell centres or stretches per its own
        // alignment rather than being force-fit.
        child->Arrange(FrameworkElement::ComputeArrangeRect(child.get(), cell));

        if (++col >= measuredCols_) {
            col = 0;
            ++row;
        }
    }
}

} // namespace fluent
