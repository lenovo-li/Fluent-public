// ChartTests.cpp — the data->pixel scale, tick selection, and window virtualization.
//
// WHY THE SCALE IS THE THING TO TEST. Everything a chart gets wrong that a user notices is
// a scale error: an inverted y axis, a series that runs off the plot, bars measured from
// the wrong baseline, a candle body drawn upside down. Those are all arithmetic, so they
// are testable exactly and without a device — and they cannot be caught by looking at a
// screenshot, because a plausible-looking wrong chart looks like a chart.
//
// The mapping functions are public for this reason. A test that recomputed the expected
// pixel from minY/maxY and PlotRect would be asserting its own copy of the formula and
// would pass even if both were wrong the same way; instead these assert ANCHORS that must
// hold for any correct scale: the range maximum lands on the plot top, the minimum on the
// bottom, the midpoint in the middle, and y grows downward.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Chart.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <cstdio>
#include <vector>

using namespace fluent;

namespace {

class MockHost : public WindowServices {
public:
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host) {
    UIContext c;
    c.window = &host;
    c.dpiScale = 1.0f;
    return c;
}

// Shared sample data, held at namespace scope so the Series accessors (plain function
// pointers into a std::function) stay valid for the chart's lifetime.
std::vector<double> g_values;

struct Fixture {
    MockHost host;
    Chart chart;
    Fixture(Chart::Kind kind, int n) {
        chart.AttachToContext(MakeCtx(host));
        chart.SetKind(kind);
        g_values.clear();
        for (int i = 0; i < n; ++i) g_values.push_back(static_cast<double>(i));
        chart.SetPointCount(n);
        Chart::Series s;
        s.value = [](int i) { return g_values[static_cast<size_t>(i)]; };
        s.open  = [](int i) { return g_values[static_cast<size_t>(i)]; };
        s.high  = [](int i) { return g_values[static_cast<size_t>(i)] + 1.0; };
        s.low   = [](int i) { return g_values[static_cast<size_t>(i)] - 1.0; };
        s.close = [](int i) { return g_values[static_cast<size_t>(i)] + 0.5; };
        chart.AddSeries(std::move(s));
        chart.Measure(400.0f, 200.0f);
        chart.Arrange(RectDip{0.0f, 0.0f, 400.0f, 200.0f});
    }
};

}  // namespace

// --- The scale ----------------------------------------------------------------

// The three anchors any correct y scale must satisfy, plus the direction. Stated as
// anchors rather than as a recomputed formula so the test cannot agree with a wrong
// implementation by sharing its arithmetic.
TEST(Chart, YScaleAnchorsRangeToPlotEdgesAndGrowsUpward) {
    Fixture f(Chart::Kind::Line, 10);
    f.chart.SetYRange(0.0, 100.0);
    const RectDip plot = f.chart.PlotRect();

    EXPECT_NEAR(f.chart.YToPixel(100.0), plot.y, 0.01f);          // max at the top
    EXPECT_NEAR(f.chart.YToPixel(0.0), plot.bottom(), 0.01f);     // min at the bottom
    EXPECT_NEAR(f.chart.YToPixel(50.0), plot.y + plot.h * 0.5f, 0.01f);

    // Direction: a LARGER value must map to a SMALLER pixel y. This is the assertion that
    // catches an inverted axis, which is the single most common chart bug.
    EXPECT_TRUE(f.chart.YToPixel(90.0) < f.chart.YToPixel(10.0));
}

// X positions sit at slot centres, not slot edges. Bars and candles are drawn AROUND their
// x, so index 0 at the plot's left edge would have half of its mark clipped away.
TEST(Chart, XPositionsAreSlotCentresSoMarksAreNotClipped) {
    Fixture f(Chart::Kind::Bar, 4);
    const RectDip plot = f.chart.PlotRect();
    const float slot = plot.w / 4.0f;

    EXPECT_NEAR(f.chart.XToPixel(0), plot.x + slot * 0.5f, 0.01f);
    EXPECT_NEAR(f.chart.XToPixel(3), plot.x + slot * 3.5f, 0.01f);
    // Strictly inside the plot on both ends.
    EXPECT_TRUE(f.chart.XToPixel(0) > plot.x);
    EXPECT_TRUE(f.chart.XToPixel(3) < plot.right());
}

// PixelToIndex must invert XToPixel, so a hover reports the point the user is pointing at.
TEST(Chart, PixelToIndexInvertsXToPixel) {
    Fixture f(Chart::Kind::Line, 20);
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(f.chart.PixelToIndex(f.chart.XToPixel(i)), i);
    // Outside the plot is "no point", not a clamped edge index — a caller showing a
    // tooltip must be able to tell "not over the data".
    const RectDip plot = f.chart.PlotRect();
    EXPECT_EQ(f.chart.PixelToIndex(plot.x - 20.0f), -1);
    EXPECT_EQ(f.chart.PixelToIndex(plot.right() + 20.0f), -1);
}

// A degenerate range must not divide by zero or invert. SetYRange is the public door for
// this, and callers do pass a flat range (a constant series).
TEST(Chart, DegenerateYRangeIsWidenedRatherThanDividingByZero) {
    Fixture f(Chart::Kind::Line, 5);
    f.chart.SetYRange(42.0, 42.0);
    EXPECT_TRUE(f.chart.MaxY() > f.chart.MinY());
    const float y = f.chart.YToPixel(42.0);
    EXPECT_TRUE(std::isfinite(y));
}

// --- Auto-scale ---------------------------------------------------------------

// Auto-scale must cover the visible data with headroom, and must NOT be so tight that the
// extreme points land exactly on the border (where a 1.5 DIP stroke is half-clipped).
TEST(Chart, AutoScaleCoversVisibleDataWithHeadroom) {
    Fixture f(Chart::Kind::Line, 100);   // values 0..99
    f.chart.SetVisibleRange(0, 100);
    f.chart.Measure(400.0f, 200.0f);

    std::printf("  auto range for 0..99: [%.2f, %.2f]\n", f.chart.MinY(), f.chart.MaxY());
    EXPECT_TRUE(f.chart.MinY() < 0.0);     // headroom below the minimum
    EXPECT_TRUE(f.chart.MaxY() > 99.0);    // headroom above the maximum
}

// The range must follow the WINDOW, not the whole series: that is what makes a scrolled
// chart rescale to what is on screen instead of being flattened by a far-away outlier.
TEST(Chart, AutoScaleFollowsTheVisibleWindowNotTheWholeSeries) {
    Fixture f(Chart::Kind::Line, 1000);   // values 0..999
    f.chart.SetVisibleRange(0, 10);       // only 0..9 on screen
    f.chart.Measure(400.0f, 200.0f);
    const double maxSmall = f.chart.MaxY();

    f.chart.SetVisibleRange(900, 10);     // now 900..909
    f.chart.Measure(400.0f, 200.0f);
    const double maxLarge = f.chart.MaxY();

    std::printf("  window 0..9 max=%.1f ; window 900..909 max=%.1f\n", maxSmall, maxLarge);
    EXPECT_TRUE(maxSmall < 50.0);
    EXPECT_TRUE(maxLarge > 900.0);
}

// A bar chart must include zero in its range, or the bars are meaningless: values 98..100
// scaled to [98,100] all look like full-height bars.
TEST(Chart, BarChartAutoScaleIncludesTheZeroBaseline) {
    MockHost host;
    Chart chart;
    chart.AttachToContext(MakeCtx(host));
    chart.SetKind(Chart::Kind::Bar);
    g_values.assign({98.0, 99.0, 100.0});
    chart.SetPointCount(3);
    Chart::Series s;
    s.value = [](int i) { return g_values[static_cast<size_t>(i)]; };
    chart.AddSeries(std::move(s));
    chart.Measure(400.0f, 200.0f);

    std::printf("  bar range for 98..100: [%.2f, %.2f]\n", chart.MinY(), chart.MaxY());
    EXPECT_TRUE(chart.MinY() <= 0.0);
}

// Candlestick auto-scale must use high/low, not open/close, or every wick is clipped.
TEST(Chart, CandlestickAutoScaleUsesHighAndLowNotOpenAndClose) {
    Fixture f(Chart::Kind::Candlestick, 50);   // high = v+1, low = v-1
    f.chart.SetVisibleRange(0, 50);
    f.chart.Measure(400.0f, 200.0f);

    // Lowest low is -1 (at index 0) and the highest high is 50 (at index 49); the range
    // must contain both, which the open/close range [0,49.5] would not.
    std::printf("  candle range: [%.2f, %.2f]\n", f.chart.MinY(), f.chart.MaxY());
    EXPECT_TRUE(f.chart.MinY() <= -1.0);
    EXPECT_TRUE(f.chart.MaxY() >= 50.0);
}

// --- Ticks --------------------------------------------------------------------

// Ticks must be round numbers. Without "nice number" rounding an axis over [0, 3.7] shows
// labels like 0.74, 1.48 — correct and unreadable.
TEST(Chart, YTicksAreRoundNumbers) {
    Fixture f(Chart::Kind::Line, 10);
    f.chart.SetYRange(0.0, 100.0);
    const std::vector<double> ticks = f.chart.YTicks();

    EXPECT_TRUE(ticks.size() >= 3);
    for (double t : ticks) {
        // Every tick is a multiple of the step, and the step is a 1/2/5 x 10^n value, so
        // each tick times 100 must be a whole number for any sane range.
        const double scaled = t * 100.0;
        EXPECT_NEAR(scaled - std::floor(scaled + 0.5), 0.0, 1e-6);
    }
    std::printf("  ticks for [0,100]:");
    for (double t : ticks) std::printf(" %g", t);
    std::printf("\n");
}

// Every tick must be inside the range, or a label is drawn outside the plot.
TEST(Chart, YTicksStayInsideTheRange) {
    Fixture f(Chart::Kind::Line, 10);
    f.chart.SetYRange(-3.3, 7.7);
    for (double t : f.chart.YTicks()) {
        EXPECT_TRUE(t >= f.chart.MinY() - 1e-9);
        EXPECT_TRUE(t <= f.chart.MaxY() + 1e-9);
    }
}

// --- Window virtualization ----------------------------------------------------

// The visible window must clamp to the data. A caller that scrolls past the end must not
// cause the chart to call an accessor with an out-of-range index — that would read past
// the app's own array.
TEST(Chart, VisibleWindowClampsToTheDataInsteadOfReadingPastIt) {
    Fixture f(Chart::Kind::Line, 100);

    f.chart.SetVisibleRange(90, 50);          // asks for 90..139, only 90..99 exist
    EXPECT_EQ(f.chart.VisibleFirst(), 90);
    EXPECT_EQ(f.chart.VisibleCount(), 10);

    f.chart.SetVisibleRange(500, 10);         // entirely past the end
    EXPECT_TRUE(f.chart.VisibleFirst() < 100);
    EXPECT_TRUE(f.chart.VisibleFirst() + f.chart.VisibleCount() <= 100);

    // Shrinking the data under a stale window must also clamp.
    f.chart.SetVisibleRange(0, 100);
    f.chart.SetPointCount(5);
    EXPECT_TRUE(f.chart.VisibleFirst() + f.chart.VisibleCount() <= 5);
}

// A count of 0 means "all points" — the documented default, and what a caller gets when it
// never sets a window at all.
TEST(Chart, ZeroVisibleCountMeansEveryPoint) {
    Fixture f(Chart::Kind::Line, 250);
    f.chart.SetVisibleRange(0, 0);
    EXPECT_EQ(f.chart.VisibleCount(), 250);
}

// The plot area must leave room for the axis labels, and give it back when the axes are
// switched off — otherwise a sparkline wastes a third of its width on an invisible gutter.
TEST(Chart, AxisGuttersAreReclaimedWhenAxesAreHidden) {
    Fixture f(Chart::Kind::Line, 10);
    const RectDip withAxes = f.chart.PlotRect();

    f.chart.SetShowYAxis(false);
    f.chart.SetShowXAxis(false);
    const RectDip bare = f.chart.PlotRect();

    std::printf("  plot w/h with axes %.0fx%.0f, without %.0fx%.0f\n",
                withAxes.w, withAxes.h, bare.w, bare.h);
    EXPECT_TRUE(bare.w > withAxes.w);
    EXPECT_TRUE(bare.h > withAxes.h);
    EXPECT_NEAR(bare.x, f.chart.Bounds().x, 0.01f);   // no left gutter left over
}
