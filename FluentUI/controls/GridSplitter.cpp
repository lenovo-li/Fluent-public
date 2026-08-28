// GridSplitter.cpp

#include "GridSplitter.h"
#include "../layout/Grid.h"
#include "../core/UIContext.h"
#include "../input/InputManager.h"
#include "../styling/ThemeManager.h"
#include <Windows.h>

namespace fluent {

GridSplitter::GridSplitter() {
    // Default size: 4 DIP wide/tall, stretches along the other axis.
    SetWidth(4.0f);
    SetHeight(4.0f);
}

void GridSplitter::SetOrientation(Orientation o) {
    if (orientation_ == o) return;
    orientation_ = o;
    Invalidate();
}

HCURSOR GridSplitter::Cursor() const {
    if (!CanResize()) return nullptr;
    return LoadCursor(nullptr, orientation_ == Orientation::Vertical
                                   ? IDC_SIZEWE  // left/right resize
                                   : IDC_SIZENS); // up/down resize
}

Grid* GridSplitter::ParentGrid() const {
    return dynamic_cast<Grid*>(Parent());
}

int GridSplitter::TrackIndex() const {
    Grid* grid = ParentGrid();
    if (!grid) return -1;
    return (orientation_ == Orientation::Vertical) ? grid->ColumnOf(this)
                                                   : grid->RowOf(this);
}

bool GridSplitter::CanResize() const {
    Grid* grid = ParentGrid();
    if (!grid) return false;

    // Need a track on each side, so at least 3 tracks and an interior index.
    const int count = static_cast<int>(orientation_ == Orientation::Vertical
                                           ? grid->ColumnCount()
                                           : grid->RowCount());
    if (count < 3) return false;
    const int idx = TrackIndex();
    return idx > 0 && idx < count - 1;
}

void GridSplitter::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    if (!CanResize()) return;
    Grid* grid = ParentGrid();
    if (!grid) return;

    // Capture so a drag that leaves the 4 DIP bar keeps resizing (the whole point
    // of a splitter is that the pointer outruns it).
    if (Context().input) Context().input->CapturePointer(this);
    dragging_ = true;

    // e.position is already window DIPs; record the anchor.
    dragStartX_ = e.position.x;
    dragStartY_ = e.position.y;

    // Sample the two neighbouring tracks' RESOLVED sizes. A Star or Auto track has
    // no size in its definition, so this is the only place the draggable number
    // exists. Empty before the first Arrange — then there is nothing to drag yet.
    const int idx = TrackIndex();
    const auto& resolved = (orientation_ == Orientation::Vertical)
                               ? grid->ResolvedColumnSizes()
                               : grid->ResolvedRowSizes();
    if (idx > 0 && idx + 1 < static_cast<int>(resolved.size())) {
        trackBeforeStart_ = resolved[idx - 1];
        trackAfterStart_ = resolved[idx + 1];
    }

    e.handled = true;
}

void GridSplitter::OnPointerMoved(PointerEventArgs& e) {
    if (!dragging_ || !trackBeforeStart_ || !trackAfterStart_) return;
    Grid* grid = ParentGrid();
    if (!grid) return;

    const float delta = (orientation_ == Orientation::Vertical)
                            ? (e.position.x - dragStartX_)
                            : (e.position.y - dragStartY_);

    // The pair is resized as a zero-sum move: what one track gains the other loses,
    // so the grid's total content extent does not change and nothing outside the
    // pair shifts. Both are clamped at kMinTrackDip, which means at the extremes the
    // move stops being zero-sum — that is deliberate, and preferable to letting a
    // track reach 0 (a 0 DIP track is unhittable, so the splitter would strand
    // itself against an edge it cannot come back from).
    const float newBefore = std::max(kMinTrackDip, *trackBeforeStart_ + delta);
    const float newAfter = std::max(kMinTrackDip, *trackAfterStart_ - delta);

    const int idx = TrackIndex();
    if (idx <= 0) return;

    // Both tracks become Pixel. See the header for why this is the only coherent
    // choice and what it costs (the pair stops re-proportioning on window resize).
    if (orientation_ == Orientation::Vertical) {
        grid->SetColumnAt(static_cast<size_t>(idx) - 1, GridLength::Pixels(newBefore));
        grid->SetColumnAt(static_cast<size_t>(idx) + 1, GridLength::Pixels(newAfter));
    } else {
        grid->SetRowAt(static_cast<size_t>(idx) - 1, GridLength::Pixels(newBefore));
        grid->SetRowAt(static_cast<size_t>(idx) + 1, GridLength::Pixels(newAfter));
    }

    e.handled = true;
}

void GridSplitter::OnPointerReleased(PointerEventArgs& e) {
    if (!dragging_) return;
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    dragging_ = false;
    trackBeforeStart_ = std::nullopt;
    trackAfterStart_ = std::nullopt;
    e.handled = true;
}

void GridSplitter::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const float corner = EffectiveCornerRadius(2.0f);

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);

    // A quiet bar: a hairline stroke reads as a seam between two panes, and the
    // hover/drag fill is what tells the user it is grabbable. Both go through the
    // Control property layer, so an app can restyle one splitter without a theme.
    if (dragging_ || State() == VisualState::Hover) {
        const D2D1_COLOR_F fill = EffectiveBackground(
            dragging_ ? th.colors.controlFillPressed : th.colors.controlFillHover);
        dc.FillRoundedRect(rr, fill);
    } else if (HasBackground()) {
        // An explicitly set Background is drawn at rest too — otherwise setting it
        // would look like it did nothing until the pointer arrives.
        dc.FillRoundedRect(rr, EffectiveBackground());
    }

    dc.DrawRoundedRect(rr, EffectiveBorderBrush(th.colors.controlStrokeDefault),
                       EffectiveBorderThickness(1.0f));
}

} // namespace fluent
