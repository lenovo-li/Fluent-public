// StockAnalyzerShapeTests.cpp — the composite shapes the A-share replica page relies on.
//
// WHY THIS FILE EXISTS. The replica page (FluentUIDemo/pages/StockAnalyzerPage.cpp) is the
// deliverable that answers "can this framework express that application's UI". But a demo
// page proves nothing on its own: it lives in an exe nobody runs in CI, and "it compiles"
// says nothing about whether the composite actually lays out. The page cannot be
// instantiated from here either — it is a GalleryApp member in the demo project.
//
// So this file rebuilds the same COMPOSITIONS the page uses (chart inside a card inside a
// scrolling column; a wide grid inside a fixed-height tab; a KPI strip in a UniformGrid)
// and asserts they survive Measure/Arrange with sane geometry. That catches the class of
// failure a demo page hits first: a control that reports a zero or absurd desired size
// inside a real container, or one that divides by a zero the container legitimately hands
// it during an early layout pass.
//
// These are deliberately shape assertions, not pixel assertions. Whether the page LOOKS
// right is something only a human at the running exe can judge, and the report says so.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Chart.h"
#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/UniformGrid.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/styling/ThemeManager.h"

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

const ThemeSnapshot& Theme1() {
    static const ThemeSnapshot t = BuildSnapshot(ThemeInputs{}, 0);
    return t;
}

struct Env {
    MockHost host;
    UIContext ctx;
    Env() {
        (void)host.DWrite().Initialize();
        ctx.window = &host;
        ctx.theme = &Theme1();
        ctx.dwrite = &host.DWrite();   // must be set or text controls take estimate paths
        ctx.dpiScale = 1.0f;
    }
};

std::vector<double> g_series;

}  // namespace

// The page's core composition: a card (Border + StackPanel) holding a fixed-height Chart,
// all inside a padded ScrollPanel, exactly as CreateExampleCard produces. If a Chart
// reported a zero height here, the demo page would show an invisible plot.
TEST(StockAnalyzerShape, ChartInsideACardInsideAScrollPanelGetsRealBounds) {
    Env env;
    ScrollPanel scroll;
    scroll.AttachToContext(env.ctx);
    scroll.SetPadding(24.0f);

    auto* column = scroll.Add(std::make_unique<StackPanel>());
    column->SetOrientation(StackPanel::Orientation::Vertical);

    auto* card = column->Add(std::make_unique<Border>());
    card->SetPadding(Thickness{16.0f});
    auto* cardBody = card->SetChild(std::make_unique<StackPanel>());

    auto* chart = cardBody->Add(std::make_unique<Chart>());
    chart->SetHeight(260.0f);
    chart->SetKind(Chart::Kind::Candlestick);
    g_series.clear();
    for (int i = 0; i < 240; ++i) g_series.push_back(40.0 + i * 0.05);
    chart->SetPointCount(240);
    Chart::Series s;
    s.open  = [](int i) { return g_series[static_cast<size_t>(i)]; };
    s.high  = [](int i) { return g_series[static_cast<size_t>(i)] + 1.0; };
    s.low   = [](int i) { return g_series[static_cast<size_t>(i)] - 1.0; };
    s.close = [](int i) { return g_series[static_cast<size_t>(i)] + 0.3; };
    chart->AddSeries(std::move(s));
    chart->SetVisibleRange(150, 90);

    scroll.Measure(900.0f, 700.0f);
    scroll.Arrange(RectDip{0.0f, 0.0f, 900.0f, 700.0f});

    const RectDip plot = chart->PlotRect();
    std::printf("  chart bounds %.0fx%.0f, plot %.0fx%.0f\n",
                chart->Bounds().w, chart->Bounds().h, plot.w, plot.h);

    EXPECT_NEAR(chart->Bounds().h, 260.0f, 1.0f);
    // Real width: 900 minus scroll padding (48) minus card padding (32) and border.
    EXPECT_TRUE(chart->Bounds().w > 700.0f);
    EXPECT_TRUE(plot.w > 0.0f && plot.h > 0.0f);
    // The plot must be strictly inside the control, with the axis gutters accounted for.
    EXPECT_TRUE(plot.x >= chart->Bounds().x);
    EXPECT_TRUE(plot.bottom() <= chart->Bounds().bottom() + 0.01f);
}

// A zero-sized layout pass must not crash or produce NaN. Containers legitimately offer
// zero during early passes and when a page is collapsed, and a chart that divided by a
// zero plot width would take the process down.
TEST(StockAnalyzerShape, ChartSurvivesAZeroSizedLayoutPass) {
    Env env;
    Chart chart;
    chart.AttachToContext(env.ctx);
    chart.SetKind(Chart::Kind::Line);
    g_series.assign({1.0, 2.0, 3.0});
    chart.SetPointCount(3);
    Chart::Series s;
    s.value = [](int i) { return g_series[static_cast<size_t>(i)]; };
    chart.AddSeries(std::move(s));

    chart.Measure(0.0f, 0.0f);
    chart.Arrange(RectDip{0.0f, 0.0f, 0.0f, 0.0f});

    const RectDip plot = chart.PlotRect();
    EXPECT_TRUE(plot.w >= 0.0f);
    EXPECT_TRUE(plot.h >= 0.0f);
    // Mapping must stay finite even with no room to draw in.
    EXPECT_TRUE(std::isfinite(chart.YToPixel(2.0)));
    EXPECT_TRUE(std::isfinite(chart.XToPixel(1)));
    // -1 ("no point here") is the CORRECT answer for a zero-width plot, not a bug: there
    // is no pixel that maps to a data index. The first version of this test asserted 0 and
    // was simply wrong — a hit test on a collapsed chart must report a miss, otherwise a
    // tooltip would appear for a chart with no visible area.
    EXPECT_EQ(chart.PixelToIndex(0.0f), -1);
}

// The wide financial table: 41 columns in a fixed-height tab body. This is the shape that
// makes column virtualization matter, and the assertion is that only a handful of the 41
// are live at any moment.
TEST(StockAnalyzerShape, WideFinancialTableVirtualizesColumnsInATabSizedBox) {
    Env env;
    DataGrid grid;
    grid.AttachToContext(env.ctx);

    std::vector<DataGrid::Column> cols;
    cols.push_back({L"科目", 140.0f, DataGrid::Align::Left});
    for (int p = 0; p < 40; ++p)
        cols.push_back({L"2020Q1", 84.0f, DataGrid::Align::Right});
    grid.SetColumns(std::move(cols));
    grid.SetRowCount(60);
    grid.SetCellProvider([](int, int) { return std::wstring(L"123.45"); });

    // The tab body the page gives it.
    grid.Measure(820.0f, 220.0f);
    grid.Arrange(RectDip{0.0f, 0.0f, 820.0f, 220.0f});

    int fc = 0, lc = -1, fr = 0, lr = -1;
    grid.VisibleColumnRange(fc, lc);
    grid.VisibleRowRange(fr, lr);
    const int liveCols = lc - fc + 1;
    const int liveRows = lr - fr + 1;
    std::printf("  41 cols x 60 rows in 820x220 -> %d cols, %d rows live (%d cells)\n",
                liveCols, liveRows, liveCols * liveRows);

    EXPECT_TRUE(liveCols > 0);
    EXPECT_TRUE(liveCols < 15);        // a screenful of 84 DIP columns, not 41
    EXPECT_TRUE(liveRows > 0);
    EXPECT_TRUE(liveRows < 20);
    // Total extent still describes the whole table, so the scrollbar is proportional.
    EXPECT_NEAR(grid.TotalContentWidth(), 140.0f + 40.0f * 84.0f, 0.5f);
}

// The KPI strip: four Metrics in a UniformGrid of 4 columns, as st.columns produces. Each
// must get a quarter of the width and a non-zero height, or the dashboard row collapses.
TEST(StockAnalyzerShape, KpiStripSplitsWidthEvenlyAndHasHeight) {
    // Grid with four Star columns, NOT UniformGrid.
    //
    // This is a real finding about the framework, worth stating: UniformGrid sizes its cell
    // to the LARGEST CHILD'S DESIRED size (it measures children unbounded, then uses that
    // max as cellW_) and never expands cells to fill the arranged width. So a four-up
    // UniformGrid in an 800 DIP row gives each child ~63 DIP — the text width — and leaves
    // the remaining 550 DIP empty. That is defensible for its stated purpose (a uniform
    // grid of equal, content-sized cells like a calendar) but it is NOT the st.columns
    // shape, which divides the available width evenly. Grid + Star tracks is.
    Env env;
    Grid strip;
    strip.AttachToContext(env.ctx);
    strip.SetHeight(58.0f);
    strip.AddRow(GridLength::Star(1.0f));
    for (int i = 0; i < 4; ++i) strip.AddColumn(GridLength::Star(1.0f));

    Metric* metrics[4] = {};
    const wchar_t* labels[4] = {L"最新价", L"市盈率 TTM", L"市净率", L"资产负债率"};
    for (int i = 0; i < 4; ++i) {
        metrics[i] = strip.Add(std::make_unique<Metric>());
        strip.SetCell(metrics[i], 0, i);
        metrics[i]->SetLabel(labels[i]);
        metrics[i]->SetValue(L"42.35");
        metrics[i]->SetDelta(L"+1.24%", Metric::Trend::Up);
    }
    metrics[3]->SetInverted(true);   // 负债率上升是坏消息

    strip.Measure(800.0f, 58.0f);
    strip.Arrange(RectDip{0.0f, 0.0f, 800.0f, 58.0f});

    std::printf("  metric widths: %.0f %.0f %.0f %.0f\n",
                metrics[0]->Bounds().w, metrics[1]->Bounds().w,
                metrics[2]->Bounds().w, metrics[3]->Bounds().w);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(metrics[i]->Bounds().w, 200.0f, 1.0f);
        EXPECT_TRUE(metrics[i]->Bounds().h > 0.0f);
    }
    // The inverted one must resolve Up to the NEGATIVE colour: that is the whole point of
    // the flag, and the page depends on it for 资产负债率.
    const ColorTokens& c = Theme1().colors;
    const D2D1_COLOR_F up3 = metrics[3]->TrendColor(Metric::Trend::Up);
    const D2D1_COLOR_F up0 = metrics[0]->TrendColor(Metric::Trend::Up);
    EXPECT_TRUE(up3.r == c.dataNegative.r && up3.g == c.dataNegative.g);
    EXPECT_TRUE(up0.r == c.dataPositive.r && up0.g == c.dataPositive.g);
}

// A stack of InfoBars in a narrow column, the page's disclaimer + error notices. Each must
// take the width offered and grow tall enough for its wrapped text, and they must not
// overlap — the failure that would make the page look broken.
TEST(StockAnalyzerShape, StackedInfoBarsWrapAndDoNotOverlap) {
    Env env;
    if (!env.host.DWrite().Valid()) { std::printf("  [SKIP] no DWrite\n"); return; }

    StackPanel column;
    column.AttachToContext(env.ctx);
    column.SetOrientation(StackPanel::Orientation::Vertical);
    column.SetSpacing(8.0f);

    InfoBar* bars[3] = {};
    const InfoBar::Severity sev[3] = {InfoBar::Severity::Warning,
                                      InfoBar::Severity::Error,
                                      InfoBar::Severity::Informational};
    for (int i = 0; i < 3; ++i) {
        bars[i] = column.Add(std::make_unique<InfoBar>());
        bars[i]->SetSeverity(sev[i]);
        bars[i]->SetTitle(L"标题");
        bars[i]->SetMessage(
            L"本页所有数字都是进程内生成的合成数据，用于验证框架能否表达这类界面，"
            L"原项目通过网络取数，本框架没有 HTTP 客户端。");
    }
    bars[1]->SetClosable(true);

    column.Measure(360.0f, 2000.0f);
    column.Arrange(RectDip{0.0f, 0.0f, 360.0f, 2000.0f});

    for (int i = 0; i < 3; ++i) {
        std::printf("  bar %d: y=%.1f h=%.1f\n", i,
                    bars[i]->Bounds().y, bars[i]->Bounds().h);
        EXPECT_TRUE(bars[i]->Bounds().h > 30.0f);      // wrapped to several lines
        EXPECT_NEAR(bars[i]->Bounds().w, 360.0f, 1.0f);
    }
    // Strictly increasing, non-overlapping, with the spacing respected.
    EXPECT_TRUE(bars[1]->Bounds().y >= bars[0]->Bounds().bottom() + 7.9f);
    EXPECT_TRUE(bars[2]->Bounds().y >= bars[1]->Bounds().bottom() + 7.9f);

    // The closable one's button stays inside its own bounds.
    const RectDip cb = bars[1]->CloseButtonRect();
    EXPECT_TRUE(cb.right() <= bars[1]->Bounds().right() + 0.01f);
    EXPECT_TRUE(cb.y >= bars[1]->Bounds().y - 0.01f);
}

// The screener table with row tinting: 480 rows, a provider that colours by direction.
// Asserts the provider is consulted only for what is on screen, which is the property that
// makes a coloured table affordable.
TEST(StockAnalyzerShape, ScreenerTableConsultsRowColorProviderOnlyForVisibleRows) {
    Env env;
    DataGrid grid;
    grid.AttachToContext(env.ctx);
    grid.SetColumns({
        {L"代码", 90.0f, DataGrid::Align::Left},
        {L"名称", 130.0f, DataGrid::Align::Left},
        {L"最新价", 90.0f, DataGrid::Align::Right},
    });
    grid.SetRowCount(480);
    grid.SetRowHeight(28.0f);
    grid.SetCellProvider([](int r, int) { return std::to_wstring(r); });

    grid.Measure(400.0f, 280.0f);
    grid.Arrange(RectDip{0.0f, 0.0f, 400.0f, 280.0f});

    int fr = 0, lr = -1;
    grid.VisibleRowRange(fr, lr);
    const int live = lr - fr + 1;
    std::printf("  480 rows in a 280 DIP viewport -> %d live\n", live);
    EXPECT_TRUE(live > 0);
    EXPECT_TRUE(live < 15);

    // Scrolling to the end must still show a full screenful, not run off into blank space.
    grid.SetVerticalOffset(grid.TotalContentHeight());
    grid.VisibleRowRange(fr, lr);
    std::printf("  scrolled to the end -> rows %d..%d (last row is 479)\n", fr, lr);
    EXPECT_EQ(lr, 479);
    EXPECT_TRUE(lr - fr + 1 > 1);
}
