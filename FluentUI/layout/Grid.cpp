// Grid.cpp

#include "Grid.h"
#include <algorithm>

namespace fluent {

void Grid::SetCell(FrameworkElement* child, int row, int col, int rowSpan, int colSpan) {
    if (!child) return;
    Cell c;
    c.row = std::max(0, row);
    c.col = std::max(0, col);
    c.rowSpan = std::max(1, rowSpan);
    c.colSpan = std::max(1, colSpan);
    cells_[child] = c;
}

Grid::Cell Grid::CellOf(const FrameworkElement* child) const {
    auto it = cells_.find(child);
    return it == cells_.end() ? Cell{} : it->second;
}

// ---------------------------------------------------------------------------
// Track math
// ---------------------------------------------------------------------------

std::vector<float> Grid::ResolveTracks(const std::vector<GridLength>& tracks,
                                       const std::vector<float>& autoSizes,
                                       float spacing, float avail) {
    const int n = static_cast<int>(tracks.size());
    std::vector<float> sizes(n, 0.0f);

    float used = 0.0f;
    float starWeight = 0.0f;
    for (int i = 0; i < n; ++i) {
        const GridLength& t = tracks[i];
        if (t.isPixel()) { sizes[i] = std::max(0.0f, t.value); used += sizes[i]; }
        else if (t.isAuto()) { sizes[i] = autoSizes[i]; used += sizes[i]; }
        else starWeight += std::max(0.0f, t.value);  // Star: sized below.
    }
    if (n > 1) used += spacing * (n - 1);

    float leftover = std::max(0.0f, avail - used);
    if (starWeight > 0.0f) {
        for (int i = 0; i < n; ++i)
            if (tracks[i].isStar())
                sizes[i] = leftover * (std::max(0.0f, tracks[i].value) / starWeight);
    }
    return sizes;
}

std::vector<float> Grid::TrackOffsets(const std::vector<float>& sizes,
                                      float spacing, float origin) {
    std::vector<float> offsets(sizes.size(), origin);
    float pos = origin;
    for (size_t i = 0; i < sizes.size(); ++i) {
        offsets[i] = pos;
        pos += sizes[i] + spacing;
    }
    return offsets;
}

float Grid::SpanExtent(const std::vector<float>& sizes, int start, int span,
                       float spacing) {
    const int n = static_cast<int>(sizes.size());
    if (n == 0) return 0.0f;
    start = std::clamp(start, 0, n - 1);
    int end = std::min(n, start + std::max(1, span));  // exclusive
    float extent = 0.0f;
    for (int i = start; i < end; ++i) extent += sizes[i];
    if (end - start > 1) extent += spacing * (end - start - 1);
    return extent;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

SizeDip Grid::MeasureOverride(float availW, float availH) {
    // Degenerate case: no tracks defined -> a single cell filling the bounds.
    // Measure children against the full space and report the largest desired.
    if (columns_.empty() && rows_.empty()) {
        SizeDip out;
        for (auto& cptr : children_) {
            FrameworkElement* c = cptr.get();
            if (!c || !c->IsVisible()) continue;
            c->MeasureCached(availW, availH);
            const SizeDip& d = c->Desired();
            out.w = std::max(out.w, d.w + c->Margin().horizontal());
            out.h = std::max(out.h, d.h + c->Margin().vertical());
        }
        return out;
    }

    const int nCols = std::max(1, static_cast<int>(columns_.size()));
    const int nRows = std::max(1, static_cast<int>(rows_.size()));

    // Resolve tracks. Empty track lists act as a single Star track.
    std::vector<GridLength> cols = columns_.empty()
        ? std::vector<GridLength>{GridLength::Star()} : columns_;
    std::vector<GridLength> rws = rows_.empty()
        ? std::vector<GridLength>{GridLength::Star()} : rows_;

    // Pass 1: natural desired sizes against the full space. Only WIDTHS are taken
    // from this pass; heights are re-measured in pass 2 at the final column width.
    std::vector<float> autoW(nCols, 0.0f);
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        c->MeasureCached(availW, availH);
        Cell cell = CellOf(c);
        const SizeDip& d = c->Desired();
        if (cell.colSpan == 1 && cell.col < nCols && !columns_.empty() &&
            columns_[cell.col].isAuto())
            autoW[cell.col] = std::max(autoW[cell.col], d.w + c->Margin().horizontal());
    }

    std::vector<float> colSizes = ResolveTracks(cols, autoW, colSpacing_, availW);

    // Pass 2: re-measure each child at its FINAL column width, and take the Auto row
    // heights from THIS pass.
    //
    // A child's height depends on its width, so reading Auto row heights from pass 1
    // (full grid width) would read them at the wrong width — anything that wraps
    // (a card with a wrapped description) would under-report and overflow the grid's
    // box.
    //
    // The height constraint stays availH rather than the resolved row height, because
    // the row heights are what this pass is computing. Under a ScrollPanel availH is
    // infinity, which is exactly right: report the natural height.
    std::vector<float> autoH(nRows, 0.0f);
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        Cell cell = CellOf(c);
        const float cw = SpanExtent(colSizes, cell.col, cell.colSpan, colSpacing_);
        c->MeasureCached(std::max(0.0f, cw - c->Margin().horizontal()), availH);
        const SizeDip& d = c->Desired();
        if (cell.rowSpan == 1 && cell.row < nRows && !rows_.empty() &&
            rows_[cell.row].isAuto())
            autoH[cell.row] = std::max(autoH[cell.row], d.h + c->Margin().vertical());
    }

    std::vector<float> rowSizes = ResolveTracks(rws, autoH, rowSpacing_, availH);

    // The grid's desired size is the sum of its resolved tracks + spacing.
    SizeDip out;
    for (float w : colSizes) out.w += w;
    for (float h : rowSizes) out.h += h;
    if (colSizes.size() > 1) out.w += colSpacing_ * (colSizes.size() - 1);
    if (rowSizes.size() > 1) out.h += rowSpacing_ * (rowSizes.size() - 1);
    return out;
}

void Grid::ArrangeOverride(const RectDip& content) {
    // Recompute track content sizes (Auto) from the children's measured desired
    // sizes, then resolve against the actual content extent.
    std::vector<GridLength> cols = columns_.empty()
        ? std::vector<GridLength>{GridLength::Star()} : columns_;
    std::vector<GridLength> rws = rows_.empty()
        ? std::vector<GridLength>{GridLength::Star()} : rows_;
    const int nCols = static_cast<int>(cols.size());
    const int nRows = static_cast<int>(rws.size());

    std::vector<float> autoW(nCols, 0.0f), autoH(nRows, 0.0f);
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        Cell cell = CellOf(c);
        const SizeDip& d = c->Desired();
        if (cell.colSpan == 1 && cell.col < nCols && cols[cell.col].isAuto())
            autoW[cell.col] = std::max(autoW[cell.col], d.w + c->Margin().horizontal());
        if (cell.rowSpan == 1 && cell.row < nRows && rws[cell.row].isAuto())
            autoH[cell.row] = std::max(autoH[cell.row], d.h + c->Margin().vertical());
    }

    std::vector<float> colSizes = ResolveTracks(cols, autoW, colSpacing_, content.w);
    std::vector<float> rowSizes = ResolveTracks(rws, autoH, rowSpacing_, content.h);
    std::vector<float> colOff = TrackOffsets(colSizes, colSpacing_, content.x);
    std::vector<float> rowOff = TrackOffsets(rowSizes, rowSpacing_, content.y);

    // Cache resolved sizes for GridSplitter runtime queries (roadmap §5).
    resolvedCols_ = colSizes;
    resolvedRows_ = rowSizes;

    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        Cell cell = CellOf(c);
        int col = std::clamp(cell.col, 0, nCols - 1);
        int row = std::clamp(cell.row, 0, nRows - 1);
        RectDip slot = {colOff[col], rowOff[row],
                        SpanExtent(colSizes, col, cell.colSpan, colSpacing_),
                        SpanExtent(rowSizes, row, cell.rowSpan, rowSpacing_)};
        ArrangeChild(c, slot);  // honors the child's margin + alignment
    }
}

// ---------------------------------------------------------------------------
// Runtime track resizing (roadmap §5 — GridSplitter)
// ---------------------------------------------------------------------------

void Grid::SetColumnAt(size_t i, GridLength w) {
    if (i >= columns_.size()) return;
    columns_[i] = w;
    InvalidateMeasure();
}

void Grid::SetRowAt(size_t i, GridLength h) {
    if (i >= rows_.size()) return;
    rows_[i] = h;
    InvalidateMeasure();
}

int Grid::ColumnOf(const FrameworkElement* child) const {
    auto it = cells_.find(child);
    return it == cells_.end() ? -1 : it->second.col;
}

int Grid::RowOf(const FrameworkElement* child) const {
    auto it = cells_.find(child);
    return it == cells_.end() ? -1 : it->second.row;
}

} // namespace fluent
