// ContentDrivenHeightTests.cpp — hard-coded container heights clip their content.
//
// THE BUG THIS FILE EXISTS FOR. The A-share replica page put its KPI strip in a Grid with
// SetHeight(58) — a number typed by hand. A Metric with a label, a value and a delta line
// actually needs 62.5 DIP, so the last line was sheared off: the screenshot showed
// "行业中位 31.2" with the bottom of its glyphs cut away, while the three neighbours (whose
// sub-lines happened to sit slightly higher) looked fine.
//
// The user's diagnosis was exactly right and worth writing down, because it is the general
// rule this codebase should follow: IF YOU HAVE LAYOUT, YOU SHOULD NOT BE TYPING SIZES.
// A hand-typed height is a guess about a font's line metrics, and it silently goes wrong
// when the font, the DPI, the theme's type ramp, or the control's content changes. The
// framework already knows the right number — it is what Measure computed.
//
// So these tests assert the CONTRACT rather than the specific numbers:
//   * a control's Desired height is enough for everything it draws, and
//   * a container that lets its rows size to content never gives a child less than the
//     child asked for.
// A test pinned to "62.5" would break on the next font change and teach nobody anything.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <cstdio>

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
        ctx.dwrite = &host.DWrite();
        ctx.dpiScale = 1.0f;
    }
    bool Ready() { return host.DWrite().Valid(); }
};

Metric* AddFullMetric(Grid& g, int col, const wchar_t* label,
                      const wchar_t* value, const wchar_t* delta) {
    auto* m = g.Add(std::make_unique<Metric>());
    g.SetCell(m, 0, col);
    m->SetLabel(label);
    m->SetValue(value);
    m->SetDelta(delta, Metric::Trend::Up);
    return m;
}

}  // namespace

// A three-line Metric must report a height that fits all three lines. This is the direct
// regression for the screenshot: the control was right, the hand-typed 58 was wrong.
TEST(ContentDrivenHeight, ThreeLineMetricNeedsMoreThanTheHandTypedFiftyEight) {
    Env env;
    if (!env.Ready()) { std::printf("  [SKIP] no DWrite\n"); return; }

    Metric m;
    m.AttachToContext(env.ctx);
    m.SetLabel(L"市盈率 TTM");
    m.SetValue(L"28.40");
    m.SetDelta(L"行业中位 31.2", Metric::Trend::Flat);
    m.Measure(200.0f, 1000.0f);

    std::printf("  three-line Metric wants %.1f DIP (the page hard-coded 58)\n",
                m.Desired().h);
    // The point is not the exact value but that 58 was NOT enough — which is why typing it
    // was the mistake.
    EXPECT_TRUE(m.Desired().h > 58.0f);
}

// An Auto row must give every child at least its desired height. This is the fix: let the
// row size to content instead of typing a number.
TEST(ContentDrivenHeight, AutoRowGivesEveryChildItsFullDesiredHeight) {
    Env env;
    if (!env.Ready()) return;

    Grid strip;
    strip.AttachToContext(env.ctx);
    strip.AddRow(GridLength::Auto());          // the fix, instead of SetHeight(58)
    for (int i = 0; i < 4; ++i) strip.AddColumn(GridLength::Star(1.0f));

    Metric* m[4];
    m[0] = AddFullMetric(strip, 0, L"最新价", L"40.31", L"+0.61%");
    m[1] = AddFullMetric(strip, 1, L"市盈率 TTM", L"28.40", L"行业中位 31.2");
    m[2] = AddFullMetric(strip, 2, L"市净率", L"3.17", L"分位 62%");
    m[3] = AddFullMetric(strip, 3, L"资产负债率", L"41.8%", L"+1.9%");

    strip.Measure(800.0f, 1000.0f);
    strip.Arrange(RectDip{0.0f, 0.0f, 800.0f, strip.Desired().h});

    std::printf("  strip desired h = %.1f\n", strip.Desired().h);
    for (int i = 0; i < 4; ++i) {
        std::printf("    tile %d: desired %.1f, arranged %.1f\n",
                    i, m[i]->Desired().h, m[i]->Bounds().h);
        // No tile may be arranged shorter than it asked for: that is what clips glyphs.
        EXPECT_TRUE(m[i]->Bounds().h >= m[i]->Desired().h - 0.01f);
    }
    // And the strip itself must be at least as tall as its tallest child.
    EXPECT_TRUE(strip.Desired().h >= m[1]->Desired().h - 0.01f);
}

// COMPRESSION IS THE FRAMEWORK'S CHOSEN CONTRACT, not a defect — this test pins it.
//
// `ComputeArrangeRect` deliberately does `std::min(desired, available)`: a child is never
// arranged larger than the slot it was given, so layout boundaries are hard boundaries and
// nothing paints outside its container. The alternative (WPF's, where the child keeps its
// desired size and overflows) was considered and rejected: overflow is visually noisier,
// and a hard boundary is easier to reason about.
//
// The consequence, which every caller has to know: a container sized SMALLER than its
// content silently truncates that content. There is no warning. That is exactly what
// produced the sheared "行业中位 31.2" in the screenshot — and under this contract the bug
// was in the CALLER (a hand-typed 58 where the content needed 62.6), not in the framework.
//
// This test therefore asserts the compression rather than complaining about it, so that
// (a) nobody "fixes" it into overflow without a deliberate decision, and (b) the reason a
// hand-typed size is dangerous is recorded next to the behaviour that makes it dangerous.
TEST(ContentDrivenHeight, FixedContainerCompressesContentByDesign) {
    Env env;
    if (!env.Ready()) return;

    Grid strip;
    strip.AttachToContext(env.ctx);
    strip.AddRow(GridLength::Star(1.0f));
    strip.AddColumn(GridLength::Star(1.0f));
    strip.SetHeight(58.0f);                     // the original bug, reproduced

    Metric* m = AddFullMetric(strip, 0, L"市盈率 TTM", L"28.40", L"行业中位 31.2");
    strip.Measure(800.0f, 1000.0f);
    strip.Arrange(RectDip{0.0f, 0.0f, 800.0f, 58.0f});

    std::printf("  fixed 58: tile wants %.1f, gets %.1f -> content short by %.1f DIP\n",
                m->Desired().h, m->Bounds().h, m->Desired().h - m->Bounds().h);
    // The contract: never larger than the slot. The tile is compressed, and the part of its
    // last line that does not fit is clipped rather than painted outside.
    EXPECT_TRUE(m->Bounds().h <= 58.0f + 0.01f);
    EXPECT_TRUE(m->Bounds().h < m->Desired().h);
}

// Two-line vs three-line Metrics must differ in height, so a strip mixing them sizes to
// the taller one rather than to whichever happens to be first.
TEST(ContentDrivenHeight, StripSizesToTheTallestTileNotTheFirstOne) {
    Env env;
    if (!env.Ready()) return;

    Grid strip;
    strip.AttachToContext(env.ctx);
    strip.AddRow(GridLength::Auto());
    strip.AddColumn(GridLength::Star(1.0f));
    strip.AddColumn(GridLength::Star(1.0f));

    // First tile: two lines only (no delta).
    auto* shortTile = strip.Add(std::make_unique<Metric>());
    strip.SetCell(shortTile, 0, 0);
    shortTile->SetLabel(L"市净率");
    shortTile->SetValue(L"3.17");

    // Second tile: three lines.
    Metric* tallTile = AddFullMetric(strip, 1, L"市盈率 TTM", L"28.40", L"行业中位 31.2");

    strip.Measure(600.0f, 1000.0f);
    strip.Arrange(RectDip{0.0f, 0.0f, 600.0f, strip.Desired().h});

    std::printf("  2-line %.1f vs 3-line %.1f, strip %.1f\n",
                shortTile->Desired().h, tallTile->Desired().h, strip.Desired().h);
    EXPECT_TRUE(tallTile->Desired().h > shortTile->Desired().h);
    EXPECT_TRUE(tallTile->Bounds().h >= tallTile->Desired().h - 0.01f);
}

// The same rule for InfoBar, whose height is wrap-dependent and therefore even less
// predictable by hand: a hard-coded height would clip a message the moment the window
// narrows. A StackPanel (which sizes rows to content) must give it what it asked for.
TEST(ContentDrivenHeight, WrappedInfoBarGetsItsFullHeightFromAContentSizedParent) {
    Env env;
    if (!env.Ready()) return;

    StackPanel column;
    column.AttachToContext(env.ctx);
    column.SetOrientation(StackPanel::Orientation::Vertical);

    auto* bar = column.Add(std::make_unique<InfoBar>());
    bar->SetTitle(L"演示数据，非真实行情");
    bar->SetMessage(L"本页所有数字都是进程内生成的合成数据，用于验证 FluentUI 能否表达"
                    L"这类数据分析界面。原项目通过 akshare 联网取数，本框架没有 HTTP "
                    L"客户端，因此不含任何真实行情。所有指标均不构成投资建议。");

    for (float w : {900.0f, 600.0f, 380.0f}) {
        column.Measure(w, 2000.0f);
        column.Arrange(RectDip{0.0f, 0.0f, w, column.Desired().h});
        std::printf("  width %.0f -> bar desired %.1f, arranged %.1f\n",
                    w, bar->Desired().h, bar->Bounds().h);
        EXPECT_TRUE(bar->Bounds().h >= bar->Desired().h - 0.01f);
        EXPECT_TRUE(bar->Bounds().h > 0.0f);
    }
}
