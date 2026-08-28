// StockAnalyzerPage.cpp — a replica of the D:\project\Game A-share analysis tool's UI,
// built entirely from FluentUI controls.
//
// PURPOSE. This page answers a specific question: can this framework express a real
// data-analysis application's interface? The reference is a Streamlit app with six pages
// (个股分析 / 选股筛选 / 策略回测 / 基本面 / 内在价值 / 市场概览). Rather than describe the
// mapping in prose, this reproduces the parts that exercise every control the mapping
// needed, so a reviewer can see whether it actually looks and behaves like the original.
//
// ALL DATA HERE IS SYNTHETIC and generated deterministically in-process. This is a UI
// feasibility demo, not a port: the original pulls live quotes from akshare over the
// network, and this framework has no HTTP client. Nothing here should be read as market
// data, and the numbers are shaped only to look plausible (a random walk with a drift).
//
// WHAT IT DEMONSTRATES, control by control:
//   TabControl      the reference's five fundamental tabs
//   Chart           K-line (candlestick), equity curve (line), revenue bars
//   DataGrid        the screener result table and the raw-financials table
//   Metric          the KPI strip (price, PE, PB, market cap)
//   InfoBar         the reference's st.info / st.warning / st.error notices
//   ComboBox        st.selectbox
//   Slider          st.slider
//   NumericUpDown   st.number_input
//   CheckBox        st.checkbox
//   ProgressBar     st.progress
//   Expander        st.expander
//   GroupBox/Grid   st.columns and the card layout

#include "../GalleryMain.h"
#include "../../FluentUI/window/NativeWindowHost.h"

#include "../../FluentUI/controls/Chart.h"
#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/controls/TabControl.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/NumericUpDown.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/Separator.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/UniformGrid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/ScrollPanel.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fluent {

namespace {

// --- Synthetic data ---------------------------------------------------------
// Deterministic so the page looks identical on every run (a screenshot diff or a repeated
// manual check would otherwise show different numbers each time).

struct Bar { double open, high, low, close, volume; };

struct StockData {
    std::vector<Bar> bars;
    std::vector<std::wstring> dates;
    std::vector<double> ma20;
    std::vector<double> equity;
    std::vector<double> revenue;
    std::vector<std::wstring> revLabels;
};

// A tiny LCG rather than <random>: reproducible across platforms and standard library
// versions, which std::mt19937's distributions are not.
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    double Next() {              // [0,1)
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s >> 8) / 16777216.0;
    }
};

const StockData& Data() {
    static StockData d = [] {
        StockData out;
        Lcg rng(20260822u);
        double price = 42.0;
        for (int i = 0; i < 240; ++i) {
            // Random walk with mild mean reversion so it does not drift off the chart.
            const double drift = (45.0 - price) * 0.004;
            const double shock = (rng.Next() - 0.5) * 1.6;
            const double open = price;
            const double close = std::max(1.0, open + drift + shock);
            const double high = std::max(open, close) + rng.Next() * 0.6;
            const double low = std::min(open, close) - rng.Next() * 0.6;
            out.bars.push_back({open, high, low, close, 1e6 + rng.Next() * 4e6});
            price = close;

            wchar_t buf[16];
            std::swprintf(buf, 16, L"%02d-%02d", (i / 21) % 12 + 1, i % 21 + 1);
            out.dates.push_back(buf);
        }
        // 20-period moving average of the close.
        for (size_t i = 0; i < out.bars.size(); ++i) {
            const size_t n = i >= 19 ? 20 : i + 1;
            double sum = 0.0;
            for (size_t k = i + 1 - n; k <= i; ++k) sum += out.bars[k].close;
            out.ma20.push_back(sum / static_cast<double>(n));
        }
        // A backtest equity curve, starting at 1.0.
        double eq = 1.0;
        for (int i = 0; i < 240; ++i) {
            eq *= 1.0 + (rng.Next() - 0.47) * 0.02;
            out.equity.push_back(eq);
        }
        // Eight half-year revenue figures, in 亿元.
        for (int i = 0; i < 8; ++i) {
            out.revenue.push_back(38.0 + i * 5.5 + (rng.Next() - 0.5) * 8.0);
            wchar_t buf[16];
            std::swprintf(buf, 16, L"%dH%d", 2022 + i / 2, i % 2 + 1);
            out.revLabels.push_back(buf);
        }
        return out;
    }();
    return d;
}

// Screener rows: code, name, price, change%, PE, PB, market cap (亿).
struct ScreenRow {
    std::wstring code, name;
    double price, changePct, pe, pb, cap;
};

const std::vector<ScreenRow>& Screened() {
    static std::vector<ScreenRow> rows = [] {
        static const wchar_t* names[] = {
            L"贵州茅台", L"宁德时代", L"比亚迪", L"招商银行", L"中国平安",
            L"隆基绿能", L"美的集团", L"五粮液", L"东方财富", L"万科A",
            L"京东方A", L"三一重工", L"海康威视", L"伊利股份", L"长江电力",
        };
        std::vector<ScreenRow> out;
        Lcg rng(7u);
        for (int i = 0; i < 480; ++i) {          // deliberately more than one screenful
            ScreenRow r;
            wchar_t code[12];
            std::swprintf(code, 12, L"%06d", 600000 + i * 7);
            r.code = code;
            r.name = names[static_cast<size_t>(i) % (sizeof(names) / sizeof(names[0]))];
            if (i >= 15) r.name += L"-" + std::to_wstring(i / 15);
            r.price = 8.0 + rng.Next() * 220.0;
            r.changePct = (rng.Next() - 0.45) * 9.0;
            r.pe = 6.0 + rng.Next() * 60.0;
            r.pb = 0.6 + rng.Next() * 8.0;
            r.cap = 60.0 + rng.Next() * 20000.0;
            out.push_back(std::move(r));
        }
        return out;
    }();
    return rows;
}

std::wstring Fmt(double v, int decimals) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%.*f", decimals, v);
    return buf;
}

std::wstring FmtSigned(double v, int decimals) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%+.*f%%", decimals, v);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------

std::unique_ptr<ScrollPanel> GalleryApp::CreateStockAnalyzerPage() {
    auto [page, content] = CreatePageShell(L"A股分析工具（界面复刻）");

    // A standing disclaimer, and an honest statement that the data is fake. The reference
    // app carries the same warning; omitting it from a replica of a financial UI would be
    // the wrong thing to copy.
    {
        auto* bar = content->Add(std::make_unique<InfoBar>());
        bar->SetSeverity(InfoBar::Severity::Warning);
        bar->SetTitle(L"演示数据，非真实行情");
        bar->SetMessage(
            L"本页所有数字都是进程内生成的合成数据，用于验证 FluentUI 能否表达这类"
            L"数据分析界面。原项目通过 akshare 联网取数，本框架没有 HTTP 客户端，"
            L"因此不含任何真实行情。所有指标均不构成投资建议。");
        bar->SetMargin(Thickness{0, 0, 0, 16.0f});
    }

    // --- KPI strip (st.metric x st.columns) --------------------------------
    {
        auto* card = CreateExampleCard(content, L"个股概览（Metric + Grid Star 列 = st.columns）");
        // Grid with Star columns, not UniformGrid: UniformGrid sizes every cell to the
        // LARGEST CHILD'S DESIRED width and never expands to fill the row, so a four-up
        // strip in an 800 DIP card would give each tile ~63 DIP and leave the rest blank.
        // st.columns divides the AVAILABLE width evenly, which is what Star tracks do.
        // Auto row + no SetHeight: the row sizes to the tallest tile.
        //
        // The previous version typed SetHeight(58). That is a guess about a font's line
        // metrics, and it was wrong: a three-line Metric needs 62.6 DIP, so the last line
        // of 「市盈率 TTM」was sheared off. Arrange compresses a child to its slot (never
        // larger), so a slot smaller than the content silently truncates it — the framework
        // behaved correctly and the hand-typed number was the bug. Let layout compute it.
        auto* strip = card->Add(std::make_unique<Grid>());
        strip->AddRow(GridLength::Auto());
        for (int i = 0; i < 4; ++i) strip->AddColumn(GridLength::Star(1.0f));

        const StockData& d = Data();
        const double last = d.bars.back().close;
        const double prev = d.bars[d.bars.size() - 2].close;
        const double chg = (last - prev) / prev * 100.0;

        int stripCol = 0;
        auto addMetric = [&](const wchar_t* label, std::wstring value,
                             std::wstring delta, Metric::Trend trend, bool inverted) {
            auto* m = strip->Add(std::make_unique<Metric>());
            strip->SetCell(m, 0, stripCol++);
            m->SetLabel(label);
            m->SetValue(std::move(value));
            if (trend != Metric::Trend::None) m->SetDelta(std::move(delta), trend);
            m->SetInverted(inverted);
            return m;
        };

        addMetric(L"最新价", Fmt(last, 2), FmtSigned(chg, 2),
                  chg >= 0 ? Metric::Trend::Up : Metric::Trend::Down, false);
        addMetric(L"市盈率 TTM", Fmt(28.4, 2), L"行业中位 31.2", Metric::Trend::Flat, false);
        addMetric(L"市净率", Fmt(3.17, 2), L"分位 62%", Metric::Trend::Flat, false);
        // Inverted: a RISING debt ratio is bad news, so it must not be painted as a gain.
        // This is the case that motivated Metric::SetInverted.
        addMetric(L"资产负债率", L"41.8%", FmtSigned(1.9, 1), Metric::Trend::Up, true);
    }

    // --- K-line (st.plotly_chart with go.Candlestick) -----------------------
    {
        auto* card = CreateExampleCard(content, L"个股分析：K 线 + MA20（Chart::Candlestick）");
        auto* chart = card->Add(std::make_unique<Chart>());
        chart->SetHeight(260.0f);
        chart->SetKind(Chart::Kind::Candlestick);
        const StockData& d = Data();
        chart->SetPointCount(static_cast<int>(d.bars.size()));
        // Show the last 90 bars, the way the reference defaults to a recent window.
        chart->SetVisibleRange(static_cast<int>(d.bars.size()) - 90, 90);

        Chart::Series ohlc;
        ohlc.name = L"K线";
        ohlc.open  = [](int i) { return Data().bars[static_cast<size_t>(i)].open; };
        ohlc.high  = [](int i) { return Data().bars[static_cast<size_t>(i)].high; };
        ohlc.low   = [](int i) { return Data().bars[static_cast<size_t>(i)].low; };
        ohlc.close = [](int i) { return Data().bars[static_cast<size_t>(i)].close; };
        chart->AddSeries(std::move(ohlc));

        chart->SetXLabelProvider([](int i) {
            return Data().dates[static_cast<size_t>(i)];
        });

        auto* note = card->Add(std::make_unique<TextBlock>());
        note->SetText(L"红涨绿跌（CJK 习惯）。这个方向由主题的 dataPositive / dataNegative "
                      L"决定，欧美习惯相反，改主题即可，不用改控件。");
        note->SetDimmed(true);
        note->SetWrap(true);
    }

    // --- Fundamental tabs (st.tabs) ----------------------------------------
    {
        auto* card = CreateExampleCard(content, L"基本面：五个 tab（TabControl = st.tabs）");
        auto* tabs = card->Add(std::make_unique<TabControl>());
        tabs->SetHeight(300.0f);

        // 1. 体检报告 — scores as bars.
        {
            auto panel = std::make_unique<StackPanel>();
            panel->SetSpacing(8.0f);
            auto* info = panel->Add(std::make_unique<InfoBar>());
            info->SetSeverity(InfoBar::Severity::Success);
            info->SetMessage(L"综合评分 78 / 100，盈利与现金流为强项，估值处于中位。");
            auto* chart = panel->Add(std::make_unique<Chart>());
            chart->SetHeight(180.0f);
            chart->SetKind(Chart::Kind::Bar);
            chart->SetPointCount(5);
            Chart::Series s;
            s.name = L"五维评分";
            s.value = [](int i) {
                static const double v[5] = {86.0, 71.0, 64.0, 82.0, 58.0};
                return v[static_cast<size_t>(i)];
            };
            chart->AddSeries(std::move(s));
            chart->SetXLabelProvider([](int i) {
                static const wchar_t* n[5] = {L"盈利", L"成长", L"安全", L"现金流", L"估值"};
                return std::wstring(n[static_cast<size_t>(i)]);
            });
            tabs->AddTab(L"体检报告", std::move(panel));
        }

        // 2. 财务趋势 — revenue bars.
        {
            auto panel = std::make_unique<StackPanel>();
            panel->SetSpacing(8.0f);
            auto* chart = panel->Add(std::make_unique<Chart>());
            chart->SetHeight(200.0f);
            chart->SetKind(Chart::Kind::Bar);
            chart->SetPointCount(static_cast<int>(Data().revenue.size()));
            Chart::Series s;
            s.name = L"营业收入(亿元)";
            s.value = [](int i) { return Data().revenue[static_cast<size_t>(i)]; };
            chart->AddSeries(std::move(s));
            chart->SetXLabelProvider([](int i) {
                return Data().revLabels[static_cast<size_t>(i)];
            });
            tabs->AddTab(L"财务趋势", std::move(panel));
        }

        // 3. 估值分位 — a line chart of PE percentile.
        {
            auto panel = std::make_unique<StackPanel>();
            auto* chart = panel->Add(std::make_unique<Chart>());
            chart->SetHeight(210.0f);
            chart->SetKind(Chart::Kind::Line);
            chart->SetPointCount(static_cast<int>(Data().bars.size()));
            Chart::Series s;
            s.name = L"PE(TTM)";
            s.value = [](int i) {
                return 22.0 + Data().bars[static_cast<size_t>(i)].close * 0.22;
            };
            chart->AddSeries(std::move(s));
            tabs->AddTab(L"估值分位", std::move(panel));
        }

        // 4. 同行对比 — a small grid.
        {
            auto panel = std::make_unique<StackPanel>();
            auto* grid = panel->Add(std::make_unique<DataGrid>());
            grid->SetHeight(220.0f);
            grid->SetColumns({
                {L"公司", 120.0f, DataGrid::Align::Left},
                {L"PE", 70.0f, DataGrid::Align::Right},
                {L"PB", 70.0f, DataGrid::Align::Right},
                {L"ROE%", 80.0f, DataGrid::Align::Right},
            });
            grid->SetRowCount(6);
            grid->SetAlternatingRowFill(true);
            grid->SetCellProvider([](int r, int c) -> std::wstring {
                static const wchar_t* peers[6] = {L"本公司", L"同行A", L"同行B",
                                                  L"同行C", L"行业中位", L"行业均值"};
                static const double pe[6] = {28.4, 31.0, 24.6, 40.2, 31.2, 33.8};
                static const double pb[6] = {3.17, 3.8, 2.4, 5.1, 3.4, 3.9};
                static const double roe[6] = {14.2, 11.8, 12.9, 9.4, 12.1, 11.5};
                switch (c) {
                    case 0: return peers[static_cast<size_t>(r)];
                    case 1: return Fmt(pe[static_cast<size_t>(r)], 2);
                    case 2: return Fmt(pb[static_cast<size_t>(r)], 2);
                    default: return Fmt(roe[static_cast<size_t>(r)], 1);
                }
            });
            tabs->AddTab(L"同行对比", std::move(panel));
        }

        // 5. 原始数据 — the wide financial table, which is what column virtualization is
        // for: the reference shows 80 columns of periods.
        {
            auto panel = std::make_unique<StackPanel>();
            auto* grid = panel->Add(std::make_unique<DataGrid>());
            grid->SetHeight(220.0f);
            std::vector<DataGrid::Column> cols;
            cols.push_back({L"科目", 140.0f, DataGrid::Align::Left});
            for (int p = 0; p < 40; ++p) {
                wchar_t h[16];
                std::swprintf(h, 16, L"%dQ%d", 2016 + p / 4, p % 4 + 1);
                cols.push_back({h, 84.0f, DataGrid::Align::Right});
            }
            grid->SetColumns(std::move(cols));
            grid->SetRowCount(60);
            grid->SetCellProvider([](int r, int c) -> std::wstring {
                static const wchar_t* items[8] = {
                    L"营业收入", L"营业成本", L"毛利润", L"净利润",
                    L"经营现金流", L"总资产", L"总负债", L"股东权益"};
                if (c == 0) {
                    std::wstring base = items[static_cast<size_t>(r) % 8];
                    return base + L"-" + std::to_wstring(r / 8 + 1);
                }
                return Fmt(120.0 + (r * 37 + c * 13) % 800 + (c * 0.37), 2);
            });
            tabs->AddTab(L"原始数据", std::move(panel));
        }
    }

    // --- Screener (st.dataframe over a filtered universe) ------------------
    {
        auto* card = CreateExampleCard(content,
            L"选股筛选：480 行结果表（DataGrid，行/列双向虚拟化）");

        // Filter controls, the reference's sidebar inputs.
        // Grid with Star columns, not UniformGrid: UniformGrid sizes every cell to the
        // LARGEST CHILD'S DESIRED width and never expands to fill the row, so a four-up
        // strip in an 800 DIP card would give each tile ~63 DIP and leave the rest blank.
        // st.columns divides the AVAILABLE width evenly, which is what Star tracks do.
        // Auto row: control heights come from spacing tokens, not from a typed number.
        auto* controls = card->Add(std::make_unique<Grid>());
        controls->AddRow(GridLength::Auto());
        for (int i = 0; i < 4; ++i) controls->AddColumn(GridLength::Star(1.0f));

        auto* preset = controls->Add(std::make_unique<ComboBox>());
        controls->SetCell(preset, 0, 0);
        preset->SetItems({L"预设：低估值蓝筹", L"预设：均线多头", L"预设：超卖反弹"});
        preset->SetSelectedIndex(0);

        auto* maxPe = controls->Add(std::make_unique<NumericUpDown>());
        controls->SetCell(maxPe, 0, 1);
        maxPe->SetValue(30.0);
        maxPe->SetMin(0.0);
        maxPe->SetMax(200.0);

        auto* minCap = controls->Add(std::make_unique<Slider>());
        controls->SetCell(minCap, 0, 2);
        minCap->SetMin(0.0f);
        minCap->SetMax(5000.0f);
        minCap->SetValue(500.0f);

        auto* onlyProfit = controls->Add(std::make_unique<CheckBox>());
        controls->SetCell(onlyProfit, 0, 3);
        onlyProfit->SetText(L"仅盈利公司");
        onlyProfit->SetChecked(true);

        // Scan progress, the reference's st.progress during pattern matching.
        auto* prog = card->Add(std::make_unique<ProgressBar>());
        prog->SetValue(0.68f);

        auto* grid = card->Add(std::make_unique<DataGrid>());
        grid->SetHeight(280.0f);
        grid->SetColumns({
            {L"代码", 90.0f, DataGrid::Align::Left},
            {L"名称", 130.0f, DataGrid::Align::Left},
            {L"最新价", 90.0f, DataGrid::Align::Right},
            {L"涨跌幅", 90.0f, DataGrid::Align::Right},
            {L"市盈率", 90.0f, DataGrid::Align::Right},
            {L"市净率", 80.0f, DataGrid::Align::Right},
            {L"市值(亿)", 110.0f, DataGrid::Align::Right},
        });
        grid->SetRowCount(static_cast<int>(Screened().size()));
        grid->SetAlternatingRowFill(true);
        grid->SetCellProvider([](int r, int c) -> std::wstring {
            const ScreenRow& row = Screened()[static_cast<size_t>(r)];
            switch (c) {
                case 0: return row.code;
                case 1: return row.name;
                case 2: return Fmt(row.price, 2);
                case 3: return FmtSigned(row.changePct, 2);
                case 4: return Fmt(row.pe, 1);
                case 5: return Fmt(row.pb, 2);
                default: return Fmt(row.cap, 0);
            }
        });
        // Row tinting by direction — the case RowColorProvider exists for.
        // The snapshot is read through the owning WINDOW rather than captured by value.
        // UIElement::Theme() is protected, so a page cannot reach it; and capturing the
        // ColorTokens by value would freeze the colours at build time, so the rows would
        // keep their light-mode tint after a switch to dark. NativeWindowHost::Theme()
        // returns a reference whose pointee is overwritten in place on a theme change,
        // which is exactly the indirection needed here.
        NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
        grid->SetRowColorProvider([owner](int r, D2D1_COLOR_F& out) {
            if (!owner) return false;      // fall back to textPrimary
            const double chg = Screened()[static_cast<size_t>(r)].changePct;
            const ColorTokens& c = owner->Theme().colors;
            out = chg >= 0.0 ? c.dataPositive : c.dataNegative;
            return true;
        });

        auto* note = card->Add(std::make_unique<TextBlock>());
        note->SetText(L"480 行 x 7 列，但每帧只有可见的约 10 行被取值和绘制。"
                      L"原始数据 tab 的 41 列表格同时验证了列方向的虚拟化。");
        note->SetDimmed(true);
        note->SetWrap(true);
    }

    // --- Backtest (equity curve + stats) -----------------------------------
    {
        auto* card = CreateExampleCard(content, L"策略回测：净值曲线（Chart::Line）");
        auto* chart = card->Add(std::make_unique<Chart>());
        chart->SetHeight(200.0f);
        chart->SetKind(Chart::Kind::Line);
        chart->SetPointCount(static_cast<int>(Data().equity.size()));
        Chart::Series s;
        s.name = L"策略净值";
        s.value = [](int i) { return Data().equity[static_cast<size_t>(i)]; };
        chart->AddSeries(std::move(s));
        chart->SetXLabelProvider([](int i) { return Data().dates[static_cast<size_t>(i)]; });

        // Grid with Star columns, not UniformGrid: UniformGrid sizes every cell to the
        // LARGEST CHILD'S DESIRED width and never expands to fill the row, so a four-up
        // strip in an 800 DIP card would give each tile ~63 DIP and leave the rest blank.
        // st.columns divides the AVAILABLE width evenly, which is what Star tracks do.
        // Auto row, same reason as the KPI strip above.
        auto* stats = card->Add(std::make_unique<Grid>());
        stats->AddRow(GridLength::Auto());
        for (int i = 0; i < 4; ++i) stats->AddColumn(GridLength::Star(1.0f));
        const double finalEq = Data().equity.back();
        int statCol = 0;
        auto addStat = [&](const wchar_t* label, std::wstring value,
                           Metric::Trend trend, bool inverted) {
            auto* m = stats->Add(std::make_unique<Metric>());
            stats->SetCell(m, 0, statCol++);
            m->SetLabel(label);
            m->SetValue(std::move(value));
            m->SetInverted(inverted);
            if (trend != Metric::Trend::None) m->SetDelta(L"", trend);
            return m;
        };
        addStat(L"累计收益", FmtSigned((finalEq - 1.0) * 100.0, 2),
                Metric::Trend::None, false);
        addStat(L"最大回撤", L"-18.4%", Metric::Trend::None, true);
        addStat(L"夏普比率", Fmt(1.23, 2), Metric::Trend::None, false);
        addStat(L"胜率", L"54.2%", Metric::Trend::None, false);
    }

    // --- Valuation (assumptions in an Expander, like the reference) --------
    {
        auto* card = CreateExampleCard(content, L"内在价值：DCF 假设（Expander = st.expander）");

        auto* exp = card->Add(std::make_unique<Expander>());
        exp->SetHeader(L"模型假设（可调）");
        // Animated, since this is the disclosure the animation was added for.
        exp->SetUserToggleTransition(Expander::Transition::Animate);
        auto inner = std::make_unique<StackPanel>();
        inner->SetSpacing(8.0f);
        auto* growth = inner->Add(std::make_unique<Slider>());
        growth->SetMin(0.0f);
        growth->SetMax(30.0f);
        growth->SetValue(12.0f);
        auto* wacc = inner->Add(std::make_unique<NumericUpDown>());
        wacc->SetValue(9.5);
        auto* perp = inner->Add(std::make_unique<NumericUpDown>());
        perp->SetValue(2.5);
        exp->SetContent(std::move(inner));

        auto* err = card->Add(std::make_unique<InfoBar>());
        err->SetSeverity(InfoBar::Severity::Error);
        err->SetTitle(L"自由现金流为负，拒绝估值");
        err->SetMessage(L"最近四个季度自由现金流为负，两阶段 DCF 无法给出有意义的结果。"
                        L"原项目在这种情况下明确拒绝输出而不是硬算 —— 复刻保留了这个行为。");
        err->SetClosable(true);
    }

    // --- Market overview ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"市场概览：指数与涨跌分布");
        auto* row = card->Add(std::make_unique<Grid>());
        row->SetHeight(200.0f);
        row->AddColumn(GridLength::Star(1.0f));
        row->AddColumn(GridLength::Star(1.0f));
        row->AddRow(GridLength::Star(1.0f));

        auto* left = row->Add(std::make_unique<Chart>());
        left->SetKind(Chart::Kind::Line);
        left->SetPointCount(static_cast<int>(Data().bars.size()));
        Chart::Series idx;
        idx.name = L"上证指数";
        idx.value = [](int i) { return 3000.0 + Data().bars[static_cast<size_t>(i)].close * 12.0; };
        left->AddSeries(std::move(idx));
        row->SetCell(left, 0, 0);

        auto* right = row->Add(std::make_unique<Chart>());
        right->SetKind(Chart::Kind::Bar);
        right->SetPointCount(9);
        Chart::Series dist;
        dist.name = L"涨跌分布";
        dist.value = [](int i) {
            static const double v[9] = {-180, -320, -520, -410, 120, 480, 610, 330, 150};
            return v[static_cast<size_t>(i)];
        };
        right->AddSeries(std::move(dist));
        right->SetXLabelProvider([](int i) {
            static const wchar_t* n[9] = {L"<-7", L"-7~-5", L"-5~-3", L"-3~0", L"平",
                                          L"0~3", L"3~5", L"5~7", L">7"};
            return std::wstring(n[static_cast<size_t>(i)]);
        });
        row->SetCell(right, 0, 1);
    }

    // --- What could not be reproduced -------------------------------------
    {
        auto* card = CreateExampleCard(content, L"未能复刻的部分（诚实清单）");
        auto* bar = card->Add(std::make_unique<InfoBar>());
        bar->SetSeverity(InfoBar::Severity::Informational);
        bar->SetTitle(L"这些原项目有、本框架目前没有");
        bar->SetMessage(
            L"1) 联网取数：没有 HTTP 客户端，本页数据全为合成。"
            L"2) 雷达图与热力图：Chart 只支持折线/柱/K线三种，五维评分改用柱状图，"
            L"敏感性热图与 PB-ROE 散点无法表达。"
            L"3) 瀑布图：DCF 价值构成图缺失。"
            L"4) 多选控件：st.multiselect 没有对应控件，示例用 ComboBox 单选替代。"
            L"5) Markdown 渲染：TextBlock 是纯文本，原项目的 st.markdown 富文本降级为纯文本。"
            L"6) CSV 导出：没有文件保存对话框。");
        bar->SetMargin(Thickness{0, 0, 0, 8.0f});
    }

    return std::move(page);
}

}  // namespace fluent
