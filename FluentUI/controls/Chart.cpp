// Chart.cpp — see Chart.h.

#include "Chart.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/DWriteContext.h"
#include "../styling/ThemeTokens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace fluent {

namespace {
constexpr float kYLabelWidth = 52.0f;   // reserved for y tick text
constexpr float kXLabelHeight = 18.0f;  // reserved for x tick text
constexpr float kPlotPadTop = 6.0f;     // headroom so the top mark is not on the border
constexpr float kSeriesStroke = 1.5f;
constexpr float kBarGapRatio = 0.25f;   // fraction of a slot left as gap
constexpr int kTargetYTicks = 5;

// Round a raw step up to a "nice" 1/2/5 x 10^n value. Without this the axis shows labels
// like 0.7143 — technically correct, unreadable in practice.
double NiceStep(double raw) {
    if (raw <= 0.0 || !std::isfinite(raw)) return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double nice;
    if (norm <= 1.0) nice = 1.0;
    else if (norm <= 2.0) nice = 2.0;
    else if (norm <= 5.0) nice = 5.0;
    else nice = 10.0;
    return nice * mag;
}
}  // namespace

Chart::Chart() {
    SetFocusable(false);   // read-only display; no gestures yet
    SetHAlign(HAlign::Stretch);
    SetVAlign(VAlign::Stretch);
}

void Chart::SetKind(Kind k) {
    if (kind_ == k) return;
    kind_ = k;
    // Measure-level: candlesticks need the OHLC range, so the auto-scaled y range (and
    // therefore the label gutter) can differ from a line chart over the same data.
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::SetPointCount(int count) {
    const int c = std::max(0, count);
    if (pointCount_ == c) return;
    pointCount_ = c;
    ClampVisibleRange();
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::AddSeries(Series s) {
    series_.push_back(std::move(s));
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::ClearSeries() {
    series_.clear();
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::SetAutoScaleY(bool on) {
    if (autoScaleY_ == on) return;
    autoScaleY_ = on;
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::SetYRange(double minY, double maxY) {
    autoScaleY_ = false;
    // Guard against an inverted or degenerate range: a zero-height range divides by zero
    // in YToPixel, and an inverted one silently flips the chart.
    if (maxY <= minY) maxY = minY + 1.0;
    minY_ = minY;
    maxY_ = maxY;
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::SetVisibleRange(int firstIndex, int count) {
    visibleFirst_ = std::max(0, firstIndex);
    visibleCount_ = std::max(0, count);
    ClampVisibleRange();
    InvalidateDirty(DirtyFlags::Measure);
}

void Chart::SetXLabelProvider(std::function<std::wstring(int)> provider) {
    xLabelProvider_ = std::move(provider);
    Invalidate();
}

void Chart::SetShowGridLines(bool on) { if (showGridLines_ != on) { showGridLines_ = on; Invalidate(); } }
void Chart::SetShowYAxis(bool on) { if (showYAxis_ != on) { showYAxis_ = on; InvalidateDirty(DirtyFlags::Measure); } }
void Chart::SetShowXAxis(bool on) { if (showXAxis_ != on) { showXAxis_ = on; InvalidateDirty(DirtyFlags::Measure); } }

void Chart::ClampVisibleRange() {
    if (pointCount_ <= 0) { visibleFirst_ = 0; visibleCount_ = 0; return; }
    visibleFirst_ = std::clamp(visibleFirst_, 0, pointCount_ - 1);
    const int maxCount = pointCount_ - visibleFirst_;
    if (visibleCount_ <= 0 || visibleCount_ > maxCount) visibleCount_ = maxCount;
}

float Chart::YAxisGutter() const { return showYAxis_ ? kYLabelWidth : 0.0f; }
float Chart::XAxisGutter() const { return showXAxis_ ? kXLabelHeight : 0.0f; }

RectDip Chart::PlotRect() const {
    const float left = bounds_.x + YAxisGutter();
    const float top = bounds_.y + kPlotPadTop;
    const float w = std::max(0.0f, bounds_.right() - left);
    const float h = std::max(0.0f, bounds_.bottom() - XAxisGutter() - top);
    return RectDip{left, top, w, h};
}

float Chart::XToPixel(int index) const {
    const RectDip plot = PlotRect();
    if (visibleCount_ <= 0) return plot.x;
    if (visibleCount_ == 1) return plot.x + plot.w * 0.5f;
    // Points sit at slot CENTRES, not slot edges: a bar or candle is drawn around its x,
    // and putting index 0 hard against the axis would clip half of it.
    const float slot = plot.w / static_cast<float>(visibleCount_);
    const int rel = index - visibleFirst_;
    return plot.x + slot * (static_cast<float>(rel) + 0.5f);
}

float Chart::YToPixel(double value) const {
    const RectDip plot = PlotRect();
    const double range = maxY_ - minY_;
    if (range <= 0.0) return plot.bottom();
    // Inverted: data grows upward, pixels grow downward.
    const double t = (value - minY_) / range;
    return static_cast<float>(plot.bottom() - t * plot.h);
}

int Chart::PixelToIndex(float dipX) const {
    const RectDip plot = PlotRect();
    if (visibleCount_ <= 0 || plot.w <= 0.0f) return -1;
    const float slot = plot.w / static_cast<float>(visibleCount_);
    const int rel = static_cast<int>((dipX - plot.x) / slot);
    const int idx = visibleFirst_ + rel;
    if (idx < visibleFirst_ || idx >= visibleFirst_ + visibleCount_) return -1;
    return idx;
}

std::vector<double> Chart::YTicks() const {
    std::vector<double> ticks;
    const double range = maxY_ - minY_;
    if (range <= 0.0) return ticks;
    const double step = NiceStep(range / kTargetYTicks);
    // Start at the first nice multiple at or above minY so labels are round numbers rather
    // than minY + k*step (which reintroduces the ugly values NiceStep exists to avoid).
    double v = std::ceil(minY_ / step) * step;
    for (int guard = 0; v <= maxY_ + step * 1e-6 && guard < 64; ++guard, v += step)
        ticks.push_back(v);
    return ticks;
}

void Chart::UpdateAutoScale() {
    if (!autoScaleY_ || series_.empty() || visibleCount_ <= 0) return;

    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    const int last = visibleFirst_ + visibleCount_ - 1;

    for (const Series& s : series_) {
        for (int i = visibleFirst_; i <= last; ++i) {
            if (kind_ == Kind::Candlestick) {
                // High/low, not open/close: the wick is what defines the extent, and
                // scaling to the bodies would clip every wick.
                if (s.high) hi = std::max(hi, s.high(i));
                if (s.low) lo = std::min(lo, s.low(i));
            } else if (s.value) {
                const double v = s.value(i);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
    }
    if (!std::isfinite(lo) || !std::isfinite(hi)) return;

    if (kind_ == Kind::Bar) {
        // Bars are read against zero, so the baseline must be in range or the bar lengths
        // are meaningless (a bar from 98 to 100 looks like a full-height bar).
        lo = std::min(lo, 0.0);
        hi = std::max(hi, 0.0);
    }
    if (hi <= lo) { hi = lo + 1.0; }     // flat series: give it a visible band
    else {
        const double pad = (hi - lo) * 0.05;   // 5% headroom top and bottom
        lo -= pad;
        hi += pad;
    }
    minY_ = lo;
    maxY_ = hi;
}

void Chart::Measure(float availW, float availH) {
    ClampVisibleRange();
    UpdateAutoScale();
    // A chart has no intrinsic size: it fills what it is given. Fall back to a usable
    // default when offered nothing (a Canvas child, or an unconstrained measure pass).
    const float w = availW > 0.0f ? availW : 320.0f;
    const float h = availH > 0.0f ? availH : 200.0f;
    SetDesired({IsAuto(width_) ? w : width_, IsAuto(height_) ? h : height_});
}

void Chart::Render(const DrawingContext& dc) {
    if (!dc.Dc()) return;
    const ColorTokens& c = Theme().colors;
    const TypographyTokens& typo = Theme().typography;
    const RectDip plot = PlotRect();
    if (plot.w <= 0.0f || plot.h <= 0.0f) return;

    DWriteContext* dw = Dwrite();
    const std::vector<double> ticks = YTicks();

    // --- Gridlines + y labels (behind the data) ---------------------------
    if (showGridLines_ || showYAxis_) {
        for (double t : ticks) {
            const float y = YToPixel(t);
            if (y < plot.y - 0.5f || y > plot.bottom() + 0.5f) continue;
            if (showGridLines_)
                dc.DrawLine(D2D1::Point2F(plot.x, y), D2D1::Point2F(plot.right(), y),
                            c.gridLine, 1.0f);
            if (showYAxis_ && dw && dw->Valid()) {
                wchar_t buf[32];
                // %g keeps round numbers short (100 not 100.000000) and falls back to
                // scientific notation for extreme ranges rather than printing 20 digits.
                std::swprintf(buf, 32, L"%g", t);
                if (IDWriteTextFormat* fmt = dw->Format(
                        typo.captionSize, DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_TEXT_ALIGNMENT_TRAILING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                        DWRITE_WORD_WRAPPING_NO_WRAP)) {
                    dc.DrawText(buf, static_cast<UINT32>(wcslen(buf)), fmt,
                                D2D1::RectF(bounds_.x, y - 8.0f,
                                            plot.x - 4.0f, y + 8.0f),
                                c.textSecondary);
                }
            }
        }
    }

    // Zero baseline, drawn stronger than a gridline. Only when zero is actually inside the
    // range — otherwise it would be pinned to an edge and read as an axis that is not there.
    if (minY_ < 0.0 && maxY_ > 0.0) {
        const float y0 = YToPixel(0.0);
        dc.DrawLine(D2D1::Point2F(plot.x, y0), D2D1::Point2F(plot.right(), y0),
                    c.controlStrokeDefault, 1.0f);
    }

    // --- Data, clipped to the plot area ----------------------------------
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(plot.x, plot.y, plot.right(), plot.bottom()));
        const int last = visibleFirst_ + visibleCount_ - 1;

        for (const Series& s : series_) {
            const D2D1_COLOR_F lineColor = s.hasColor ? s.color : c.accent;

            if (kind_ == Kind::Line && s.value) {
                // Segment-by-segment rather than a geometry: a path would have to be
                // rebuilt whenever the window scrolls, and DrawLine is already one op.
                for (int i = visibleFirst_; i < last; ++i) {
                    dc.DrawLine(D2D1::Point2F(XToPixel(i), YToPixel(s.value(i))),
                                D2D1::Point2F(XToPixel(i + 1), YToPixel(s.value(i + 1))),
                                lineColor, kSeriesStroke);
                }
            } else if (kind_ == Kind::Bar && s.value) {
                const float slot = plot.w / static_cast<float>(std::max(1, visibleCount_));
                const float barW = std::max(1.0f, slot * (1.0f - kBarGapRatio));
                const float zeroY = (minY_ <= 0.0 && maxY_ >= 0.0)
                                        ? YToPixel(0.0) : plot.bottom();
                for (int i = visibleFirst_; i <= last; ++i) {
                    const float cx = XToPixel(i);
                    const float vy = YToPixel(s.value(i));
                    const float top = std::min(vy, zeroY);
                    const float bot = std::max(vy, zeroY);
                    // Negative bars get the "down" colour when the caller did not pin one:
                    // a chart of profit/loss is unreadable in a single hue.
                    const D2D1_COLOR_F fill =
                        s.hasColor ? s.color
                                   : (s.value(i) >= 0.0 ? c.dataPositive : c.dataNegative);
                    dc.FillRect(D2D1::RectF(cx - barW * 0.5f, top,
                                            cx + barW * 0.5f, bot), fill);
                }
            } else if (kind_ == Kind::Candlestick && s.open && s.high && s.low && s.close) {
                const float slot = plot.w / static_cast<float>(std::max(1, visibleCount_));
                const float bodyW = std::max(1.0f, slot * (1.0f - kBarGapRatio));
                for (int i = visibleFirst_; i <= last; ++i) {
                    const double o = s.open(i), h = s.high(i);
                    const double l = s.low(i), cl = s.close(i);
                    const float cx = XToPixel(i);
                    const bool up = cl >= o;
                    const D2D1_COLOR_F color =
                        s.hasColor ? s.color : (up ? c.dataPositive : c.dataNegative);

                    // Wick first, so the body covers its middle.
                    dc.DrawLine(D2D1::Point2F(cx, YToPixel(h)),
                                D2D1::Point2F(cx, YToPixel(l)), color, 1.0f);
                    float top = YToPixel(std::max(o, cl));
                    float bot = YToPixel(std::min(o, cl));
                    // A doji (open == close) has zero body height and would vanish; give it
                    // one DIP so the price level is still visible.
                    if (bot - top < 1.0f) bot = top + 1.0f;
                    dc.FillRect(D2D1::RectF(cx - bodyW * 0.5f, top,
                                            cx + bodyW * 0.5f, bot), color);
                }
            }
        }
    }

    // --- X labels ---------------------------------------------------------
    if (showXAxis_ && xLabelProvider_ && dw && dw->Valid() && visibleCount_ > 0) {
        // At most a handful of labels regardless of window width: drawing one per point
        // would overlap into mush and cost O(visible) text layouts.
        const int maxLabels = std::max(1, static_cast<int>(plot.w / 70.0f));
        const int stride = std::max(1, visibleCount_ / maxLabels);
        const int last = visibleFirst_ + visibleCount_ - 1;
        for (int i = visibleFirst_; i <= last; i += stride) {
            const std::wstring text = xLabelProvider_(i);
            if (text.empty()) continue;
            if (IDWriteTextFormat* fmt = dw->Format(
                    typo.captionSize, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                    DWRITE_WORD_WRAPPING_NO_WRAP)) {
                const float cx = XToPixel(i);
                dc.DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt,
                            D2D1::RectF(cx - 35.0f, plot.bottom(),
                                        cx + 35.0f, bounds_.bottom()),
                            c.textSecondary);
            }
        }
    }
}

}  // namespace fluent
