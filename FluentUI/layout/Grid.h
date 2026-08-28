// Grid.h — lays children out in a grid of rows and columns.
//
// Columns/rows are defined by GridLength tracks (Auto / Pixel / Star). Each
// child is assigned a cell (row, col) and may span multiple tracks. Cell
// placement lives in the Grid (a map keyed by the child pointer) rather than on
// the FrameworkElement base class, so grid-specific state does not leak into every
// element. Children are owned via Panel (unique_ptr); use Emplace/Add then
// SetCell to position them:
//
//   auto* label = grid->Emplace<TextBlock>();
//   grid->SetCell(label, /*row*/ 0, /*col*/ 0);
//
// Layout follows the WPF two-pass protocol (Measure -> Arrange). With no track
// definitions the Grid degenerates to a single cell that fills its bounds.
#pragma once

#include "Panel.h"
#include <unordered_map>
#include <vector>

namespace fluent {

class Grid : public Panel {
public:
    // Track definitions. Add incrementally or set all at once.
    void AddColumn(GridLength w) { columns_.push_back(w); }
    void AddRow(GridLength h) { rows_.push_back(h); }
    void SetColumns(std::vector<GridLength> cols) { columns_ = std::move(cols); }
    void SetRows(std::vector<GridLength> rows) { rows_ = std::move(rows); }

    void SetColumnSpacing(float dip) { colSpacing_ = dip; }
    void SetRowSpacing(float dip) { rowSpacing_ = dip; }

    // --- Runtime track access (GridSplitter, roadmap §5) ----------------------
    //
    // A GridSplitter has to read the CURRENT track definitions and write new ones
    // as the user drags, which the Add*/Set* API above cannot express (it only
    // appends or replaces wholesale). These four accessors are the minimum needed;
    // deliberately not a general "track collection" abstraction.
    //
    // The resize contract, and why it is Pixel-only: dragging a splitter means
    // "these two tracks now have these exact sizes". A Star track's size is a
    // *proportion* of leftover space, so assigning it a DIP size is meaningless —
    // the next Arrange would recompute it from the star weight and discard the
    // drag. So ResizeTracks CONVERTS both affected tracks to Pixel. That is what
    // WPF does too, and it is why a splitter between two Star columns "freezes"
    // them on first drag: after the drag they no longer re-proportion on resize.
    // Document that in any UI that uses one.
    size_t ColumnCount() const { return columns_.size(); }
    size_t RowCount() const { return rows_.size(); }
    const GridLength& ColumnAt(size_t i) const { return columns_.at(i); }
    const GridLength& RowAt(size_t i) const { return rows_.at(i); }

    // Set one track's definition and invalidate layout. Out-of-range is a no-op.
    void SetColumnAt(size_t i, GridLength w);
    void SetRowAt(size_t i, GridLength h);

    // The resolved (post-Arrange) size of each track in DIPs, or empty before the
    // first Arrange. A splitter needs these because a track defined as Star or Auto
    // has no size in its definition — the number the user is dragging only exists
    // after resolution.
    const std::vector<float>& ResolvedColumnSizes() const { return resolvedCols_; }
    const std::vector<float>& ResolvedRowSizes() const { return resolvedRows_; }

    // Which track a child sits in, or -1 when the child has no cell assignment.
    // Narrower than exposing the private Cell struct: a splitter only needs to know
    // its own row/column to find the two tracks it sits between, and spans are
    // irrelevant to it (a spanning splitter is not a meaningful thing).
    int ColumnOf(const FrameworkElement* child) const;
    int RowOf(const FrameworkElement* child) const;

    // Assign a child (already added to the panel) to a cell, optionally spanning
    // multiple tracks. Spans are clamped to the track count during layout.
    void SetCell(FrameworkElement* child, int row, int col, int rowSpan = 1, int colSpan = 1);

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

private:
    struct Cell {
        int row = 0, col = 0;
        int rowSpan = 1, colSpan = 1;
    };
    Cell CellOf(const FrameworkElement* child) const;

    // Resolve final track sizes along one axis given the available extent.
    // `tracks` are the definitions; `spacing` sits between adjacent tracks;
    // `autoSizes` supplies each track's measured content size (0 for non-Auto).
    static std::vector<float> ResolveTracks(const std::vector<GridLength>& tracks,
                                            const std::vector<float>& autoSizes,
                                            float spacing, float avail);
    // Cumulative start offset of each track (offsets[i] = start of track i).
    static std::vector<float> TrackOffsets(const std::vector<float>& sizes,
                                           float spacing, float origin);
    // Summed extent of tracks [start, start+span) plus interior spacing.
    static float SpanExtent(const std::vector<float>& sizes, int start, int span,
                            float spacing);

    std::vector<GridLength> columns_;
    std::vector<GridLength> rows_;
    float colSpacing_ = 0.0f;
    float rowSpacing_ = 0.0f;
    std::unordered_map<const FrameworkElement*, Cell> cells_;

    std::vector<float> resolvedCols_;  // actual sizes after last ArrangeOverride
    std::vector<float> resolvedRows_;  // (for GridSplitter runtime queries)
};

} // namespace fluent
