// GridSplitter.h — Resizable divider between Grid tracks (roadmap §5).
//
// A GridSplitter is a thin draggable bar placed between two Grid columns or rows.
// Dragging it resizes the adjacent tracks by converting them to Pixel definitions.
// The conversion is deliberate (WPF does the same): a Star track's size is a
// *proportion* of leftover space, so assigning it a DIP size is meaningless.
//
// Usage:
//   auto* grid = root->Emplace<Grid>();
//   grid->AddColumn(GridLength::Star());    // resizable after first drag
//   grid->AddColumn(GridLength::Pixel(4));  // the splitter column (fixed 4 DIP)
//   grid->AddColumn(GridLength::Star());    // resizable after first drag
//
//   auto* splitter = grid->Emplace<GridSplitter>();
//   grid->SetCell(splitter, 0, 1);  // row 0, column 1 (the middle track)
//   splitter->SetOrientation(GridSplitter::Orientation::Vertical);  // resizes columns
//
// Orientation:
//   - Vertical: splitter is a vertical bar; dragging left/right resizes columns.
//   - Horizontal: splitter is a horizontal bar; dragging up/down resizes rows.
//
// The splitter resizes the tracks BEFORE and AFTER its own track. If placed in
// column 1, it resizes columns 0 and 2. If placed in the first or last track,
// there's nothing to resize on one side, so dragging is disabled.
//
// After the first drag, the affected tracks are Pixel (frozen size). Resizing the
// Grid does not re-proportion them. This matches WPF GridSplitter behavior.

#pragma once
#include "../core/Control.h"
#include <optional>

namespace fluent {

class Grid;

class GridSplitter final : public Control {
public:
    enum class Orientation { Horizontal, Vertical };

    GridSplitter();

    void SetOrientation(Orientation o);
    Orientation GetOrientation() const { return orientation_; }

    // Override: show resize cursor when hovering.
    HCURSOR Cursor() const override;

protected:
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void Render(const DrawingContext& dc) override;

private:
    // The parent as a Grid, or null when this splitter is not in one. A splitter in
    // any other panel is inert rather than an error: it draws, and does nothing.
    Grid* ParentGrid() const;
    // This splitter's own track index along the resize axis, or -1 with no parent
    // Grid / no cell assignment.
    int TrackIndex() const;
    // True when there is a track on each side to redistribute between.
    bool CanResize() const;

    // Floor for either track during a drag. Not 0: a zero-extent track cannot be
    // hit-tested, so a splitter dragged fully against one side could never be
    // dragged back.
    static constexpr float kMinTrackDip = 1.0f;

    Orientation orientation_ = Orientation::Vertical;
    bool dragging_ = false;
    // Drag anchor, in window DIPs (what PointerEventArgs::position carries). Only
    // the axis being resized matters, but both are recorded so the unused one is
    // available if a future diagonal/corner splitter needs it.
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    // Resolved sizes of the two tracks either side of the splitter, sampled at
    // drag start. Deltas are applied to THESE, not to the live sizes: accumulating
    // against the live value re-reads a number the previous move already changed,
    // which drifts (and, once a track hits its 1 DIP clamp, ratchets).
    std::optional<float> trackBeforeStart_;
    std::optional<float> trackAfterStart_;
};

} // namespace fluent
