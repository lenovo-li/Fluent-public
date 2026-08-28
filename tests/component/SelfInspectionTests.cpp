// SelfInspectionTests.cpp — can the framework diagnose its own layout defects?
//
// WHY THIS FILE EXISTS. The Inspector demo page (FluentUIDemo/pages/InspectorPage.cpp)
// claims the framework can build its own debugging tools. A demo page cannot prove that:
// it lives in an exe CI never runs, and "it compiles" says nothing about whether the
// diagnosis is CORRECT.
//
// So this file tests the inspector's LOGIC rather than its UI: walk a live tree, compute
// the same "is this element compressed?" verdict the page shows, and check it fires on a
// tree with a real defect and stays silent on a healthy one.
//
// That is the actual self-test. A debugging tool that reports no problems on broken input
// is worse than no tool, because it converts an open question into false confidence. The
// specific defect used here is the one that started this whole thread: a container given a
// hand-typed height smaller than its content needs, which silently shears the text.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <cstdio>
#include <string>
#include <typeinfo>
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
        ctx.dwrite = &host.DWrite();
        ctx.dpiScale = 1.0f;
    }
    bool Ready() { return host.DWrite().Valid(); }
};

// --- The inspector's core logic, duplicated here ON PURPOSE -----------------
// This mirrors InspectorPage.cpp's Walk/compressed computation. Duplicated rather than
// shared because the page lives in the demo project which the test project does not link;
// if the two ever disagree, that is itself worth knowing, and the numbers below are small
// enough to keep in step.
struct Node {
    int depth = 0;
    std::wstring type;
    RectDip bounds;
    SizeDip desired;
    bool compressed = false;
};

std::wstring TypeName(FrameworkElement* el) {
    std::string s = typeid(*el).name();
    for (const char* prefix : {"class ", "struct "}) {
        const size_t n = std::string(prefix).size();
        if (s.rfind(prefix, 0) == 0) { s = s.substr(n); break; }
    }
    const size_t c = s.rfind("::");
    if (c != std::string::npos) s = s.substr(c + 2);
    return std::wstring(s.begin(), s.end());
}

void Walk(FrameworkElement* el, int depth, std::vector<Node>& out) {
    if (!el) return;
    Node n;
    n.depth = depth;
    n.type = TypeName(el);
    n.bounds = el->Bounds();
    n.desired = el->Desired();
    // 0.5 DIP tolerance: sub-pixel rounding inside a Star track is not compression.
    n.compressed = (n.desired.h - n.bounds.h > 0.5f) ||
                   (n.desired.w - n.bounds.w > 0.5f);
    out.push_back(std::move(n));
    if (auto* panel = dynamic_cast<Panel*>(el))
        for (size_t i = 0; i < panel->ChildCount(); ++i)
            Walk(panel->ChildAt(i), depth + 1, out);
}

int CountCompressed(const std::vector<Node>& nodes) {
    int n = 0;
    for (const Node& x : nodes) if (x.compressed) ++n;
    return n;
}

}  // namespace

// --- The tree walker has to actually reach things -----------------------------

// A walker that cannot descend reports zero problems for the trivial reason that it never
// looked. This pins that ChildAt/ChildCount really traverse, and that depth is tracked.
TEST(SelfInspection, WalkerReachesEveryPanelDescendantAndRecordsDepth) {
    Env env;
    StackPanel root;
    root.AttachToContext(env.ctx);

    auto* mid = root.Add(std::make_unique<StackPanel>());
    auto* leafA = mid->Add(std::make_unique<TextBlock>());
    leafA->SetText(L"A");
    auto* leafB = mid->Add(std::make_unique<CheckBox>());
    leafB->SetText(L"B");
    auto* sibling = root.Add(std::make_unique<TextBlock>());
    sibling->SetText(L"C");

    root.Measure(400.0f, 400.0f);
    root.Arrange(RectDip{0.0f, 0.0f, 400.0f, 400.0f});

    std::vector<Node> nodes;
    Walk(&root, 0, nodes);

    std::printf("  walked %zu nodes:", nodes.size());
    for (const Node& n : nodes)
        std::printf(" [d%d %ls]", n.depth, n.type.c_str());
    std::printf("\n");

    // root + mid + 2 leaves + sibling = 5
    EXPECT_EQ(nodes.size(), size_t(5));
    EXPECT_EQ(nodes[0].depth, 0);
    EXPECT_EQ(nodes[1].depth, 1);        // mid
    EXPECT_EQ(nodes[2].depth, 2);        // leaf inside mid
    // Type names must be usable, not empty or mangled beyond recognition.
    EXPECT_TRUE(nodes[0].type.find(L"StackPanel") != std::wstring::npos);
    EXPECT_TRUE(nodes[2].type.find(L"TextBlock") != std::wstring::npos);
}

// ChildAt must not walk off the end. A debugging tool iterating a tree that is being
// mutated is a normal situation, so an out-of-range index returns nullptr rather than
// asserting or reading past the vector.
TEST(SelfInspection, ChildAtIsBoundsCheckedRatherThanUndefined) {
    Env env;
    StackPanel root;
    root.AttachToContext(env.ctx);
    root.Add(std::make_unique<TextBlock>());

    EXPECT_TRUE(root.ChildAt(0) != nullptr);
    EXPECT_TRUE(root.ChildAt(1) == nullptr);
    EXPECT_TRUE(root.ChildAt(9999) == nullptr);
    EXPECT_EQ(root.ChildCount(), size_t(1));
}

// --- The diagnosis has to be right -------------------------------------------

// A healthy tree must report ZERO compressed elements. Without this, a detector that
// flagged everything would "pass" the defect test below while being useless.
TEST(SelfInspection, HealthyTreeReportsNoCompression) {
    Env env;
    if (!env.Ready()) { std::printf("  [SKIP] no DWrite\n"); return; }

    StackPanel root;
    root.AttachToContext(env.ctx);
    root.SetOrientation(StackPanel::Orientation::Vertical);

    auto* strip = root.Add(std::make_unique<Grid>());
    strip->AddRow(GridLength::Auto());          // content-sized: the correct shape
    for (int i = 0; i < 3; ++i) strip->AddColumn(GridLength::Star(1.0f));
    for (int i = 0; i < 3; ++i) {
        auto* m = strip->Add(std::make_unique<Metric>());
        strip->SetCell(m, 0, i);
        m->SetLabel(L"指标");
        m->SetValue(L"40.31");
        m->SetDelta(L"行业中位 31.2", Metric::Trend::Flat);
    }

    root.Measure(800.0f, 600.0f);
    root.Arrange(RectDip{0.0f, 0.0f, 800.0f, 600.0f});

    std::vector<Node> nodes;
    Walk(&root, 0, nodes);
    const int bad = CountCompressed(nodes);
    std::printf("  healthy tree: %zu nodes, %d compressed\n", nodes.size(), bad);
    EXPECT_EQ(bad, 0);
}

// THE SELF-TEST THAT MATTERS: the exact defect from the screenshot must be detected.
// A Grid row pinned to 58 DIP holding a three-line Metric that needs ~62.6.
TEST(SelfInspection, DetectsTheHandTypedHeightThatShearedTheMetricText) {
    Env env;
    if (!env.Ready()) return;

    StackPanel root;
    root.AttachToContext(env.ctx);

    auto* strip = root.Add(std::make_unique<Grid>());
    strip->AddRow(GridLength::Star(1.0f));
    strip->AddColumn(GridLength::Star(1.0f));
    strip->SetHeight(58.0f);                     // the original bug

    auto* m = strip->Add(std::make_unique<Metric>());
    strip->SetCell(m, 0, 0);
    m->SetLabel(L"市盈率 TTM");
    m->SetValue(L"28.40");
    m->SetDelta(L"行业中位 31.2", Metric::Trend::Flat);

    root.Measure(800.0f, 600.0f);
    root.Arrange(RectDip{0.0f, 0.0f, 800.0f, 600.0f});

    std::vector<Node> nodes;
    Walk(&root, 0, nodes);
    const int bad = CountCompressed(nodes);

    // Report it the way the inspector page does, so the failure output is the diagnosis.
    for (const Node& n : nodes) {
        if (!n.compressed) continue;
        std::printf("  ⚠ %ls: desired %.1f×%.1f but arranged %.1f×%.1f (short %.1f DIP)\n",
                    n.type.c_str(), n.desired.w, n.desired.h,
                    n.bounds.w, n.bounds.h, n.desired.h - n.bounds.h);
    }
    EXPECT_TRUE(bad >= 1);

    // And specifically the Metric, not merely "something somewhere".
    bool metricFlagged = false;
    for (const Node& n : nodes)
        if (n.compressed && n.type.find(L"Metric") != std::wstring::npos)
            metricFlagged = true;
    EXPECT_TRUE(metricFlagged);
}

// Switching the row to Auto must make the same tree come back clean. This is the "the fix
// is verifiable by the tool" half: a detector that fires on both the broken and the fixed
// tree cannot tell anyone whether their fix worked.
TEST(SelfInspection, TheSameTreeIsCleanOnceTheRowSizesToContent) {
    Env env;
    if (!env.Ready()) return;

    auto build = [&env](bool pinHeight) {
        auto root = std::make_unique<StackPanel>();
        root->AttachToContext(env.ctx);
        auto* strip = root->Add(std::make_unique<Grid>());
        strip->AddRow(pinHeight ? GridLength::Star(1.0f) : GridLength::Auto());
        strip->AddColumn(GridLength::Star(1.0f));
        if (pinHeight) strip->SetHeight(58.0f);
        auto* m = strip->Add(std::make_unique<Metric>());
        strip->SetCell(m, 0, 0);
        m->SetLabel(L"市盈率 TTM");
        m->SetValue(L"28.40");
        m->SetDelta(L"行业中位 31.2", Metric::Trend::Flat);
        root->Measure(800.0f, 600.0f);
        root->Arrange(RectDip{0.0f, 0.0f, 800.0f, 600.0f});
        return root;
    };

    std::vector<Node> broken, fixed;
    auto brokenTree = build(true);
    Walk(brokenTree.get(), 0, broken);
    auto fixedTree = build(false);
    Walk(fixedTree.get(), 0, fixed);

    std::printf("  pinned height -> %d compressed; Auto row -> %d compressed\n",
                CountCompressed(broken), CountCompressed(fixed));
    EXPECT_TRUE(CountCompressed(broken) >= 1);
    EXPECT_EQ(CountCompressed(fixed), 0);
}

// A wrapped InfoBar in a too-short container must also be caught. Different control,
// different reason for its height (wrapping rather than line count), same verdict --
// which is what makes the check general rather than Metric-specific.
TEST(SelfInspection, DetectsAClippedWrappedInfoBarToo) {
    Env env;
    if (!env.Ready()) return;

    StackPanel root;
    root.AttachToContext(env.ctx);

    auto* box = root.Add(std::make_unique<Grid>());
    box->AddRow(GridLength::Star(1.0f));
    box->AddColumn(GridLength::Star(1.0f));
    box->SetHeight(40.0f);                        // far too short for wrapped text

    auto* bar = box->Add(std::make_unique<InfoBar>());
    box->SetCell(bar, 0, 0);
    bar->SetTitle(L"演示数据");
    bar->SetMessage(L"本页所有数字都是进程内生成的合成数据，用于验证框架能否表达"
                    L"这类界面，原项目通过网络取数，本框架没有 HTTP 客户端。");

    root.Measure(360.0f, 600.0f);
    root.Arrange(RectDip{0.0f, 0.0f, 360.0f, 600.0f});

    std::vector<Node> nodes;
    Walk(&root, 0, nodes);
    bool infoBarFlagged = false;
    for (const Node& n : nodes) {
        if (n.compressed && n.type.find(L"InfoBar") != std::wstring::npos) {
            infoBarFlagged = true;
            std::printf("  ⚠ InfoBar wants %.1f DIP tall, got %.1f\n",
                        n.desired.h, n.bounds.h);
        }
    }
    EXPECT_TRUE(infoBarFlagged);
}

// The tolerance must not swallow real defects nor flag rounding. 0.5 DIP is the line: a
// 4.6 DIP shortfall (the real bug) is caught, a 0.2 DIP rounding artefact is not.
TEST(SelfInspection, ToleranceSeparatesRoundingFromRealShortfalls) {
    Env env;

    // Synthetic nodes rather than a built tree: this is a test of the THRESHOLD, and
    // constructing a tree that lands exactly 0.2 DIP short is not reliably possible.
    Node rounding;
    rounding.desired = {100.0f, 62.8f};
    rounding.bounds = {0.0f, 0.0f, 100.0f, 62.6f};   // 0.2 short
    rounding.compressed = (rounding.desired.h - rounding.bounds.h > 0.5f);

    Node realBug;
    realBug.desired = {100.0f, 62.6f};
    realBug.bounds = {0.0f, 0.0f, 100.0f, 58.0f};    // 4.6 short — the screenshot
    realBug.compressed = (realBug.desired.h - realBug.bounds.h > 0.5f);

    EXPECT_FALSE(rounding.compressed);
    EXPECT_TRUE(realBug.compressed);
}
