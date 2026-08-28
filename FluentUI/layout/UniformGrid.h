// UniformGrid.h — auto-wrapping grid where every cell is the same size.
//
// WPF has this as a first-class panel; without it you either (1) use WrapPanel
// with explicit ItemWidth/ItemHeight, which wastes cross-axis space when children
// vary in size, or (2) build a multi-row Grid and manually assign each child's
// row/column, which is verbose and non-responsive. UniformGrid measures all
// children to find the max width and height, then arranges them in equally-sized
// cells, wrapping to fill the available width.
//
// API:
//   Columns — explicit column count (0 = auto-calculate from width).
//   Rows — explicit row count (0 = auto-calculate from item count and columns).
//   FirstColumn — which cell of the first row the first child occupies (default 0).
//
// WPF's UniformGrid has no spacing property; gaps between cells are achieved by
// setting Margin on children. That's the pattern here too — no HorizontalSpacing
// or VerticalSpacing, unlike WrapPanel.
//
// Layout algorithm:
//   1. Measure all children to find maxChildW and maxChildH.
//   2. If Columns is set, use it; else fit as many maxChildW-wide cells as will
//      fill availW. If Rows is set, use it; else derive from child count and cols.
//   3. Arrange each child in its (row, col) cell, all cells the same size.
//
// The cell size is (maxW, maxH), not (availW/cols, availH/rows) — WPF's
// UniformGrid stretches cells to fill only when every child has Star alignment,
// which this framework doesn't model. The cells stay child-driven and any extra
// space in the panel is left as margin.

#pragma once
#include "Panel.h"

namespace fluent {

class UniformGrid : public Panel {
public:
    int Columns() const { return columns_; }
    void SetColumns(int c);

    int Rows() const { return rows_; }
    void SetRows(int r);

    int FirstColumn() const { return firstColumn_; }
    void SetFirstColumn(int fc);

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& finalRect) override;

private:
    int columns_ = 0;      // 0 = auto-fit to available width
    int rows_ = 0;         // 0 = auto-calculate from child count
    int firstColumn_ = 0;  // first child starts at this column (for asymmetry)

    // Cached from Measure so Arrange doesn't have to recompute.
    float cellW_ = 0.0f;
    float cellH_ = 0.0f;
    int measuredCols_ = 0;
};

} // namespace fluent
