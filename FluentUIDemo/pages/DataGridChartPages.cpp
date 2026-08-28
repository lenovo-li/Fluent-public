// DataGridChartPages.cpp — standalone showcase pages for DataGrid and Chart.
//
// Same three-part shape as the InfoBar/Metric pages (states, behaviour, in context), with
// one addition specific to these two controls: a VISIBLE demonstration of virtualization.
//
// Virtualization is the reason both controls exist, and it is invisible by construction --
// a grid that draws 100,000 rows looks exactly like one that draws 20 and clips the rest.
// So each page prints its live visible range on screen. That turns an invisible property
// into something a reviewer can watch change while scrolling, and it is the same number
// the headless tests assert.

#include "../GalleryMain.h"
#include "../../FluentUI/window/NativeWindowHost.h"

#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/Chart.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/Separator.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/GroupBox.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fluent {

namespace {

// --- Deterministic sample data ---------------------------------------------
// A fixed-seed LCG rather than <random>: reproducible across standard library versions,
// so the page shows the same numbers on every run and a screenshot stays comparable.
struct Lcg {
    uint32_t s;
    explicit Lcg(uint32_t seed) : s(seed) {}
    double Next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s >> 8) / 16777216.0;
    }
};

struct Row {
    std::wstring code, name, sector;
    double price, changePct, volume, pe;
};

// 50,000 rows. Deliberately large: the point of the page is that the row COUNT does not
// affect the frame cost, and a number a reviewer can see is more convincing than a claim.
const std::vector<Row>& BigTable() {
    static std::vector<Row> rows = [] {
        static const wchar_t* names[] = {
            L"贵州茅台", L"宁德时代", L"比亚迪", L"招商银行", L"中国平安",
            L"隆基绿能", L"美的集团", L"五粮液", L"东方财富", L"万科A"};
        static const wchar_t* sectors[] = {
            L"食品饮料", L"电池", L"汽车", L"银行", L"保险",
            L"光伏", L"家电", L"食品饮料", L"证券", L"房地产"};
        std::vector<Row> out;
        out.reserve(50000);
        Lcg rng(20260823u);
        for (int i = 0; i < 50000; ++i) {
            Row r;
            wchar_t code[12];
            std::swprintf(code, 12, L"%06d", 600000 + i);
            r.code = code;
            const size_t k = static_cast<size_t>(i) % 10;
            r.name = std::wstring(names[k]) + L"-" + std::to_wstring(i / 10 + 1);
            r.sector = sectors[k];
            r.price = 5.0 + rng.Next() * 300.0;
            r.changePct = (rng.Next() - 0.45) * 10.0;
            r.volume = 1e5 + rng.Next() * 9e6;
            r.pe = 5.0 + rng.Next() * 80.0;
            out.push_back(std::move(r));
        }
        return out;
    }();
    return rows;
}

struct Bar { double open, high, low, close; };

const std::vector<Bar>& Bars() {
    static std::vector<Bar> bars = [] {
        std::vector<Bar> out;
        Lcg rng(4242u);
        double price = 40.0;
        for (int i = 0; i < 500; ++i) {
            const double drift = (42.0 - price) * 0.004;
            const double open = price;
            const double close = std::max(1.0, open + drift + (rng.Next() - 0.5) * 1.8);
            out.push_back({open,
                           std::max(open, close) + rng.Next() * 0.7,
                           std::min(open, close) - rng.Next() * 0.7,
                           close});
            price = close;
        }
        return out;
    }();
    return bars;
}

std::wstring Fmt(double v, int dec) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%.*f", dec, v);
    return buf;
}

std::wstring FmtSigned(double v) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%+.2f%%", v);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// DataGrid
// ---------------------------------------------------------------------------

std::unique_ptr<ScrollPanel> GalleryApp::CreateDataGridPage() {
    auto [page, content] = CreatePageShell(L"DataGrid");

    // --- 1. Virtualization, made visible ---------------------------------
    {
        auto* card = CreateExampleCard(content, L"5 万行表格 —— 虚拟化实况");
        auto* intro = card->Add(std::make_unique<TextBlock>());
        intro->SetText(L"下面这张表有 50,000 行 × 7 列 = 35 万个单元格，但每帧只有可见的"
                       L"十几行被取值和绘制。滚动时看下面那行数字：可见范围会移动，"
                       L"但数量不变 —— 这正是 headless 测试断言的同一个数。");
        intro->SetWrap(true);
        intro->SetDimmed(true);

        auto* grid = card->Add(std::make_unique<DataGrid>());
        // Explicit height is CORRECT here: a virtualized table has no natural height (its
        // content is 50000 * 28 = 1.4 million DIP), so the caller must pick a viewport.
        // Contrast the Metric strip, where typing a height clipped the text.
        grid->SetHeight(320.0f);
        grid->SetColumns({
            {L"代码",    90.0f, DataGrid::Align::Left},
            {L"名称",   150.0f, DataGrid::Align::Left},
            {L"行业",   110.0f, DataGrid::Align::Left},
            {L"最新价",  90.0f, DataGrid::Align::Right},
            {L"涨跌幅",  90.0f, DataGrid::Align::Right},
            {L"成交量", 120.0f, DataGrid::Align::Right},
            {L"市盈率",  90.0f, DataGrid::Align::Right},
        });
        grid->SetRowCount(static_cast<int>(BigTable().size()));
        grid->SetAlternatingRowFill(true);
        grid->SetCellProvider([](int r, int c) -> std::wstring {
            const Row& row = BigTable()[static_cast<size_t>(r)];
            switch (c) {
                case 0: return row.code;
                case 1: return row.name;
                case 2: return row.sector;
                case 3: return Fmt(row.price, 2);
                case 4: return FmtSigned(row.changePct);
                case 5: return Fmt(row.volume, 0);
                default: return Fmt(row.pe, 1);
            }
        });

        auto* status = card->Add(std::make_unique<TextBlock>());
        status->SetDimmed(true);
        status->SetText(L"（滚动或点击以更新）");
        gridDemo_ = grid;
        gridStatus_ = status;

        auto refresh = [](GalleryApp* self) {
            if (!self->gridDemo_ || !self->gridStatus_) return;
            int fr = 0, lr = -1, fc = 0, lc = -1;
            self->gridDemo_->VisibleRowRange(fr, lr);
            self->gridDemo_->VisibleColumnRange(fc, lc);
            const int rows = lr - fr + 1;
            const int cols = lc - fc + 1;
            wchar_t buf[256];
            std::swprintf(buf, 256,
                L"可见行 %d..%d（%d 行）× 可见列 %d..%d（%d 列）= 每帧 %d 个单元格；"
                L"总计 %d 行。选中第 %d 行。",
                fr, lr, rows, fc, lc, cols, rows * cols,
                self->gridDemo_->RowCount(), self->gridDemo_->SelectedRow());
            self->gridStatus_->SetText(buf);
        };
        // Selection change is the only event that fires on scroll-by-keyboard as well as
        // on click, so it is enough to keep the readout live for this demo.
        gridSubs_ += grid->SelectionChanged().Subscribe(this,
            [](void* o, DataGrid&, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->gridDemo_ || !self->gridStatus_) return;
                int fr = 0, lr = -1, fc = 0, lc = -1;
                self->gridDemo_->VisibleRowRange(fr, lr);
                self->gridDemo_->VisibleColumnRange(fc, lc);
                wchar_t buf[256];
                std::swprintf(buf, 256,
                    L"可见行 %d..%d（%d 行）× 可见列 %d..%d（%d 列）= 每帧 %d 个单元格；"
                    L"总计 %d 行。选中第 %d 行。",
                    fr, lr, lr - fr + 1, fc, lc, lc - fc + 1,
                    (lr - fr + 1) * (lc - fc + 1),
                    self->gridDemo_->RowCount(), self->gridDemo_->SelectedRow());
                self->gridStatus_->SetText(buf);
            });
        (void)refresh;

        auto* jump = card->Add(std::make_unique<Grid>());
        jump->AddRow(GridLength::Auto());
        for (int i = 0; i < 4; ++i) jump->AddColumn(GridLength::Star(1.0f));
        // One captureless handler per button. Event::Subscribe deliberately refuses
        // capturing lambdas (that would need a heap allocation and break its
        // zero-allocation property), so the target row cannot be captured -- it is
        // baked into a distinct function per button via a template parameter instead.
        struct Jump { const wchar_t* label; int row; };
        static constexpr Jump kJumps[] = {
            {L"跳到第 1 行", 0}, {L"第 1 万行", 9999},
            {L"第 4 万行", 39999}, {L"最后一行", 49999}};
        auto addJump = [&](int slot, auto handler) {
            auto* b = jump->Add(std::make_unique<Button>());
            jump->SetCell(b, 0, slot);
            b->SetText(kJumps[slot].label);
            b->SetMargin(Thickness{0, 0, 4.0f, 0});
            gridSubs_ += b->Click().Subscribe(this, handler);
        };
        addJump(0, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[0].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[0].row); }
        });
        addJump(1, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[1].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[1].row); }
        });
        addJump(2, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[2].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[2].row); }
        });
        addJump(3, [](void* o, Button&, RoutedEventArgs&) {
            auto* s = static_cast<GalleryApp*>(o);
            if (s->gridDemo_) { s->gridDemo_->SetSelectedRow(kJumps[3].row);
                                s->gridDemo_->ScrollRowIntoView(kJumps[3].row); }
        });
    }

    // --- 2. Options ------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"外观开关");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"斑马纹默认关闭：它帮助宽表、干扰窄表，所以由调用者决定。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* mini = card->Add(std::make_unique<DataGrid>());
        mini->SetHeight(180.0f);
        mini->SetColumns({
            {L"科目",   140.0f, DataGrid::Align::Left},
            {L"本期",   100.0f, DataGrid::Align::Right},
            {L"上期",   100.0f, DataGrid::Align::Right},
            {L"同比",   100.0f, DataGrid::Align::Right},
        });
        mini->SetRowCount(8);
        mini->SetCellProvider([](int r, int c) -> std::wstring {
            static const wchar_t* items[8] = {
                L"营业收入", L"营业成本", L"毛利润", L"净利润",
                L"经营现金流", L"总资产", L"总负债", L"股东权益"};
            static const double cur[8] = {68.4, 41.2, 27.2, 9.1, 11.3, 210.5, 88.0, 122.5};
            static const double prv[8] = {60.9, 37.8, 23.1, 9.6, 8.7, 198.2, 84.1, 114.1};
            switch (c) {
                case 0: return items[static_cast<size_t>(r)];
                case 1: return Fmt(cur[static_cast<size_t>(r)], 1);
                case 2: return Fmt(prv[static_cast<size_t>(r)], 1);
                default: {
                    const double a = cur[static_cast<size_t>(r)];
                    const double b = prv[static_cast<size_t>(r)];
                    return FmtSigned((a - b) / b * 100.0);
                }
            }
        });

        auto* opts = card->Add(std::make_unique<Grid>());
        opts->AddRow(GridLength::Auto());
        opts->AddColumn(GridLength::Star(1.0f));
        opts->AddColumn(GridLength::Star(1.0f));

        auto* zebra = opts->Add(std::make_unique<CheckBox>());
        opts->SetCell(zebra, 0, 0);
        zebra->SetText(L"斑马纹 AlternatingRowFill");
        gridSubs_ += zebra->Checked().Subscribe(mini,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetAlternatingRowFill(cb.IsChecked());
            });

        auto* lines = opts->Add(std::make_unique<CheckBox>());
        opts->SetCell(lines, 0, 1);
        lines->SetText(L"网格线 ShowGridLines");
        lines->SetChecked(true);
        gridSubs_ += lines->Checked().Subscribe(mini,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<DataGrid*>(o)->SetShowGridLines(cb.IsChecked());
            });
    }

    // --- 3. In context: row tinting + metrics -----------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：涨跌着色 + 指标条");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"RowColorProvider 按方向给整行着色。颜色取自主题的 "
                      L"dataPositive / dataNegative，所以切主题会跟着变，"
                      L"改成欧美习惯也只需要改主题。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* strip = card->Add(std::make_unique<Grid>());
        strip->AddRow(GridLength::Auto());
        for (int i = 0; i < 3; ++i) strip->AddColumn(GridLength::Star(1.0f));
        struct S { const wchar_t* l; const wchar_t* v; const wchar_t* d; Metric::Trend t; };
        const S stats[] = {
            {L"上涨", L"2,317", L"占 46.3%", Metric::Trend::Up},
            {L"下跌", L"2,684", L"占 53.7%", Metric::Trend::Down},
            {L"平盘", L"148",   L"占 3.0%",  Metric::Trend::Flat},
        };
        for (int i = 0; i < 3; ++i) {
            auto* m = strip->Add(std::make_unique<Metric>());
            strip->SetCell(m, 0, i);
            m->SetLabel(stats[i].l);
            m->SetValue(stats[i].v);
            m->SetDelta(stats[i].d, stats[i].t);
        }

        auto* tinted = card->Add(std::make_unique<DataGrid>());
        tinted->SetHeight(220.0f);
        tinted->SetColumns({
            {L"代码",   90.0f, DataGrid::Align::Left},
            {L"名称",  150.0f, DataGrid::Align::Left},
            {L"最新价", 90.0f, DataGrid::Align::Right},
            {L"涨跌幅", 90.0f, DataGrid::Align::Right},
        });
        tinted->SetRowCount(200);
        tinted->SetCellProvider([](int r, int c) -> std::wstring {
            const Row& row = BigTable()[static_cast<size_t>(r)];
            switch (c) {
                case 0: return row.code;
                case 1: return row.name;
                case 2: return Fmt(row.price, 2);
                default: return FmtSigned(row.changePct);
            }
        });
        NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
        tinted->SetRowColorProvider([owner](int r, D2D1_COLOR_F& out) {
            if (!owner) return false;
            const double chg = BigTable()[static_cast<size_t>(r)].changePct;
            const ColorTokens& c = owner->Theme().colors;
            out = chg >= 0.0 ? c.dataPositive : c.dataNegative;
            return true;
        });
    }

    CreateCodeExample(content, LR"(auto* grid = panel->Add(std::make_unique<DataGrid>());
grid->SetHeight(320.0f);          // 视口高度必须由调用者定：表格没有自然高度
grid->SetColumns({
    {L"代码",   90.0f, DataGrid::Align::Left},
    {L"最新价", 90.0f, DataGrid::Align::Right},   // 数字列右对齐才能比较位数
});
grid->SetRowCount(50000);         // 只是行数，不是数据

// 拉取式：只有可见单元格会被调用
grid->SetCellProvider([](int row, int col) -> std::wstring {
    return Format(MyData()[row], col);
});

// 整行着色（可选）
grid->SetRowColorProvider([win](int row, D2D1_COLOR_F& out) {
    out = Data()[row].up ? win->Theme().colors.dataPositive
                         : win->Theme().colors.dataNegative;
    return true;
});)");

    return std::move(page);
}

// ---------------------------------------------------------------------------
// Chart
// ---------------------------------------------------------------------------

std::unique_ptr<ScrollPanel> GalleryApp::CreateChartPage() {
    auto [page, content] = CreatePageShell(L"Chart");

    // --- 1. Three kinds --------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"三种 Kind");
        auto* intro = card->Add(std::make_unique<TextBlock>());
        intro->SetText(L"折线、柱状、K 线共用一个控件，因为难的部分是共享的："
                       L"数据到像素的映射、刻度取整、裁剪。拆成三个控件就是三份 scale "
                       L"代码，以及其中某一份的 scale bug。");
        intro->SetWrap(true);
        intro->SetDimmed(true);

        auto* line = card->Add(std::make_unique<Chart>());
        line->SetHeight(170.0f);
        line->SetKind(Chart::Kind::Line);
        line->SetPointCount(static_cast<int>(Bars().size()));
        line->SetVisibleRange(static_cast<int>(Bars().size()) - 120, 120);
        {
            Chart::Series s;
            s.name = L"收盘价";
            s.value = [](int i) { return Bars()[static_cast<size_t>(i)].close; };
            line->AddSeries(std::move(s));
        }

        auto* candle = card->Add(std::make_unique<Chart>());
        candle->SetHeight(200.0f);
        candle->SetKind(Chart::Kind::Candlestick);
        candle->SetPointCount(static_cast<int>(Bars().size()));
        candle->SetVisibleRange(static_cast<int>(Bars().size()) - 80, 80);
        {
            Chart::Series s;
            s.name = L"K线";
            s.open  = [](int i) { return Bars()[static_cast<size_t>(i)].open; };
            s.high  = [](int i) { return Bars()[static_cast<size_t>(i)].high; };
            s.low   = [](int i) { return Bars()[static_cast<size_t>(i)].low; };
            s.close = [](int i) { return Bars()[static_cast<size_t>(i)].close; };
            candle->AddSeries(std::move(s));
        }

        auto* bar = card->Add(std::make_unique<Chart>());
        bar->SetHeight(170.0f);
        bar->SetKind(Chart::Kind::Bar);
        bar->SetPointCount(12);
        {
            Chart::Series s;
            s.name = L"月度盈亏";
            s.value = [](int i) {
                static const double v[12] = {12.4, -5.1, 8.8, 3.2, -11.7, 6.5,
                                             14.2, -2.8, 9.1, -7.4, 4.6, 10.3};
                return v[static_cast<size_t>(i)];
            };
            bar->AddSeries(std::move(s));
        }
        bar->SetXLabelProvider([](int i) {
            return std::to_wstring(i + 1) + L"月";
        });
        auto* barNote = card->Add(std::make_unique<TextBlock>());
        barNote->SetText(L"柱状图自动把 0 纳入范围，否则 98~100 的三根柱子看起来"
                         L"一样高。负值默认用 dataNegative 着色。");
        barNote->SetWrap(true);
        barNote->SetDimmed(true);
    }

    // --- 2. Window virtualization, made visible --------------------------
    {
        auto* card = CreateExampleCard(content, L"可见窗口（拖动滑块滚动 500 点序列）");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"只有落在绘图区内的点会被绘制。自动缩放跟随窗口 —— "
                      L"滚到不同区段，Y 轴范围会重算，而不是被远处的极值压平。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* chart = card->Add(std::make_unique<Chart>());
        chart->SetHeight(200.0f);
        chart->SetKind(Chart::Kind::Candlestick);
        chart->SetPointCount(static_cast<int>(Bars().size()));
        chart->SetVisibleRange(0, 60);
        {
            Chart::Series s;
            s.open  = [](int i) { return Bars()[static_cast<size_t>(i)].open; };
            s.high  = [](int i) { return Bars()[static_cast<size_t>(i)].high; };
            s.low   = [](int i) { return Bars()[static_cast<size_t>(i)].low; };
            s.close = [](int i) { return Bars()[static_cast<size_t>(i)].close; };
            chart->AddSeries(std::move(s));
        }

        auto* status = card->Add(std::make_unique<TextBlock>());
        status->SetDimmed(true);
        chartDemo_ = chart;
        chartStatus_ = status;

        auto updateStatus = [](GalleryApp* self) {
            if (!self->chartDemo_ || !self->chartStatus_) return;
            wchar_t buf[220];
            std::swprintf(buf, 220,
                L"可见 %d..%d（%d 点，共 %d 点）；Y 轴自动范围 [%.2f, %.2f]",
                self->chartDemo_->VisibleFirst(),
                self->chartDemo_->VisibleFirst() + self->chartDemo_->VisibleCount() - 1,
                self->chartDemo_->VisibleCount(), self->chartDemo_->PointCount(),
                self->chartDemo_->MinY(), self->chartDemo_->MaxY());
            self->chartStatus_->SetText(buf);
        };
        updateStatus(this);

        auto* scroll = card->Add(std::make_unique<Slider>());
        scroll->SetMin(0.0f);
        scroll->SetMax(static_cast<float>(Bars().size() - 60));
        scroll->SetValue(0.0f);
        chartSubs_ += scroll->ValueChanged().Subscribe(this,
            [](void* o, Slider&, float& v) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->chartDemo_) return;
                self->chartDemo_->SetVisibleRange(static_cast<int>(v),
                                                  self->chartDemo_->VisibleCount());
                if (self->chartStatus_) {
                    wchar_t buf[220];
                    std::swprintf(buf, 220,
                        L"可见 %d..%d（%d 点，共 %d 点）；Y 轴自动范围 [%.2f, %.2f]",
                        self->chartDemo_->VisibleFirst(),
                        self->chartDemo_->VisibleFirst() +
                            self->chartDemo_->VisibleCount() - 1,
                        self->chartDemo_->VisibleCount(),
                        self->chartDemo_->PointCount(),
                        self->chartDemo_->MinY(), self->chartDemo_->MaxY());
                    self->chartStatus_->SetText(buf);
                }
            });

        auto* zoom = card->Add(std::make_unique<Slider>());
        zoom->SetMin(20.0f);
        zoom->SetMax(300.0f);
        zoom->SetValue(60.0f);
        chartSubs_ += zoom->ValueChanged().Subscribe(this,
            [](void* o, Slider&, float& v) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->chartDemo_) return;
                self->chartDemo_->SetVisibleRange(self->chartDemo_->VisibleFirst(),
                                                  static_cast<int>(v));
                if (self->chartStatus_) {
                    wchar_t buf[220];
                    std::swprintf(buf, 220,
                        L"可见 %d..%d（%d 点，共 %d 点）；Y 轴自动范围 [%.2f, %.2f]",
                        self->chartDemo_->VisibleFirst(),
                        self->chartDemo_->VisibleFirst() +
                            self->chartDemo_->VisibleCount() - 1,
                        self->chartDemo_->VisibleCount(),
                        self->chartDemo_->PointCount(),
                        self->chartDemo_->MinY(), self->chartDemo_->MaxY());
                    self->chartStatus_->SetText(buf);
                }
            });
    }

    // --- 3. Axes and multi-series ----------------------------------------
    {
        auto* card = CreateExampleCard(content, L"坐标轴开关 / 多序列");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"关掉坐标轴会把预留的边距还给绘图区 —— sparkline 需要这个，"
                      L"否则三分之一的宽度浪费在看不见的轴槽上。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* multi = card->Add(std::make_unique<Chart>());
        multi->SetHeight(190.0f);
        multi->SetKind(Chart::Kind::Line);
        multi->SetPointCount(static_cast<int>(Bars().size()));
        multi->SetVisibleRange(200, 200);
        {
            Chart::Series close;
            close.name = L"收盘";
            close.value = [](int i) { return Bars()[static_cast<size_t>(i)].close; };
            multi->AddSeries(std::move(close));

            // A 20-period moving average as a second series, coloured explicitly so the
            // two lines are distinguishable without a legend.
            Chart::Series ma;
            ma.name = L"MA20";
            ma.hasColor = true;
            ma.color = D2D1::ColorF(0xE3A008);
            ma.value = [](int i) {
                const int n = i >= 19 ? 20 : i + 1;
                double sum = 0.0;
                for (int k = i + 1 - n; k <= i; ++k)
                    sum += Bars()[static_cast<size_t>(k)].close;
                return sum / n;
            };
            multi->AddSeries(std::move(ma));
        }

        auto* toggles = card->Add(std::make_unique<Grid>());
        toggles->AddRow(GridLength::Auto());
        toggles->AddColumn(GridLength::Star(1.0f));
        toggles->AddColumn(GridLength::Star(1.0f));
        toggles->AddColumn(GridLength::Star(1.0f));

        auto* yAxis = toggles->Add(std::make_unique<CheckBox>());
        toggles->SetCell(yAxis, 0, 0);
        yAxis->SetText(L"Y 轴");
        yAxis->SetChecked(true);
        chartSubs_ += yAxis->Checked().Subscribe(multi,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<Chart*>(o)->SetShowYAxis(cb.IsChecked());
            });

        auto* xAxis = toggles->Add(std::make_unique<CheckBox>());
        toggles->SetCell(xAxis, 0, 1);
        xAxis->SetText(L"X 轴");
        xAxis->SetChecked(true);
        chartSubs_ += xAxis->Checked().Subscribe(multi,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<Chart*>(o)->SetShowXAxis(cb.IsChecked());
            });

        auto* gridLines = toggles->Add(std::make_unique<CheckBox>());
        toggles->SetCell(gridLines, 0, 2);
        gridLines->SetText(L"网格线");
        gridLines->SetChecked(true);
        chartSubs_ += gridLines->Checked().Subscribe(multi,
            [](void* o, CheckBox& cb, bool&) {
                static_cast<Chart*>(o)->SetShowGridLines(cb.IsChecked());
            });
    }

    // --- 4. In context ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：图表 + 指标 + 说明");
        auto* row = card->Add(std::make_unique<Grid>());
        row->AddRow(GridLength::Auto());
        row->AddColumn(GridLength::Star(3.0f));
        row->AddColumn(GridLength::Star(1.0f));

        auto* chart = row->Add(std::make_unique<Chart>());
        row->SetCell(chart, 0, 0);
        chart->SetHeight(200.0f);
        chart->SetKind(Chart::Kind::Line);
        chart->SetPointCount(static_cast<int>(Bars().size()));
        chart->SetVisibleRange(300, 200);
        chart->SetMargin(Thickness{0, 0, 12.0f, 0});
        {
            Chart::Series s;
            s.name = L"净值";
            s.value = [](int i) { return Bars()[static_cast<size_t>(i)].close / 40.0; };
            chart->AddSeries(std::move(s));
        }

        auto* side = row->Add(std::make_unique<StackPanel>());
        row->SetCell(side, 0, 1);
        side->SetSpacing(10.0f);
        struct S { const wchar_t* l; const wchar_t* v; const wchar_t* d;
                   Metric::Trend t; bool inv; };
        const S items[] = {
            {L"期末净值", L"1.043", L"+4.3%",  Metric::Trend::Up, false},
            {L"年化",     L"8.7%",  L"",       Metric::Trend::None, false},
            {L"最大回撤", L"-6.2%", L"+0.4pp", Metric::Trend::Up, true},
        };
        for (const S& it : items) {
            auto* m = side->Add(std::make_unique<Metric>());
            m->SetLabel(it.l);
            m->SetValue(it.v);
            if (it.t != Metric::Trend::None) m->SetDelta(it.d, it.t);
            m->SetInverted(it.inv);
        }

        auto* note = card->Add(std::make_unique<InfoBar>());
        note->SetSeverity(InfoBar::Severity::Informational);
        note->SetMessage(L"Chart 目前只有折线 / 柱状 / K 线三种。雷达图、热力图、"
                         L"散点图、瀑布图尚未实现 —— 它们都是加 Kind 的扩展，"
                         L"不是架构障碍。");
    }

    CreateCodeExample(content, LR"(auto* chart = panel->Add(std::make_unique<Chart>());
chart->SetHeight(200.0f);         // 图表没有自然高度，必须由调用者定
chart->SetKind(Chart::Kind::Candlestick);
chart->SetPointCount(500);        // 只是点数，数据仍在调用者手里
chart->SetVisibleRange(420, 80);  // 显示最后 80 根：滚动/缩放就是改这个

Chart::Series s;
s.open  = [](int i) { return Bars()[i].open;  };
s.high  = [](int i) { return Bars()[i].high;  };
s.low   = [](int i) { return Bars()[i].low;   };
s.close = [](int i) { return Bars()[i].close; };
chart->AddSeries(std::move(s));

chart->SetXLabelProvider([](int i) { return Dates()[i]; });)");

    return std::move(page);
}

}  // namespace fluent
