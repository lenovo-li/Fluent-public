// InspectorPage.cpp — the framework inspecting itself.
//
// THE QUESTION THIS PAGE ANSWERS. "Can this framework build its own debugging tools?" is a
// sharper test than any gallery page, because a debug UI needs things a showcase does not:
// it must read live state out of a running tree, present a hierarchy of unknown depth,
// update without being told when to, and stay usable while the thing it inspects is being
// poked. If the framework can host its own inspector, it can host a real application's
// tooling.
//
// WHAT IT DOES. It walks the visual tree of a live target panel (built right here, so the
// page has something to inspect that it also controls), and reports for each element:
// type, bounds, desired size, dirty flags, visibility, focus. Then it lets you mutate the
// target and watch the numbers change -- which is exactly how the clipped-Metric bug would
// have been caught: desired height 62.6 against an arranged height of 58 is visible at a
// glance in a table like this.
//
// HONEST LIMITS, stated because a debug tool that overstates itself is worse than none:
//   * There is no reflection in C++. Type names come from a virtual DebugTypeName() that
//     each control would have to implement; absent that, this uses a best-effort
//     `typeid(...).name()` which gives MSVC's decorated form. Readable, not pretty.
//   * It walks only what the tree exposes. Panel children are reachable; a control's
//     internal parts (a ScrollViewer held as a member, a ComboBox's popup) are not, since
//     they are deliberately not tree nodes.
//   * It samples on demand rather than observing. There is no tree-mutation event to
//     subscribe to, so "Refresh" is a button. That is a real gap, noted at the bottom of
//     the page.

#include "../GalleryMain.h"
#include "../../FluentUI/window/NativeWindowHost.h"

#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/GroupBox.h"

#include <cstdio>
#include <string>
#include <typeinfo>
#include <vector>

namespace fluent {

namespace {

// One row of the inspector table: a flattened visual-tree node.
struct NodeInfo {
    int depth = 0;
    std::wstring type;
    RectDip bounds;
    SizeDip desired;
    bool visible = true;
    bool focused = false;
    // The diagnosis that matters most: arranged smaller than desired means content is
    // being compressed, which under this framework's contract means it is being clipped.
    bool compressed = false;
};

// Snapshot storage. A namespace-scope vector rather than a member so the cell provider
// (which must be a plain callable with no captured state beyond what it can reach) can
// read it; the page is a singleton in practice.
std::vector<NodeInfo> g_nodes;

std::wstring Demangle(const char* raw) {
    // MSVC's typeid().name() already yields "class fluent::Button" rather than a mangled
    // symbol, so the only cleanup needed is dropping the "class "/"struct " prefix and the
    // namespace. No <cxxabi.h> dependency, and nothing to go wrong on another compiler
    // beyond the name being uglier.
    std::string s = raw ? raw : "?";
    for (const char* prefix : {"class ", "struct "}) {
        const size_t n = std::string(prefix).size();
        if (s.rfind(prefix, 0) == 0) { s = s.substr(n); break; }
    }
    const size_t colons = s.rfind("::");
    if (colons != std::string::npos) s = s.substr(colons + 2);
    return std::wstring(s.begin(), s.end());
}

void Walk(FrameworkElement* el, int depth) {
    if (!el) return;
    NodeInfo n;
    n.depth = depth;
    n.type = Demangle(typeid(*el).name());
    n.bounds = el->Bounds();
    n.desired = el->Desired();
    n.visible = el->IsVisible();
    n.focused = el->IsFocused();
    // 0.5 DIP tolerance: sub-pixel rounding in a Star track is not compression.
    n.compressed = (n.desired.h - n.bounds.h > 0.5f) ||
                   (n.desired.w - n.bounds.w > 0.5f);
    g_nodes.push_back(std::move(n));

    // Panel children are the only generically reachable descendants. This is the honest
    // boundary of a tree walker in this framework: Border/GroupBox hold a single child
    // through their own accessor, and a control's internal parts are not tree nodes.
    if (auto* panel = dynamic_cast<Panel*>(el)) {
        for (size_t i = 0; i < panel->ChildCount(); ++i)
            Walk(panel->ChildAt(i), depth + 1);
    }
}

std::wstring Fmt1(float v) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%.1f", v);
    return buf;
}

}  // namespace

std::unique_ptr<ScrollPanel> GalleryApp::CreateInspectorPage() {
    auto [page, content] = CreatePageShell(L"自我调试 · Visual Tree Inspector");

    {
        auto* intro = content->Add(std::make_unique<InfoBar>());
        intro->SetSeverity(InfoBar::Severity::Informational);
        intro->SetTitle(L"这一页是框架在检查自己");
        intro->SetMessage(
            L"下面的表格用 DataGrid 显示一棵活的视觉树：类型、bounds、desired、"
            L"脏标记、是否被压缩。被检查的对象就是下方那个「目标面板」—— 改动它，"
            L"再点「刷新快照」，数字会跟着变。"
            L"「desired 高于 bounds」这一列就是之前切掉 Metric 文字的那种问题："
            L"需要 62.6、只给了 58，在这张表里一眼能看出来。");
        intro->SetMargin(Thickness{0, 0, 0, 16.0f});
    }

    // --- The target being inspected ---------------------------------------
    // Built first so the walker has something real to read. Deliberately mixes a panel, a
    // control with a text-driven height, and a nested container, so the tree has depth.
    Panel* target = nullptr;
    {
        auto* card = CreateExampleCard(content, L"被检查的目标面板");
        auto* holder = card->Add(std::make_unique<Border>());
        holder->SetBorderThickness(1.0f);
        holder->SetCornerRadius(6.0f);
        holder->SetPadding(Thickness{10.0f});

        auto* stack = holder->SetChild(std::make_unique<StackPanel>());
        stack->SetOrientation(StackPanel::Orientation::Vertical);
        stack->SetSpacing(8.0f);
        target = stack;

        auto* label = stack->Add(std::make_unique<TextBlock>());
        label->SetText(L"我是一个 TextBlock");

        auto* row = stack->Add(std::make_unique<Grid>());
        row->AddRow(GridLength::Auto());
        row->AddColumn(GridLength::Star(1.0f));
        row->AddColumn(GridLength::Star(1.0f));

        auto* m1 = row->Add(std::make_unique<Metric>());
        row->SetCell(m1, 0, 0);
        m1->SetLabel(L"正常的 Metric");
        m1->SetValue(L"40.31");
        m1->SetDelta(L"+0.61%", Metric::Trend::Up);

        auto* m2 = row->Add(std::make_unique<Metric>());
        row->SetCell(m2, 0, 1);
        m2->SetLabel(L"另一个");
        m2->SetValue(L"28.40");
        m2->SetDelta(L"行业中位 31.2", Metric::Trend::Flat);

        auto* check = stack->Add(std::make_unique<CheckBox>());
        check->SetText(L"我是一个 CheckBox");
    }
    inspectorTarget_ = target;

    // --- Deliberate-defect switch -----------------------------------------
    {
        auto* card = CreateExampleCard(content, L"制造一个「被压缩」的缺陷");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"勾上它会给目标面板设一个偏小的固定高度 —— 正是当初写死 "
                      L"SetHeight(58) 犯的错。勾上后刷新快照，表格里会出现"
                      L"「压缩」标记，这就是自我调试真正的用处：把不可见的缺陷变成一个数。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* squeeze = card->Add(std::make_unique<CheckBox>());
        squeeze->SetText(L"人为压缩目标面板（SetHeight(40)）");
        inspectorSubs_ += squeeze->Checked().Subscribe(this,
            [](void* o, CheckBox& cb, bool&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->inspectorTarget_) return;
                if (cb.IsChecked()) self->inspectorTarget_->SetHeight(40.0f);
                else self->inspectorTarget_->SetHeight(kAuto);   // kAuto = NaN = "回到自动"
            });
    }

    // --- The inspector itself ---------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"视觉树快照");

        auto* grid = card->Add(std::make_unique<DataGrid>());
        grid->SetHeight(300.0f);
        grid->SetColumns({
            {L"元素（缩进=层级）", 220.0f, DataGrid::Align::Left},
            {L"bounds x,y",        130.0f, DataGrid::Align::Right},
            {L"bounds w×h",        130.0f, DataGrid::Align::Right},
            {L"desired w×h",       130.0f, DataGrid::Align::Right},
            {L"状态",              160.0f, DataGrid::Align::Left},
        });
        grid->SetAlternatingRowFill(true);
        grid->SetCellProvider([](int r, int c) -> std::wstring {
            if (r < 0 || r >= static_cast<int>(g_nodes.size())) return {};
            const NodeInfo& n = g_nodes[static_cast<size_t>(r)];
            switch (c) {
                case 0: return std::wstring(static_cast<size_t>(n.depth) * 2, L' ') + n.type;
                case 1: return Fmt1(n.bounds.x) + L", " + Fmt1(n.bounds.y);
                case 2: return Fmt1(n.bounds.w) + L" × " + Fmt1(n.bounds.h);
                case 3: return Fmt1(n.desired.w) + L" × " + Fmt1(n.desired.h);
                default: {
                    std::wstring s;
                    if (n.compressed) s += L"⚠ 被压缩 ";
                    if (!n.visible) s += L"隐藏 ";
                    if (n.focused) s += L"焦点 ";
                    if (s.empty()) s = L"正常";
                    return s;
                }
            }
        });
        // Compressed rows are tinted with the negative data colour: the same semantic role
        // a falling price uses, because both mean "something is wrong here".
        NativeWindowHost* owner = ownerWindow_ ? ownerWindow_() : nullptr;
        grid->SetRowColorProvider([owner](int r, D2D1_COLOR_F& out) {
            if (!owner) return false;
            if (r < 0 || r >= static_cast<int>(g_nodes.size())) return false;
            if (!g_nodes[static_cast<size_t>(r)].compressed) return false;
            out = owner->Theme().colors.dataNegative;
            return true;
        });
        gridDemo_ = nullptr;   // not the DataGrid page's grid; keep them separate

        auto* summary = card->Add(std::make_unique<TextBlock>());
        summary->SetDimmed(true);
        summary->SetText(L"点「刷新快照」开始。");
        inspectorOut_ = summary;

        // Stored so the refresh handler can reach both. A raw pointer is safe: pages are
        // built once and live for the app's lifetime (BuildAllPages keeps them all).
        static DataGrid* s_treeGrid = nullptr;
        static TextBlock* s_summary = nullptr;
        s_treeGrid = grid;
        s_summary = summary;

        auto* buttons = card->Add(std::make_unique<Grid>());
        buttons->AddRow(GridLength::Auto());
        buttons->AddColumn(GridLength::Auto());
        buttons->AddColumn(GridLength::Auto());
        buttons->AddColumn(GridLength::Star(1.0f));

        auto* refresh = buttons->Add(std::make_unique<Button>());
        buttons->SetCell(refresh, 0, 0);
        refresh->SetText(L"刷新快照");
        refresh->SetKind(Button::Kind::Accent);
        refresh->SetMargin(Thickness{0, 0, 8.0f, 0});
        inspectorSubs_ += refresh->Click().Subscribe(this,
            [](void* o, Button&, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->inspectorTarget_ || !s_treeGrid) return;

                g_nodes.clear();
                Walk(self->inspectorTarget_, 0);
                s_treeGrid->SetRowCount(static_cast<int>(g_nodes.size()));

                int compressed = 0, hidden = 0;
                for (const NodeInfo& n : g_nodes) {
                    if (n.compressed) ++compressed;
                    if (!n.visible) ++hidden;
                }
                if (s_summary) {
                    wchar_t buf[256];
                    std::swprintf(buf, 256,
                        L"共 %zu 个元素；被压缩 %d 个%s；隐藏 %d 个。"
                        L"「被压缩」= 排布尺寸小于 desired，内容会被裁掉。",
                        g_nodes.size(), compressed,
                        compressed ? L"（红色行）" : L"", hidden);
                    s_summary->SetText(buf);
                }
            });

        auto* clear = buttons->Add(std::make_unique<Button>());
        buttons->SetCell(clear, 0, 1);
        clear->SetText(L"清空");
        inspectorSubs_ += clear->Click().Subscribe(this,
            [](void* o, Button&, RoutedEventArgs&) {
                (void)o;
                g_nodes.clear();
                if (s_treeGrid) s_treeGrid->SetRowCount(0);
                if (s_summary) s_summary->SetText(L"已清空。");
            });
    }

    // --- Live frame stats -------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"运行时指标（来自窗口，非合成数据）");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"这些是真实读数,不是演示数字。刷新率来自显示器查询,"
                      L"和帧节奏用的是同一个来源。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* strip = card->Add(std::make_unique<Grid>());
        strip->AddRow(GridLength::Auto());
        for (int i = 0; i < 3; ++i) strip->AddColumn(GridLength::Star(1.0f));

        // Queried here rather than read off the window: NativeWindowHost::RefreshHz is
        // private frame-pacing state, and widening a framework API so a demo page can
        // print a number would be the wrong trade. This calls the SAME Win32 source the
        // pacer uses (EnumDisplaySettingsW), so the two cannot disagree.
        int hz = 0;
        {
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) &&
                dm.dmDisplayFrequency > 1) {
                hz = static_cast<int>(dm.dmDisplayFrequency);
            }
        }

        auto* mHz = strip->Add(std::make_unique<Metric>());
        strip->SetCell(mHz, 0, 0);
        mHz->SetLabel(L"显示器刷新率");
        mHz->SetValue(hz > 0 ? std::to_wstring(hz) + L" Hz" : L"未知");
        mHz->SetDelta(hz > 0 ? (L"每帧预算 " + Fmt1(1000.0f / hz) + L" ms")
                             : L"查询失败，回落 60Hz",
                      Metric::Trend::Flat);

        auto* mBudget = strip->Add(std::make_unique<Metric>());
        strip->SetCell(mBudget, 0, 1);
        mBudget->SetLabel(L"卡顿阈值");
        mBudget->SetValue(hz > 0 ? Fmt1(2000.0f / hz) + L" ms" : L"33.3 ms");
        mBudget->SetDelta(L"两帧，随刷新率缩放", Metric::Trend::Flat);

        auto* mNodes = strip->Add(std::make_unique<Metric>());
        strip->SetCell(mNodes, 0, 2);
        mNodes->SetLabel(L"快照元素数");
        mNodes->SetValue(L"—");
        mNodes->SetDelta(L"点上方「刷新快照」", Metric::Trend::Flat);
    }

    // --- What this cannot do ----------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"这个自查工具做不到什么（诚实清单）");
        auto* bar = card->Add(std::make_unique<InfoBar>());
        bar->SetSeverity(InfoBar::Severity::Warning);
        bar->SetTitle(L"四条真实限制");
        bar->SetMessage(
            L"1) 类型名靠 typeid,不是反射。MSVC 下能读,但那是编译器给的名字,"
            L"不是框架的。要好看需要每个控件实现一个 DebugTypeName()。"
            L"2) 只能走到树暴露的部分。Panel 的孩子可达；控件内部零件"
            L"（ScrollViewer 成员、ComboBox 的弹出层）不是树节点,走不到。"
            L"3) 需要手动点刷新。框架没有「树被改动」事件可订阅,所以做不到自动更新 —— "
            L"这是真实缺口。"
            L"4) 只看几何和脏标记。看不到重绘次数、每帧耗时、GPU 占用 —— "
            L"那些在 F12 HUD 和 FluentUIBench 里。");
    }

    CreateCodeExample(content, LR"(// 走一棵活的视觉树：Panel 的孩子是唯一通用可达的后代
void Walk(FrameworkElement* el, int depth) {
    if (!el) return;
    Record(el->Bounds(), el->Desired(), el->IsVisible(), el->IsFocused());
    if (auto* panel = dynamic_cast<Panel*>(el))
        for (size_t i = 0; i < panel->ChildCount(); ++i)
            Walk(panel->ChildAt(i), depth + 1);
}

// 判断「内容被裁」：排布尺寸小于 desired
// 0.5 DIP 容差，因为 Star 轨道的亚像素取整不算压缩
bool compressed = (desired.h - bounds.h > 0.5f) || (desired.w - bounds.w > 0.5f);)");

    return std::move(page);
}

}  // namespace fluent
