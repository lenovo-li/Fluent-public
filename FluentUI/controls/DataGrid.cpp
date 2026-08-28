// DataGrid.cpp — see DataGrid.h.

#include "DataGrid.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/DWriteContext.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"

#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kCellPadH = 8.0f;      // text inset inside a cell
constexpr float kHeaderExtra = 4.0f;   // header row is slightly taller than a data row
constexpr float kMinRowHeight = 16.0f;
constexpr float kWheelStepRows = 3.0f; // rows per wheel notch
}  // namespace

DataGrid::DataGrid() {
    SetFocusable(true);      // keyboard row navigation
    SetHAlign(HAlign::Stretch);
    SetVAlign(VAlign::Stretch);
    scroll_.SetKeepVisibleWhenOverflow(true);
}

void DataGrid::SetColumns(std::vector<Column> columns) {
    columns_ = std::move(columns);
    InvalidateDirty(DirtyFlags::Measure);
}

void DataGrid::SetRowCount(int count) {
    const int clamped = std::max(0, count);
    if (rowCount_ == clamped) return;
    rowCount_ = clamped;
    // A shrink can leave the selection past the end, and the offset past the content.
    if (selectedRow_ >= rowCount_) selectedRow_ = rowCount_ - 1;
    InvalidateDirty(DirtyFlags::Measure);
}

void DataGrid::SetCellProvider(CellProvider provider) {
    cellProvider_ = std::move(provider);
    Invalidate();   // Render-level: same geometry, different text
}

void DataGrid::SetRowColorProvider(RowColorProvider provider) {
    rowColorProvider_ = std::move(provider);
    Invalidate();
}

void DataGrid::SetRowHeight(float dip) {
    const float h = std::max(kMinRowHeight, dip);
    if (rowHeight_ == h) return;
    rowHeight_ = h;
    InvalidateDirty(DirtyFlags::Measure);
}

void DataGrid::SetAlternatingRowFill(bool on) {
    if (alternating_ == on) return;
    alternating_ = on;
    Invalidate();
}

void DataGrid::SetShowGridLines(bool on) {
    if (showGridLines_ == on) return;
    showGridLines_ = on;
    Invalidate();
}

void DataGrid::SetSelectedRow(int row) {
    const int clamped = (row < 0 || rowCount_ == 0)
                            ? -1
                            : std::min(row, rowCount_ - 1);
    if (selectedRow_ == clamped) return;
    selectedRow_ = clamped;
    Invalidate();   // selection is a fill, not a size
    RoutedEventArgs args{};
    args.source = this;
    selectionChanged_.Raise(*this, args);
}

float DataGrid::TotalContentWidth() const {
    float w = 0.0f;
    for (const Column& c : columns_) w += c.width;
    return w;
}

RectDip DataGrid::HeaderRect() const {
    const float h = rowHeight_ + kHeaderExtra;
    return RectDip{bounds_.x, bounds_.y, bounds_.w, std::min(h, bounds_.h)};
}

RectDip DataGrid::ViewportRect() const {
    const RectDip hdr = HeaderRect();
    const float y = bounds_.y + hdr.h;
    return RectDip{bounds_.x, y, bounds_.w, std::max(0.0f, bounds_.bottom() - y)};
}

void DataGrid::VisibleRowRange(int& first, int& last) const {
    first = 0;
    last = -1;                       // the documented "nothing visible" answer
    if (rowCount_ <= 0 || rowHeight_ <= 0.0f) return;
    const RectDip vp = ViewportRect();
    if (vp.h <= 0.0f) return;

    const float top = scroll_.Offset();
    // floor for the first row and ceil for the last: a row only partly scrolled into the
    // viewport is still visible and must be drawn, otherwise scrolling shows a gap at the
    // top edge for a fraction of a row height.
    first = static_cast<int>(std::floor(top / rowHeight_));
    last = static_cast<int>(std::ceil((top + vp.h) / rowHeight_)) - 1;
    first = std::max(0, first);
    last = std::min(last, rowCount_ - 1);
    if (last < first) { first = 0; last = -1; }
}

void DataGrid::VisibleColumnRange(int& first, int& last) const {
    first = 0;
    last = -1;
    if (columns_.empty() || bounds_.w <= 0.0f) return;

    const float left = scroll_.OffsetX();
    const float right = left + bounds_.w;
    float x = 0.0f;
    int firstCol = -1, lastCol = -1;
    for (size_t i = 0; i < columns_.size(); ++i) {
        const float cw = columns_[i].width;
        // Half-open intersection test: a column touching the edge exactly is not visible,
        // which keeps the count stable instead of flickering by one at exact offsets.
        if (x + cw > left && x < right) {
            if (firstCol < 0) firstCol = static_cast<int>(i);
            lastCol = static_cast<int>(i);
        }
        x += cw;
    }
    if (firstCol < 0) { first = 0; last = -1; return; }
    first = firstCol;
    last = lastCol;
}

RectDip DataGrid::CellRect(int row, int col) const {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return RectDip{};
    float x = bounds_.x - scroll_.OffsetX();
    for (int i = 0; i < col; ++i) x += columns_[static_cast<size_t>(i)].width;
    const RectDip vp = ViewportRect();
    const float y = vp.y + row * rowHeight_ - scroll_.Offset();
    return RectDip{x, y, columns_[static_cast<size_t>(col)].width, rowHeight_};
}

void DataGrid::SetVerticalOffset(float dip) {
    scroll_.SetOffset(dip);
    Invalidate();
}

void DataGrid::SetHorizontalOffset(float dip) {
    scroll_.SetOffsetX(dip);
    Invalidate();
}

void DataGrid::ScrollRowIntoView(int row) {
    if (row < 0 || row >= rowCount_) return;
    const RectDip vp = ViewportRect();
    if (vp.h <= 0.0f) return;

    const float rowTop = row * rowHeight_;
    const float rowBottom = rowTop + rowHeight_;
    const float viewTop = scroll_.Offset();
    const float viewBottom = viewTop + vp.h;

    // Only move if the row is actually outside: scrolling an already-visible row to the
    // top would make arrow-key navigation jerk the view on every keypress.
    if (rowTop < viewTop) scroll_.SetOffset(rowTop);
    else if (rowBottom > viewBottom) scroll_.SetOffset(rowBottom - vp.h);
    Invalidate();
}

void DataGrid::ClampScroll() {
    const RectDip vp = ViewportRect();
    scroll_.SetContentHeight(TotalContentHeight());
    scroll_.SetContentWidth(TotalContentWidth());
    // Re-applying the current offset runs it through the ScrollViewer's own clamp against
    // the extents just set, which is what pulls the view back when content shrinks.
    scroll_.SetOffset(scroll_.Offset());
    scroll_.SetOffsetX(scroll_.OffsetX());
    UNREFERENCED_PARAMETER(vp);
}

void DataGrid::Measure(float availW, float availH) {
    // Fixed column widths, so desired width is simply their sum: no auto-fit pass.
    //
    // Auto-fit is deliberately absent. Measuring the widest cell in a column means pulling
    // and laying out EVERY row's text for that column, which is O(rows) per measure — it
    // would undo the virtualization this control exists for, and on a 100k-row grid it
    // would stall the UI thread on every resize. A caller who wants fitted columns can
    // measure a sample itself and set widths.
    const float w = TotalContentWidth();
    const float h = TotalContentHeight() + HeaderRect().h;
    SetDesired({IsAuto(width_) ? (availW > 0.0f ? std::min(w, availW) : w) : width_,
                IsAuto(height_) ? (availH > 0.0f ? std::min(h, availH) : h) : height_});
}

void DataGrid::Arrange(const RectDip& finalRect) {
    bounds_ = finalRect;
    // The ScrollViewer's own bounds ARE the viewport it computes thumbs against, so they
    // must be the body area rather than the whole grid — otherwise the vertical thumb is
    // sized as if the pinned header scrolled too, and its travel is short by the header
    // height at the bottom of a long table.
    scroll_.Arrange(ViewportRect());
    ClampScroll();
}

void DataGrid::Render(const DrawingContext& dc) {
    if (!dc.Dc()) return;
    const ColorTokens& c = Theme().colors;
    const TypographyTokens& typo = Theme().typography;

    if (IsFocused())
        DrawFocusRing(dc, bounds_, c, FocusRingSpec{});

    DWriteContext* dw = Dwrite();
    const RectDip hdr = HeaderRect();
    const RectDip vp = ViewportRect();

    int firstRow = 0, lastRow = -1, firstCol = 0, lastCol = -1;
    VisibleRowRange(firstRow, lastRow);
    VisibleColumnRange(firstCol, lastCol);

    // --- Body -------------------------------------------------------------
    // Clipped to the viewport so a partially-scrolled row cannot paint over the header or
    // past the bottom edge. Rows are drawn before gridlines so the lines sit on top.
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(vp.x, vp.y, vp.right(), vp.bottom()));

        for (int r = firstRow; r <= lastRow; ++r) {
            const float rowY = vp.y + r * rowHeight_ - scroll_.Offset();
            const D2D1_RECT_F rowRect =
                D2D1::RectF(vp.x, rowY, vp.right(), rowY + rowHeight_);

            if (r == selectedRow_) {
                dc.FillRect(rowRect, c.accent);
            } else if (alternating_ && (r % 2) == 1) {
                dc.FillRect(rowRect, c.rowFillAlternate);
            }

            if (!dw || !dw->Valid() || !cellProvider_) continue;

            // Row text colour: selection wins over the app's per-row tint, because a
            // tinted row on an accent fill can fall below contrast.
            D2D1_COLOR_F textColor = c.textPrimary;
            if (r == selectedRow_) {
                textColor = c.onAccent;
            } else if (rowColorProvider_) {
                D2D1_COLOR_F rowColor{};
                if (rowColorProvider_(r, rowColor)) textColor = rowColor;
            }

            for (int col = firstCol; col <= lastCol; ++col) {
                const Column& column = columns_[static_cast<size_t>(col)];
                // One scratch buffer for the whole frame. assign() (not operator=) so the
                // buffer's capacity is REUSED: `cellScratch_ = provider(...)` is a move
                // assignment that steals the temporary's block and frees the grown
                // capacity, reallocating on every cell — the exact trap ListBox hit.
                const std::wstring text = cellProvider_(r, col);
                cellScratch_.assign(text);
                if (cellScratch_.empty()) continue;

                const RectDip cell = CellRect(r, col);
                DWRITE_TEXT_ALIGNMENT ta =
                    column.align == Align::Right  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                    : column.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                    : DWRITE_TEXT_ALIGNMENT_LEADING;
                IDWriteTextFormat* fmt = dw->Format(
                    typo.bodySize, DWRITE_FONT_WEIGHT_NORMAL, ta,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
                if (!fmt) continue;
                dc.DrawText(cellScratch_.c_str(),
                            static_cast<UINT32>(cellScratch_.size()), fmt,
                            D2D1::RectF(cell.x + kCellPadH, cell.y,
                                        cell.right() - kCellPadH, cell.bottom()),
                            textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }

        if (showGridLines_) {
            // Horizontal rules only between visible rows, vertical only between visible
            // columns: drawing all of them would be O(rows) and defeat the point.
            for (int r = firstRow; r <= lastRow + 1 && r <= rowCount_; ++r) {
                const float y = vp.y + r * rowHeight_ - scroll_.Offset();
                if (y < vp.y || y > vp.bottom()) continue;
                dc.DrawLine(D2D1::Point2F(vp.x, y), D2D1::Point2F(vp.right(), y),
                            c.gridLine, 1.0f);
            }
            float x = bounds_.x - scroll_.OffsetX();
            for (int col = 0; col <= static_cast<int>(columns_.size()); ++col) {
                if (x >= vp.x && x <= vp.right())
                    dc.DrawLine(D2D1::Point2F(x, vp.y), D2D1::Point2F(x, vp.bottom()),
                                c.gridLine, 1.0f);
                if (col < static_cast<int>(columns_.size()))
                    x += columns_[static_cast<size_t>(col)].width;
            }
        }
    }

    // --- Header (drawn AFTER the body, so a scrolled row cannot cover it) ---
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(hdr.x, hdr.y, hdr.right(), hdr.bottom()));
        dc.FillRect(D2D1::RectF(hdr.x, hdr.y, hdr.right(), hdr.bottom()),
                    c.controlFillDefault);
        dc.DrawLine(D2D1::Point2F(hdr.x, hdr.bottom()),
                    D2D1::Point2F(hdr.right(), hdr.bottom()),
                    c.controlStrokeDefault, 1.0f);

        if (dw && dw->Valid()) {
            float x = bounds_.x - scroll_.OffsetX();
            for (size_t i = 0; i < columns_.size(); ++i) {
                const Column& column = columns_[i];
                const float cw = column.width;
                if (x + cw > hdr.x && x < hdr.right() && !column.header.empty()) {
                    DWRITE_TEXT_ALIGNMENT ta =
                        column.align == Align::Right  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                        : column.align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                        : DWRITE_TEXT_ALIGNMENT_LEADING;
                    if (IDWriteTextFormat* fmt = dw->Format(
                            typo.bodySize, DWRITE_FONT_WEIGHT_SEMI_BOLD, ta,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                            DWRITE_WORD_WRAPPING_NO_WRAP)) {
                        dc.DrawText(column.header.c_str(),
                                    static_cast<UINT32>(column.header.size()), fmt,
                                    D2D1::RectF(x + kCellPadH, hdr.y,
                                                x + cw - kCellPadH, hdr.bottom()),
                                    c.textSecondary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                }
                x += cw;
            }
        }
    }

    // Scrollbars last: they overlay everything.
    scroll_.Render(dc);
}

int DataGrid::RowAtPoint(float dipY) const {
    const RectDip vp = ViewportRect();
    if (dipY < vp.y || dipY > vp.bottom() || rowHeight_ <= 0.0f) return -1;
    const int row = static_cast<int>((dipY - vp.y + scroll_.Offset()) / rowHeight_);
    return (row >= 0 && row < rowCount_) ? row : -1;
}

UIElement* DataGrid::HitTestDeep(float dipX, float dipY) {
    if (!IsVisible() || !bounds_.contains(dipX, dipY)) return nullptr;
    return this;
}

void DataGrid::OnPointerPressed(PointerEventArgs& e) {
    const RectDip hdr = HeaderRect();
    if (hdr.contains(e.position.x, e.position.y)) {
        // Header click: report the column so the app can sort. The grid does not sort.
        float x = bounds_.x - scroll_.OffsetX();
        for (size_t i = 0; i < columns_.size(); ++i) {
            const float cw = columns_[i].width;
            if (e.position.x >= x && e.position.x < x + cw) {
                clickedColumn_ = static_cast<int>(i);
                RoutedEventArgs args{};
                args.source = this;
                headerClicked_.Raise(*this, args);
                e.handled = true;
                return;
            }
            x += cw;
        }
        return;
    }
    const int row = RowAtPoint(e.position.y);
    if (row >= 0) {
        SetSelectedRow(row);
        e.handled = true;
    }
}

void DataGrid::OnPointerWheelChanged(PointerEventArgs& e) {
    // Animated, so the wheel feels like the rest of the framework's scrolling; AnimateBy
    // accumulates against the pending target so spinning builds momentum.
    scroll_.AnimateBy(-static_cast<float>(e.wheelDelta) / 120.0f *
                      kWheelStepRows * rowHeight_);
    e.handled = true;
    Invalidate();
}

void DataGrid::OnKeyDownRouted(KeyEventArgs& e) {
    if (rowCount_ <= 0) return;
    const RectDip vp = ViewportRect();
    const int pageRows = std::max(1, static_cast<int>(vp.h / rowHeight_) - 1);
    int target = selectedRow_;

    switch (e.vk) {
        case VK_DOWN:  target = (selectedRow_ < 0) ? 0 : selectedRow_ + 1; break;
        case VK_UP:    target = (selectedRow_ < 0) ? 0 : selectedRow_ - 1; break;
        case VK_NEXT:  target = (selectedRow_ < 0) ? 0 : selectedRow_ + pageRows; break;
        case VK_PRIOR: target = (selectedRow_ < 0) ? 0 : selectedRow_ - pageRows; break;
        case VK_HOME:  target = 0; break;
        case VK_END:   target = rowCount_ - 1; break;
        default: return;
    }
    target = std::clamp(target, 0, rowCount_ - 1);
    if (target != selectedRow_) {
        SetSelectedRow(target);
        ScrollRowIntoView(target);
    }
    e.handled = true;
}

void DataGrid::OnAnimationTick(float dtSec) {
    scroll_.Tick(dtSec);
    // scroll_ is a member rather than a tree node, so its own Invalidate cannot reach the
    // host. Invalidate here, exactly as ListBox does.
    Invalidate();
}

float DataGrid::VisualOverflowDip() const {
    // Unconditional, not gated on focus: the frame where focus LEAVES still has to clear
    // the ring's pixels, and IsFocused() is already false by then.
    return FocusRingPadDip(FocusRingSpec{});
}

}  // namespace fluent
