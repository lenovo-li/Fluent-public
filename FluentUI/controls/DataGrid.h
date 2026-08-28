// DataGrid.h — a virtualized, read-only table of text cells.
//
// WHY THIS EXISTS. A data application's central widget is a table, and this framework had
// none: ListBox is one column of strings, TreeView is a hierarchy. The consumer that
// motivated this shows market snapshots and financial statements — thousands of rows,
// tens of columns — so the requirement is not "draw a grid" but "draw a grid whose cost
// does not grow with the data".
//
// VIRTUALIZATION IS THE POINT, and it applies to BOTH axes:
//   * rows: only those intersecting the viewport are drawn;
//   * columns: only those intersecting the horizontal scroll window are drawn.
// A 5000x80 table therefore costs the same per frame as a 20x8 one. Column virtualization
// matters as much as row virtualization here: a wide financial statement has 80+ periods,
// and drawing all of them off-screen would dominate the frame while looking fine on a
// small test table.
//
// PULL, NOT PUSH: the grid never owns the data. The caller supplies a cell provider
// (row, col) -> wstring, so a million-row table costs nothing to "load" and the app keeps
// its own storage, formatting and sort order. This is the same contract ListBox's
// virtualized mode uses, for the same reason.
//
// SCROLLING is internal (it owns two ScrollState objects) rather than delegated to an
// enclosing ScrollPanel. A grid must keep its header row pinned while the body scrolls
// vertically, and its row labels are drawn per-cell; an outer scroller would move the
// header out of view and would have to be told the total extent anyway.
//
// WHAT IT DOES NOT DO, so the limits are known before use:
//   * No editing. Read-only. Cell editing needs a focus/commit/validation model that
//     belongs to a separate control (an editable grid is a different beast).
//   * No sorting or filtering. The caller sorts its own data and re-points the provider;
//     the grid has no opinion about order. Header clicks are reported so the app can act.
//   * No per-cell widgets. Cells are text. A grid of arbitrary elements would defeat the
//     virtualization this control exists for.
//   * No cell wrapping. One line per cell, clipped at the column edge — a wrapped cell
//     makes row height data-dependent, which breaks the constant-time row indexing that
//     makes scrolling O(visible).

#pragma once
#include "../core/Control.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../layout/ScrollViewer.h"
#include <functional>
#include <string>
#include <vector>

namespace fluent {

class DataGrid : public Control {
public:
    // How a column's text sits in its cell. Numeric data is read by comparing digits in a
    // column, which only works when the digits line up, so a numeric column must be
    // right-aligned; that is why this exists rather than one global alignment.
    enum class Align { Left, Right, Center };

    struct Column {
        std::wstring header;
        float width = 96.0f;      // DIPs; fixed (no auto-fit — see the note on Measure)
        Align align = Align::Left;
    };

    // (row, col) -> the text to draw. Called only for visible cells, once per cell per
    // frame. Must be cheap and must not allocate if avoidable: it is on the render path.
    using CellProvider = std::function<std::wstring(int row, int col)>;

    // (row) -> a colour for that row's text, or nullopt to use the theme default. Exists
    // for the case that motivated the grid: a market table where a whole row is tinted by
    // whether the instrument rose or fell. Optional — a grid with no accessor draws every
    // row in textPrimary.
    using RowColorProvider = std::function<bool(int row, D2D1_COLOR_F& out)>;

    DataGrid();

    // --- Data ------------------------------------------------------------
    void SetColumns(std::vector<Column> columns);
    const std::vector<Column>& Columns() const { return columns_; }

    // Row COUNT, not rows: the grid pulls text on demand.
    void SetRowCount(int count);
    int RowCount() const { return rowCount_; }

    void SetCellProvider(CellProvider provider);
    void SetRowColorProvider(RowColorProvider provider);

    // --- Appearance ------------------------------------------------------
    void SetRowHeight(float dip);
    float RowHeight() const { return rowHeight_; }

    // Zebra striping. Off by default: it helps a wide table and clutters a narrow one, so
    // the caller decides.
    void SetAlternatingRowFill(bool on);
    bool AlternatingRowFill() const { return alternating_; }

    void SetShowGridLines(bool on);
    bool ShowGridLines() const { return showGridLines_; }

    // --- Selection -------------------------------------------------------
    // Single full-row selection, or -1 for none. Row selection (not cell) because the
    // consumer's interaction is "pick this instrument", and a cell cursor would imply
    // editing.
    void SetSelectedRow(int row);
    int SelectedRow() const { return selectedRow_; }
    Event<DataGrid, RoutedEventArgs>& SelectionChanged() { return selectionChanged_; }

    // A header click, so the app can implement sorting. Payload is the column index in
    // ColumnClickedIndex() — read it in the handler.
    Event<DataGrid, RoutedEventArgs>& ColumnHeaderClicked() { return headerClicked_; }
    int ColumnClickedIndex() const { return clickedColumn_; }

    // --- Geometry / virtualization, public for testability ----------------
    // These four are the virtualization contract. They are public because that contract
    // IS the reason this control exists, and a headless test has no other way to prove
    // that a 100k-row grid only touches a screenful — the alternative would be asserting
    // pixels, which cannot distinguish "drew 20 rows" from "drew 100000 rows, 19980 of
    // them off-screen".
    float TotalContentHeight() const { return rowCount_ * rowHeight_; }
    float TotalContentWidth() const;
    // First and last row index intersecting the viewport, inclusive. Returns {0,-1} when
    // nothing is visible (empty grid or scrolled past the end).
    void VisibleRowRange(int& first, int& last) const;
    void VisibleColumnRange(int& first, int& last) const;
    // The header strip, in window DIPs. Pinned: it does not move when the body scrolls
    // vertically (it DOES shift with horizontal scroll, with its cells).
    RectDip HeaderRect() const;
    // The scrollable body area (grid minus header, minus any scrollbar gutters).
    RectDip ViewportRect() const;
    // Where a given cell lands right now, accounting for both scroll offsets. May be
    // outside ViewportRect if the cell is scrolled away.
    RectDip CellRect(int row, int col) const;

    // Scroll offsets, exposed so a test can place the viewport deterministically instead
    // of synthesising wheel events.
    void SetVerticalOffset(float dip);
    float VerticalOffset() const { return scroll_.Offset(); }
    void SetHorizontalOffset(float dip);
    float HorizontalOffset() const { return scroll_.OffsetX(); }

    // Scroll a row into view. Needed by keyboard navigation and by an app restoring a
    // selection; without it "select row 900" would leave the viewport at the top.
    void ScrollRowIntoView(int row);

    // --- Element contract -------------------------------------------------
    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    float VisualOverflowDip() const override;
    // Smooth scrolling: the ScrollViewer eases toward its target, so the grid has to be
    // ticked while it does. Same wiring as ListBox.
    bool WantsAnimationTick() const override { return scroll_.NeedsTick(); }
    void OnAnimationTick(float dtSec) override;

private:
    // Row under a point, or -1. Shared by pointer handling and tests.
    int RowAtPoint(float dipY) const;
    // Clamp both scroll offsets to their current extents. Called after anything that
    // changes content size or viewport size, because a resize that shrinks the content
    // below the current offset would otherwise leave the view scrolled into blank space.
    void ClampScroll();

    std::vector<Column> columns_;
    int rowCount_ = 0;
    CellProvider cellProvider_;
    RowColorProvider rowColorProvider_;

    float rowHeight_ = 28.0f;
    bool alternating_ = false;
    bool showGridLines_ = true;
    int selectedRow_ = -1;
    int clickedColumn_ = -1;

    // ONE ScrollViewer, not two: it already carries both axes, and they deliberately share
    // the fade/idle state so the two rails appear and disappear together (see the comment
    // on its horizontal section). Two instances would desynchronise that.
    //
    // It is a MEMBER, not a child element: it is scroll state plus a scrollbar renderer,
    // and putting it in the tree would give it its own layout slot and hit-testing.
    // ListBox holds it the same way for the same reason.
    ScrollViewer scroll_;

    // Reused across the row loop so a screenful of cells does not allocate a string per
    // cell per frame. Mutable because Render is logically const but must reuse it.
    mutable std::wstring cellScratch_;

    Event<DataGrid, RoutedEventArgs> selectionChanged_;
    Event<DataGrid, RoutedEventArgs> headerClicked_;
};

}  // namespace fluent
