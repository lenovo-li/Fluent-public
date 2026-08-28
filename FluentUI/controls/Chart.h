// Chart.h — line / bar / candlestick plot with axes, for quantitative UI.
//
// WHY ONE CONTROL FOR THREE PLOT TYPES. All three share the part that is actually hard
// and worth testing once: mapping data coordinates to pixels (the "scale"), choosing
// readable axis ticks, and clipping to the plot area. The per-type code is only the
// marks. Three separate controls would mean three copies of the scale, and a scale bug in
// one of them.
//
// SCOPE. This is a chart for reading numbers off a screen, not a plotting library: no
// legends with interactive toggles, no zoom/pan gestures, no annotations, no secondary
// axis, no stacked or grouped bars. Those belong in an app-level widget composed from
// this. What it does guarantee is that the pixels are correct — a value at the series
// maximum lands on the top gridline, a zero baseline is a straight line, and a candle's
// wick and body agree.
//
// DATA IS BORROWED, NOT OWNED, and points are pulled through an index accessor rather
// than copied in. A price series is tens of thousands of points; copying it into the
// control on every update would allocate per frame during a live refresh.
//
// VIRTUALIZATION: only points whose x maps inside the plot area are drawn. A 100k-point
// series showing a 200-point window costs the same as a 200-point series. This is the
// same reason DataGrid virtualizes, and it is asserted the same way.
//
// NO GPU-SPECIFIC PATH. Everything is D2D line/rect drawing through DrawingContext, so
// the offscreen pixel tests can verify a chart the same way they verify a focus ring.

#pragma once
#include "../core/Control.h"
#include <functional>
#include <string>
#include <vector>

namespace fluent {

class Chart : public Control {
public:
    enum class Kind {
        Line,         // one or more polylines
        Bar,          // vertical bars from the zero baseline
        Candlestick,  // OHLC boxes with high/low wicks
    };

    // One plotted series. `count` and the accessors describe borrowed data.
    struct Series {
        std::wstring name;
        // Index -> value. Called only for visible indices.
        std::function<double(int index)> value;
        // Candlestick needs four values per index; Line/Bar use `value` only. Kept as
        // separate accessors rather than a struct so a caller with columnar data (the
        // usual shape for OHLC) does not have to repack it.
        std::function<double(int index)> open;
        std::function<double(int index)> high;
        std::function<double(int index)> low;
        std::function<double(int index)> close;
        // Colour override; when unset, Line/Bar use the accent and Candlestick uses the
        // data up/down tokens per candle.
        bool hasColor = false;
        D2D1_COLOR_F color{};
    };

    Chart();

    void SetKind(Kind k);
    Kind GetKind() const { return kind_; }

    // Number of x positions. All series share it — a chart whose series had different
    // lengths would need per-series x mapping, which no consumer needs and which makes
    // the shared scale ambiguous.
    void SetPointCount(int count);
    int PointCount() const { return pointCount_; }

    void AddSeries(Series s);
    void ClearSeries();
    size_t SeriesCount() const { return series_.size(); }

    // Y range. Auto by default: computed from the visible data each Measure, which is what
    // makes a scrolled window rescale to what is on screen. A fixed range is available
    // because comparing two charts side by side requires the same axis.
    void SetAutoScaleY(bool on);
    bool AutoScaleY() const { return autoScaleY_; }
    void SetYRange(double minY, double maxY);
    double MinY() const { return minY_; }
    double MaxY() const { return maxY_; }

    // Visible x window, in point indices. This is how a caller scrolls/zooms: it decides
    // what range to show and the chart maps it. Defaults to the whole series.
    void SetVisibleRange(int firstIndex, int count);
    int VisibleFirst() const { return visibleFirst_; }
    int VisibleCount() const { return visibleCount_; }

    // Optional x tick labels (dates, usually). Index -> text; return empty to skip.
    void SetXLabelProvider(std::function<std::wstring(int index)> provider);

    // Axis / decoration switches. All default on except the y-zero line, which only makes
    // sense when zero is inside the range.
    void SetShowGridLines(bool on);
    void SetShowYAxis(bool on);
    void SetShowXAxis(bool on);

    // --- Geometry, public because it IS the contract ----------------------
    // The plot area: bounds minus axis gutters. Everything below maps into this.
    RectDip PlotRect() const;
    // Data -> pixel. Public so a test can assert that a known value lands on a known
    // pixel, which is the only way to catch an inverted or off-by-one scale; recomputing
    // the mapping inside the test would just assert the test's own copy of it.
    float XToPixel(int index) const;
    float YToPixel(double value) const;
    // Inverse, for hit-testing a hovered point.
    int PixelToIndex(float dipX) const;
    // The y values the horizontal gridlines sit on, after "nice number" rounding. Public
    // because tick selection is the part users notice when it is wrong (labels like
    // 0.7143 instead of 0.75).
    std::vector<double> YTicks() const;

    // --- Element contract -------------------------------------------------
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;

private:
    // Recompute minY_/maxY_ from the visible window when auto-scaling. Called from
    // Measure, not Render: the range affects the axis gutter width (label widths), which
    // is layout, and computing it during Render would make the drawn scale disagree with
    // the measured one on the first frame after data changes.
    void UpdateAutoScale();
    // Width reserved for y-axis labels at the current range.
    float YAxisGutter() const;
    float XAxisGutter() const;
    // Clamp the visible window to the data. A caller that scrolls past the end, or shrinks
    // the data under a stale window, must not produce out-of-range accessor calls.
    void ClampVisibleRange();

    Kind kind_ = Kind::Line;
    int pointCount_ = 0;
    std::vector<Series> series_;

    bool autoScaleY_ = true;
    double minY_ = 0.0;
    double maxY_ = 1.0;

    int visibleFirst_ = 0;
    int visibleCount_ = 0;   // 0 = all points

    std::function<std::wstring(int index)> xLabelProvider_;
    bool showGridLines_ = true;
    bool showYAxis_ = true;
    bool showXAxis_ = true;
};

}  // namespace fluent
