// LayoutPages.cpp — 布局容器页面
#include "../GalleryMain.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/WrapPanel.h"
#include "../../FluentUI/layout/UniformGrid.h"
#include "../../FluentUI/layout/DockPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/Canvas.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Viewbox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/Expander.h"

namespace fluent {

namespace {

// 布局演示的每个格子都必须画出边框和底色：不画边，「拉伸填满」和「按内容收缩」在
// 屏幕上是同一个样子（和 ShowcaseHelpers 里 AddBoundedBox 的理由一致）。
Border* AddCell(Panel* into, std::wstring label, UINT32 tint = 0, float alpha = 0.18f) {
    auto* cell = into->Add(std::make_unique<Border>());
    cell->SetBorderThickness(1.0f);
    cell->SetCornerRadius(4.0f);
    cell->SetPadding(Thickness{8.0f, 6.0f, 8.0f, 6.0f});
    if (tint != 0) cell->SetBackground(D2D1::ColorF(tint, alpha));
    auto* text = cell->SetChild(std::make_unique<TextBlock>());
    text->SetText(std::move(label));
    text->SetFontSize(12.0f);
    return cell;
}

// 停靠一条边。刻意不写死厚度：DockPanel::ArrangeOverride 用子元素的 desired.w/h
// 切条，所以一个内容自适应的 Border 得到的正好是「包住文字」的一条。
Border* AddDocked(DockPanel* dock, Dock edge, std::wstring label, UINT32 tint) {
    Border* strip = AddCell(dock, std::move(label), tint);
    DockPanel::SetDock(strip, edge);
    return strip;
}

// 定高的演示舞台。DockPanel 和 Star 行只有在高度有界时才有意义：页面外层是
// ScrollPanel，它用无穷高度测量子元素，Star 轨道会解析成无穷并让上游的
// leftover = avail - used 变成 NaN。这里的高度是「可用空间」这个输入本身，
// 不是猜出来的文字行高。
Border* AddStage(Panel* parent, float height) {
    auto* frame = parent->Add(std::make_unique<Border>());
    frame->SetBorderThickness(1.0f);
    frame->SetCornerRadius(6.0f);
    frame->SetPadding(Thickness{6.0f});
    frame->SetHeight(height);
    return frame;
}

// 一行小字说明，跟在演示后面。比 AddNote 更紧凑，用于给对照组打标签。
void AddCaption(Panel* parent, std::wstring text) {
    auto* t = parent->Add(std::make_unique<TextBlock>());
    t->SetText(std::move(text));
    t->SetFontSize(12.0f);
    t->SetWrap(true);
    t->SetDimmed(true);
}

}  // namespace

std::unique_ptr<ScrollPanel> GalleryApp::CreateGridPage() {
    auto [page, content] = CreatePageShell(L"Grid");

    // --- 1. 基础用法 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"基础用法");
        AddNote(card, L"Grid 的单元格归属不存在 FrameworkElement 上，而是 Grid 自己按子元素指针"
                      L"记的一张表 —— 所以先 Add 拿到指针，再 SetCell 指定位置，两步不能合并。"
                      L"没被 SetCell 过的子元素落在 (0,0)。");

        auto* g = card->Add(std::make_unique<Grid>());
        g->SetColumnSpacing(8.0f);
        g->SetRowSpacing(8.0f);
        // 行用 Auto：格子里是文字，行高该由文字决定。列用 Star 等分可用宽度。
        for (int r = 0; r < 2; ++r) g->AddRow(GridLength::Auto());
        for (int c = 0; c < 3; ++c) g->AddColumn(GridLength::Star(1.0f));
        int swatchCount = 0;
        const AccentSwatch* sw = AccentSwatches(swatchCount);
        for (int i = 0; i < 6; ++i) {
            Border* cell = AddCell(g, L"(" + std::to_wstring(i / 3) + L", " +
                                          std::to_wstring(i % 3) + L")",
                                   sw[i % swatchCount].color);
            g->SetCell(cell, i / 3, i % 3);
        }

        auto* sec = AddSubSection(card, L"跨行跨列：SetCell 的第 4、5 个参数");
        AddNote(sec, L"跨度会在布局时被夹到轨道总数内，所以写大了不会越界，只是不再增长。");
        auto* span = sec->Add(std::make_unique<Grid>());
        span->SetColumnSpacing(6.0f);
        span->SetRowSpacing(6.0f);
        for (int r = 0; r < 2; ++r) span->AddRow(GridLength::Auto());
        for (int c = 0; c < 4; ++c) span->AddColumn(GridLength::Star(1.0f));
        Border* banner = AddCell(span, L"colSpan = 4", sw[0].color);
        span->SetCell(banner, 0, 0, 1, 4);
        Border* tall = AddCell(span, L"rowSpan = 1", sw[1].color);
        span->SetCell(tall, 1, 0);
        for (int c = 1; c < 4; ++c) {
            Border* cell = AddCell(span, L"(1, " + std::to_wstring(c) + L")", sw[2].color);
            span->SetCell(cell, 1, c);
        }
    }

    // --- 2. 尺寸模式 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"尺寸模式：Auto / Pixels / Star");
        AddNote(card, L"三种轨道解析的顺序决定了它们各自的用途：Pixels 和 Auto 先各自"
                      L"占掉自己要的量，Star 再按权重分剩下的。所以 Star 是唯一「跟着容器"
                      L"变」的，另两种是「不跟着变」。");

        auto* sec1 = AddSubSection(card, L"同一个三列布局，容器变宽时只有 Star 列在变");
        for (float w : {560.0f, 400.0f, 280.0f}) {
            auto* box = AddBoundedBox(sec1, w,
                std::wstring(L"容器 ") + std::to_wstring(static_cast<int>(w)) + L" DIP");
            auto* g = box->Add(std::make_unique<Grid>());
            g->SetColumnSpacing(6.0f);
            g->AddRow(GridLength::Auto());
            g->AddColumn(GridLength::Auto());          // 宽度 = 本列最宽子元素
            g->AddColumn(GridLength::Pixels(120.0f));  // 宽度恒为 120
            g->AddColumn(GridLength::Star(1.0f));      // 宽度 = 剩下的全部
            Border* a = AddCell(g, L"Auto", 0x107C10);
            g->SetCell(a, 0, 0);
            Border* p = AddCell(g, L"Pixels 120", 0xF7630C);
            g->SetCell(p, 0, 1);
            Border* s = AddCell(g, L"Star 1*", 0x0078D4);
            g->SetCell(s, 0, 2);
        }
        AddCaption(sec1, L"Auto 用于标签列（宽度由最长的一条文字决定，换语言自动跟着变）；"
                         L"Pixels 用于图标列、数值列这种「必须对齐、不许抖」的位置；"
                         L"Star 用于真正的内容区。");

        auto* sec2 = AddSubSection(card, L"Star 的权重是比例，不是尺寸");
        int swatchCount = 0;
        const AccentSwatch* sw = AccentSwatches(swatchCount);
        auto* box = AddBoundedBox(sec2, 560.0f, L"Star(1) : Star(2) : Star(3)");
        auto* ratio = box->Add(std::make_unique<Grid>());
        ratio->SetColumnSpacing(6.0f);
        ratio->AddRow(GridLength::Auto());
        for (float weight : {1.0f, 2.0f, 3.0f}) ratio->AddColumn(GridLength::Star(weight));
        const wchar_t* ratioLabels[] = {L"1*", L"2*", L"3*"};
        for (int i = 0; i < 3; ++i) {
            Border* cell = AddCell(ratio, ratioLabels[i], sw[i].color);
            ratio->SetCell(cell, 0, i);
        }
        AddCaption(sec2, L"权重先相加再分配，所以 1:2:3 和 10:20:30 完全等价；"
                         L"混用 Auto/Pixels 时，Star 分的是「减掉它们之后」的剩余。");

        auto* sec3 = AddSubSection(card, L"Auto 行 + Star 行需要有界的高度");
        AddNote(sec3, L"页面外层是 ScrollPanel，它用无穷高度测量子元素 —— 无穷高度下 Star 行"
                      L"没有「剩余空间」可分。所以想用 Star 行，得先有一个高度确定的容器。");
        auto* stage = AddStage(sec3, 150.0f);
        auto* rows = stage->SetChild(std::make_unique<Grid>());
        rows->SetRowSpacing(6.0f);
        rows->AddRow(GridLength::Auto());       // 标题：文字多高，行就多高
        rows->AddRow(GridLength::Star(1.0f));   // 内容：吃掉剩下的
        rows->AddRow(GridLength::Auto());       // 状态栏
        rows->AddColumn(GridLength::Star(1.0f));
        Border* head = AddCell(rows, L"Auto 行：标题", 0x8764B8);
        rows->SetCell(head, 0, 0);
        Border* body = AddCell(rows, L"Star 行：内容区，吃掉剩余高度", 0x0078D4);
        rows->SetCell(body, 1, 0);
        Border* foot = AddCell(rows, L"Auto 行：状态栏", 0x8764B8);
        rows->SetCell(foot, 2, 0);
    }

    // --- 3. 嵌套与组合 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"嵌套与组合：一个应用窗口的骨架");
        AddNote(card, L"外层 Grid 定「工具栏 / 主体 / 状态栏」三行，主体那一行再嵌一个"
                      L"两列 Grid 做「侧栏 + 内容」。侧栏用 Pixels 是因为它的宽度是设计决定，"
                      L"不该随窗口变；内容区用 Star 才能吃掉剩余。");

        auto* stage = AddStage(card, 260.0f);
        auto* shell = stage->SetChild(std::make_unique<Grid>());
        shell->SetRowSpacing(6.0f);
        shell->AddRow(GridLength::Auto());      // 工具栏：按钮多高就多高
        shell->AddRow(GridLength::Star(1.0f));  // 主体
        shell->AddRow(GridLength::Auto());      // 状态栏
        shell->AddColumn(GridLength::Star(1.0f));

        // 工具栏：横向 StackPanel 而不是 Grid —— 按钮宽度各不相同，且不需要对齐成列。
        auto* toolbarFrame = shell->Add(std::make_unique<Border>());
        shell->SetCell(toolbarFrame, 0, 0);
        toolbarFrame->SetBorderThickness(1.0f);
        toolbarFrame->SetCornerRadius(4.0f);
        toolbarFrame->SetPadding(Thickness{6.0f});
        auto* toolbar = toolbarFrame->SetChild(std::make_unique<StackPanel>());
        toolbar->SetOrientation(StackPanel::Orientation::Horizontal);
        toolbar->SetSpacing(6.0f);
        for (const wchar_t* label : {L"新建", L"打开", L"保存"}) {
            auto* b = toolbar->Add(std::make_unique<Button>());
            b->SetText(label);
            b->SetKind(Button::Kind::Subtle);
        }

        auto* mid = shell->Add(std::make_unique<Grid>());
        shell->SetCell(mid, 1, 0);
        mid->SetColumnSpacing(6.0f);
        mid->AddRow(GridLength::Star(1.0f));
        mid->AddColumn(GridLength::Pixels(140.0f));
        mid->AddColumn(GridLength::Star(1.0f));

        auto* sideFrame = mid->Add(std::make_unique<Border>());
        mid->SetCell(sideFrame, 0, 0);
        sideFrame->SetBorderThickness(1.0f);
        sideFrame->SetCornerRadius(4.0f);
        sideFrame->SetPadding(Thickness{6.0f});
        auto* side = sideFrame->SetChild(std::make_unique<StackPanel>());
        side->SetSpacing(4.0f);
        side->SetVAlign(VAlign::Top);  // 否则 StackPanel 会被拉满，条目挤在中间看不出是列表
        for (const wchar_t* label : {L"概览", L"设备", L"日志", L"设置"}) {
            auto* t = side->Add(std::make_unique<TextBlock>());
            t->SetText(label);
            t->SetFontSize(13.0f);
        }

        Border* body = AddCell(mid, L"内容区：Star 列 + Star 行，随窗口一起变", 0x0078D4);
        mid->SetCell(body, 0, 1);

        Border* status = AddCell(shell, L"状态栏：就绪", 0x107C10);
        shell->SetCell(status, 2, 0);
    }

    // --- 4. 常见陷阱 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"常见陷阱");

        auto* sec1 = AddSubSection(card, L"想等分宽度？UniformGrid 做不到，要用 Star 列");
        AddNote(sec1, L"UniformGrid 把每个格子定成「最大子元素的 desired 尺寸」，并且不会把格子"
                      L"撑满 arrange 宽度 —— 它是「一批等大的、按内容定尺寸的格子」，不是"
                      L"「把可用宽度等分」。两者在窄容器里看起来一样，容器一宽就分道扬镳。");
        auto* pair1 = AddEqualColumns(sec1, 2);
        {
            auto* wrong = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ UniformGrid：格子只有内容那么宽，右边一大片空着");
            auto* ug = wrong->Add(std::make_unique<UniformGrid>());
            ug->SetColumns(3);
            for (const wchar_t* label : {L"甲", L"乙", L"丙"}) {
                Border* cell = AddCell(ug, label, 0xC42B1C);
                cell->SetMargin(Thickness{2.0f});
            }

            auto* right = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ Grid + Star(1) × 3：真正等分可用宽度");
            auto* g = right->Add(std::make_unique<Grid>());
            g->SetColumnSpacing(4.0f);
            g->AddRow(GridLength::Auto());
            for (int i = 0; i < 3; ++i) g->AddColumn(GridLength::Star(1.0f));
            const wchar_t* labels[] = {L"甲", L"乙", L"丙"};
            for (int i = 0; i < 3; ++i) {
                Border* cell = AddCell(g, labels[i], 0x107C10);
                g->SetCell(cell, 0, i);
            }
        }

        auto* sec2 = AddSubSection(card, L"给放文字的行写死高度 = 悄悄切掉文字");
        AddNote(sec2, L"Arrange 会把子元素压进它的槽位，既不溢出也不报警。手写的行高是在猜"
                      L"字体的行度量 —— 换字体、换 DPI、换字号都会切，而 Auto 行永远不会。");
        auto* pair2 = AddEqualColumns(sec2, 2);
        {
            auto* wrong = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ Pixels(22)：字被压扁，下缘的笔画被切掉");
            auto* g = wrong->Add(std::make_unique<Grid>());
            g->AddRow(GridLength::Pixels(22.0f));
            g->AddColumn(GridLength::Star(1.0f));
            Border* cell = AddCell(g, L"实测行高约 34 DIP，写死 22", 0xC42B1C);
            g->SetCell(cell, 0, 0);

            auto* right = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ Auto()：行高由文字自己报，怎么改都不会切");
            auto* g2 = right->Add(std::make_unique<Grid>());
            g2->AddRow(GridLength::Auto());
            g2->AddColumn(GridLength::Star(1.0f));
            Border* cell2 = AddCell(g2, L"实测行高约 34 DIP，行是 Auto", 0x107C10);
            g2->SetCell(cell2, 0, 0);
        }
        AddCaption(sec2, L"注意这不是 Grid 的缺陷：写死尺寸是调用方的正当权利，"
                         L"框架照办而已（和 WPF 一致）。错在演示代码猜了一个数字。");
    }

    CreateCodeExample(content, LR"(auto* g = parent->Add(std::make_unique<Grid>());

// 轨道：Auto 按内容、Pixels 固定、Star 分剩余（权重是比例）
g->AddRow(GridLength::Auto());          // 放文字的行永远用 Auto
g->AddRow(GridLength::Star(1.0f));      // 需要外层有确定的高度
g->AddColumn(GridLength::Pixels(140.0f));
g->AddColumn(GridLength::Star(1.0f));

g->SetColumnSpacing(8.0f);
g->SetRowSpacing(8.0f);

// 先 Add 拿指针，再 SetCell —— 单元格归属记在 Grid 里，不在子元素上
auto* cell = g->Add(std::make_unique<Border>());
g->SetCell(cell, /*row*/ 0, /*col*/ 1);
g->SetCell(banner, 0, 0, /*rowSpan*/ 1, /*colSpan*/ 2);

// 想等分宽度用 Star 列，不要用 UniformGrid：
// 它把格子定成「最大子元素的 desired 尺寸」，不会撑满 arrange 宽度。)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateStackPanelPage() {
    auto [page, content] = CreatePageShell(L"StackPanel");

    // --- 1. 基础用法 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"基础用法");
        AddNote(card, L"StackPanel 只做一件事：沿主轴按每个子元素的 desired 尺寸依次摆放。"
                      L"它刻意没有 Star 语义 —— 「谁来吃掉剩余空间」这件事属于 Grid 的 Star 轨道，"
                      L"不属于 StackPanel。");

        auto* row = AddRow(card, 24.0f);
        {
            auto* col = row->Add(std::make_unique<StackPanel>());
            col->SetSpacing(6.0f);
            AddCaption(col, L"Vertical（默认）");
            auto* v = col->Add(std::make_unique<StackPanel>());
            v->SetSpacing(8.0f);
            v->SetHAlign(HAlign::Left);
            for (const wchar_t* text : {L"第一项", L"第二项", L"第三项"}) {
                auto* b = v->Add(std::make_unique<Button>());
                b->SetText(text);
            }
        }
        {
            auto* col = row->Add(std::make_unique<StackPanel>());
            col->SetSpacing(6.0f);
            AddCaption(col, L"Horizontal");
            auto* h = col->Add(std::make_unique<StackPanel>());
            h->SetOrientation(StackPanel::Orientation::Horizontal);
            h->SetSpacing(8.0f);
            h->SetHAlign(HAlign::Left);
            for (const wchar_t* text : {L"左", L"中", L"右"}) {
                auto* b = h->Add(std::make_unique<Button>());
                b->SetText(text);
            }
        }
    }

    // --- 2. 尺寸模式 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"尺寸模式：Spacing、方向、与非堆叠轴上的对齐");

        auto* sec1 = AddSubSection(card, L"Spacing 与 Margin 是叠加的，不是二选一");
        AddNote(sec1, L"Measure 把 spacing×(n-1) 和每个子元素的 margin 分别累加，所以"
                      L"Spacing(8) 加上子元素上下各 4 的 margin，相邻元素之间是 16。"
                      L"想要「统一间距」就只用 Spacing，别混。");
        for (float spacing : {0.0f, 8.0f, 20.0f}) {
            auto* box = AddBoundedBox(sec1, 420.0f,
                std::wstring(L"Spacing = ") + std::to_wstring(static_cast<int>(spacing)));
            auto* v = box->Add(std::make_unique<StackPanel>());
            v->SetOrientation(StackPanel::Orientation::Horizontal);
            v->SetSpacing(spacing);
            v->SetHAlign(HAlign::Left);
            for (const wchar_t* text : {L"甲", L"乙", L"丙"}) AddCell(v, text, 0x0078D4);
        }

        auto* sec2 = AddSubSection(card, L"非堆叠轴：默认 Stretch，所以子元素被拉满");
        AddNote(sec2, L"主轴上子元素总是拿自己的 desired 尺寸；非堆叠轴上则拿到整条槽位，"
                      L"于是默认的 Stretch 会把它拉满。要「按内容宽」，得在子元素上写"
                      L"HAlign（竖排时）或 VAlign（横排时）。");
        {
            auto* box = AddBoundedBox(sec2, 420.0f, L"竖排：子元素的 HAlign 决定横向表现");
            auto* v = box->Add(std::make_unique<StackPanel>());
            v->SetSpacing(6.0f);
            struct HCase { const wchar_t* label; HAlign align; };
            const HCase cases[] = {
                {L"HAlign::Stretch（默认）—— 拉满", HAlign::Stretch},
                {L"HAlign::Left", HAlign::Left},
                {L"HAlign::Center", HAlign::Center},
                {L"HAlign::Right", HAlign::Right},
            };
            for (const HCase& c : cases) {
                Border* cell = AddCell(v, c.label, 0x107C10);
                cell->SetHAlign(c.align);
            }
        }
        {
            // 横排时非堆叠轴是纵向，所以要给一个确定高度才看得出 VAlign 的差别 ——
            // 不然槽位高度等于最高子元素，四种对齐会挤成一样。
            auto* box = AddBoundedBox(sec2, 420.0f, L"横排：子元素的 VAlign 决定纵向表现");
            auto* stage = AddStage(box, 110.0f);
            auto* h = stage->SetChild(std::make_unique<StackPanel>());
            h->SetOrientation(StackPanel::Orientation::Horizontal);
            h->SetSpacing(8.0f);
            struct VCase { const wchar_t* label; VAlign align; };
            const VCase cases[] = {
                {L"Stretch", VAlign::Stretch},
                {L"Top", VAlign::Top},
                {L"Center", VAlign::Center},
                {L"Bottom", VAlign::Bottom},
            };
            for (const VCase& c : cases) {
                Border* cell = AddCell(h, c.label, 0xF7630C);
                cell->SetVAlign(c.align);
            }
        }

        auto* sec3 = AddSubSection(card, L"主轴上不存在「填满剩余」");
        AddNote(sec3, L"下面两个容器一样宽，横排的三个格子无论怎么设 HAlign 都不会撑满 ——"
                      L"HAlign 在横排时是主轴，而主轴的尺寸只由 desired 决定。真要撑满，"
                      L"换成 Grid 的 Star 列。");
        auto* pair = AddEqualColumns(sec3, 2);
        {
            auto* left = pair->Add(std::make_unique<StackPanel>());
            pair->SetCell(left, 0, 0);
            left->SetSpacing(4.0f);
            left->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(left, L"StackPanel 横排：右侧留白，撑不满");
            auto* h = left->Add(std::make_unique<StackPanel>());
            h->SetOrientation(StackPanel::Orientation::Horizontal);
            h->SetSpacing(4.0f);
            for (const wchar_t* text : {L"甲", L"乙", L"丙"}) AddCell(h, text, 0xC42B1C);

            auto* right = pair->Add(std::make_unique<StackPanel>());
            pair->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"Grid + Star 列：等分并撑满");
            auto* g = right->Add(std::make_unique<Grid>());
            g->SetColumnSpacing(4.0f);
            g->AddRow(GridLength::Auto());
            for (int i = 0; i < 3; ++i) g->AddColumn(GridLength::Star(1.0f));
            const wchar_t* labels[] = {L"甲", L"乙", L"丙"};
            for (int i = 0; i < 3; ++i) {
                Border* cell = AddCell(g, labels[i], 0x107C10);
                g->SetCell(cell, 0, i);
            }
        }
    }

    // --- 3. 嵌套与组合 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"嵌套与组合：设置项列表");
        AddNote(card, L"横排 StackPanel 做一行（图标 + 文字块 + 操作），竖排 StackPanel 把这些行"
                      L"堆起来。行内的文字块本身又是一个竖排 StackPanel —— 标题和副标题的间距"
                      L"归它管，不归外层。");

        auto* box = AddBoundedBox(card, 520.0f, L"");
        auto* list = box->Add(std::make_unique<StackPanel>());
        list->SetSpacing(10.0f);

        struct Item { const wchar_t* title; const wchar_t* detail; const wchar_t* action; };
        const Item items[] = {
            {L"深色主题", L"跟随系统外观设置", L"切换"},
            {L"启动时打开上次的工程", L"下次启动恢复窗口与标签页", L"配置"},
            {L"诊断日志", L"记录布局与渲染的耗时", L"查看"},
        };
        for (const Item& it : items) {
            auto* rowFrame = list->Add(std::make_unique<Border>());
            rowFrame->SetBorderThickness(1.0f);
            rowFrame->SetCornerRadius(4.0f);
            rowFrame->SetPadding(Thickness{10.0f, 8.0f, 10.0f, 8.0f});

            // 一行内部用 Grid：文字块要吃掉中间的剩余宽度，右侧按钮要贴右。
            // 这正是 StackPanel 主轴做不到的那件事。
            auto* rowGrid = rowFrame->SetChild(std::make_unique<Grid>());
            rowGrid->SetColumnSpacing(10.0f);
            rowGrid->AddRow(GridLength::Auto());
            rowGrid->AddColumn(GridLength::Auto());
            rowGrid->AddColumn(GridLength::Star(1.0f));
            rowGrid->AddColumn(GridLength::Auto());

            auto* dot = rowGrid->Add(std::make_unique<Border>());
            rowGrid->SetCell(dot, 0, 0);
            dot->SetWidth(8.0f);
            dot->SetHeight(8.0f);
            dot->SetCornerRadius(4.0f);
            dot->SetBackground(D2D1::ColorF(0x0078D4, 1.0f));
            dot->SetVAlign(VAlign::Center);

            auto* texts = rowGrid->Add(std::make_unique<StackPanel>());
            rowGrid->SetCell(texts, 0, 1);
            texts->SetSpacing(2.0f);
            texts->SetVAlign(VAlign::Center);
            auto* title = texts->Add(std::make_unique<TextBlock>());
            title->SetText(it.title);
            auto* detail = texts->Add(std::make_unique<TextBlock>());
            detail->SetText(it.detail);
            detail->SetFontSize(12.0f);
            detail->SetDimmed(true);

            auto* action = rowGrid->Add(std::make_unique<Button>());
            rowGrid->SetCell(action, 0, 2);
            action->SetText(it.action);
            action->SetKind(Button::Kind::Subtle);
            action->SetVAlign(VAlign::Center);
        }
    }

    // --- 4. 常见陷阱 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"常见陷阱");

        auto* sec1 = AddSubSection(card, L"单子元素容器不认子元素的 Margin");
        AddNote(sec1, L"Border / GroupBox / Viewbox / Expander 都是这样：它们用自己的 Padding"
                      L"给内容留白，子元素的 Margin 在这一层不起作用。这是本框架的约定，"
                      L"不是缺陷 —— 但会让「我明明设了 Margin 为什么贴着边」变成一个坑。");
        auto* pair1 = AddEqualColumns(sec1, 2);
        {
            auto* wrong = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ 在 Border 的子元素上写 Margin(16)：没有效果");
            auto* frame = wrong->Add(std::make_unique<Border>());
            frame->SetBorderThickness(1.0f);
            frame->SetCornerRadius(4.0f);
            auto* inner = frame->SetChild(std::make_unique<TextBlock>());
            inner->SetText(L"我设了 Margin(16)，还是贴着边");
            inner->SetFontSize(12.0f);
            inner->SetMargin(Thickness{16.0f});

            auto* right = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ 在 Border 上写 Padding(16)：留白生效");
            auto* frame2 = right->Add(std::make_unique<Border>());
            frame2->SetBorderThickness(1.0f);
            frame2->SetCornerRadius(4.0f);
            frame2->SetPadding(Thickness{16.0f});
            auto* inner2 = frame2->SetChild(std::make_unique<TextBlock>());
            inner2->SetText(L"容器的 Padding 才是留白的正路");
            inner2->SetFontSize(12.0f);
        }
        AddCaption(sec1, L"想在单子元素容器里保留 Margin 语义，就在中间垫一个 StackPanel —— "
                         L"Panel::ArrangeChild 是认 Margin 的。");

        auto* sec2 = AddSubSection(card, L"给放文字的容器写死高度 = 悄悄切掉文字");
        AddNote(sec2, L"StackPanel 的 Arrange 把子元素压进槽位，既不溢出也不报警。"
                      L"下面两个 StackPanel 内容完全相同，唯一区别是外层容器的高度来源。");
        auto* pair2 = AddEqualColumns(sec2, 2);
        {
            auto* wrong = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ 外层 Grid 行写死 Pixels(40)，三行文字只剩一行半");
            auto* g = wrong->Add(std::make_unique<Grid>());
            g->AddRow(GridLength::Pixels(40.0f));
            g->AddColumn(GridLength::Star(1.0f));
            auto* clipped = g->Add(std::make_unique<StackPanel>());
            g->SetCell(clipped, 0, 0);
            clipped->SetSpacing(2.0f);
            for (const wchar_t* t : {L"第一行文字", L"第二行文字", L"第三行文字"}) {
                auto* tb = clipped->Add(std::make_unique<TextBlock>());
                tb->SetText(t);
                tb->SetFontSize(12.0f);
            }

            auto* right = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ 外层 Grid 行用 Auto()，三行都在");
            auto* g2 = right->Add(std::make_unique<Grid>());
            g2->AddRow(GridLength::Auto());
            g2->AddColumn(GridLength::Star(1.0f));
            auto* ok = g2->Add(std::make_unique<StackPanel>());
            g2->SetCell(ok, 0, 0);
            ok->SetSpacing(2.0f);
            for (const wchar_t* t : {L"第一行文字", L"第二行文字", L"第三行文字"}) {
                auto* tb = ok->Add(std::make_unique<TextBlock>());
                tb->SetText(t);
                tb->SetFontSize(12.0f);
            }
        }
    }

    CreateCodeExample(content, LR"(auto* panel = parent->Add(std::make_unique<StackPanel>());
panel->SetOrientation(StackPanel::Orientation::Horizontal);  // 默认 Vertical
panel->SetSpacing(8.0f);   // 与子元素的 Margin 叠加，不是二选一

// 主轴：子元素总是拿自己的 desired 尺寸，没有「填满剩余」这回事。
// 非堆叠轴：子元素拿到整条槽位，默认 Stretch 会被拉满 —— 想按内容宽就写对齐：
child->SetHAlign(HAlign::Left);   // 竖排时管横向
child->SetVAlign(VAlign::Center); // 横排时管纵向

// 需要「一列吃掉剩余宽度」时换 Grid：
// g->AddColumn(GridLength::Auto()); g->AddColumn(GridLength::Star(1.0f));

// 单子元素容器（Border/GroupBox/Viewbox/Expander）不认子元素的 Margin，
// 留白要写在容器的 Padding 上。)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateWrapPanelPage() {
    auto [page, content] = CreatePageShell(L"WrapPanel");

    // --- 1. 基础用法 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"基础用法");
        AddNote(card, L"WrapPanel 沿主轴累加子元素的占位，累加超过可用宽度就换行。"
                      L"每一行的高度等于该行最高的子元素，所以行与行的高度可以不同。");

        auto* box = AddBoundedBox(card, 520.0f, L"自然尺寸：每个标签宽度不同，按需换行");
        auto* wrap = box->Add(std::make_unique<WrapPanel>());
        const wchar_t* tags[] = {
            L"布局", L"Direct2D", L"DirectWrite", L"虚拟化", L"主题", L"动画",
            L"命中测试", L"合成", L"DPI 缩放", L"焦点环",
        };
        for (const wchar_t* tag : tags) {
            Border* cell = AddCell(wrap, tag, 0x0078D4);
            cell->SetMargin(Thickness{3.0f});  // WrapPanel 没有 spacing 属性，间隙靠 Margin
        }
        AddCaption(card, L"注意 WrapPanel 没有 Spacing 属性（和 UniformGrid 一样），"
                         L"格与格之间的空隙来自子元素的 Margin，而且 Margin 计入换行判断。");
    }

    // --- 2. 尺寸模式 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"尺寸模式：换行点由容器宽度决定");
        AddNote(card, L"下面三个盒子里是完全相同的十二个格子，只有容器宽度不同 —— 换行位置"
                      L"随之改变。Arrange 会按实际 arrange 出来的宽度重算换行，而不是沿用"
                      L"Measure 时的结果，所以窗口一变宽，换行立刻跟着变。");

        for (float w : {520.0f, 340.0f, 200.0f}) {
            auto* box = AddBoundedBox(card, w,
                std::wstring(L"容器 ") + std::to_wstring(static_cast<int>(w)) + L" DIP");
            auto* wrap = box->Add(std::make_unique<WrapPanel>());
            for (int i = 1; i <= 12; ++i) {
                Border* cell = AddCell(wrap, std::to_wstring(i), 0x107C10);
                cell->SetWidth(52.0f);   // 定宽格子，这样换行数量的变化只由容器宽度解释
                cell->SetMargin(Thickness{3.0f});
            }
        }

        auto* sec1 = AddSubSection(card, L"ItemWidth / ItemHeight：把格子强行统一");
        AddNote(sec1, L"设了 ItemWidth/ItemHeight 之后，子元素在该轴上的 desired 尺寸被无视，"
                      L"一律按给定值占位 —— 这是做磁贴网格的办法。只设一个轴也可以，"
                      L"另一个轴仍按内容。");
        {
            auto* box = AddBoundedBox(sec1, 520.0f, L"不设：宽度各不相同");
            auto* wrap = box->Add(std::make_unique<WrapPanel>());
            for (const wchar_t* t : {L"短", L"中等长度", L"很长很长的一条文字", L"短"}) {
                Border* cell = AddCell(wrap, t, 0xF7630C);
                cell->SetMargin(Thickness{3.0f});
            }
        }
        {
            auto* box = AddBoundedBox(sec1, 520.0f, L"ItemWidth = 120, ItemHeight = 44：整齐磁贴");
            auto* wrap = box->Add(std::make_unique<WrapPanel>());
            wrap->SetItemWidth(120.0f);
            wrap->SetItemHeight(44.0f);
            // 这里不给 Margin：ItemWidth/ItemHeight 已经决定了占位，再叠 Margin
            // 会让「统一格子」重新变得不统一（Margin 计入占位）。
            for (const wchar_t* t : {L"短", L"中等长度", L"很长很长的一条文字", L"短"})
                AddCell(wrap, t, 0x8764B8);
        }

        auto* sec2 = AddSubSection(card, L"Vertical：先填满一列再换列");
        AddNote(sec2, L"竖向 WrapPanel 的换行阈值是高度而不是宽度，所以必须有一个确定的高度 ——"
                      L"页面外层的 ScrollPanel 给的是无穷高度，无穷高度下永远不换列。");
        auto* stage = AddStage(sec2, 150.0f);
        auto* vwrap = stage->SetChild(std::make_unique<WrapPanel>());
        vwrap->SetOrientation(WrapPanel::Orientation::Vertical);
        for (int i = 1; i <= 10; ++i) {
            Border* cell = AddCell(vwrap, L"项 " + std::to_wstring(i), 0x0078D4);
            cell->SetMargin(Thickness{3.0f});
        }
    }

    // --- 3. 嵌套与组合 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"嵌套与组合：可换行的筛选条 + 结果网格");
        AddNote(card, L"顶部是一条会换行的筛选标签（数量不定，宽度不定，正好是 WrapPanel 的场景），"
                      L"下面是定宽磁贴的结果区。两段都放在一个 Auto 行的 Grid 里，"
                      L"于是各自按内容占高度。");

        auto* box = AddBoundedBox(card, 560.0f, L"");
        auto* shell = box->Add(std::make_unique<Grid>());
        shell->SetRowSpacing(10.0f);
        shell->AddRow(GridLength::Auto());  // 筛选条：换几行就多高
        shell->AddRow(GridLength::Auto());  // 结果区：同理
        shell->AddColumn(GridLength::Star(1.0f));

        auto* filterBox = shell->Add(std::make_unique<GroupBox>());
        shell->SetCell(filterBox, 0, 0);
        filterBox->SetHeader(L"已选筛选条件");
        auto* filters = filterBox->SetChild(std::make_unique<WrapPanel>());
        const wchar_t* conditions[] = {
            L"类型：控件", L"状态：已实现", L"平台：Win32", L"渲染：Direct2D",
            L"分类：布局容器", L"含测试", L"含性能门禁",
        };
        for (const wchar_t* c : conditions) {
            Border* chip = AddCell(filters, c, 0x0078D4);
            chip->SetCornerRadius(10.0f);
            chip->SetMargin(Thickness{3.0f});
        }

        auto* resultBox = shell->Add(std::make_unique<GroupBox>());
        shell->SetCell(resultBox, 1, 0);
        resultBox->SetHeader(L"结果（定宽磁贴）");
        auto* results = resultBox->SetChild(std::make_unique<WrapPanel>());
        results->SetItemWidth(112.0f);
        results->SetItemHeight(56.0f);
        const wchar_t* names[] = {
            L"Grid", L"StackPanel", L"WrapPanel", L"DockPanel",
            L"UniformGrid", L"Canvas", L"Border", L"Viewbox",
        };
        for (const wchar_t* n : names) {
            auto* tile = results->Add(std::make_unique<Border>());
            tile->SetBorderThickness(1.0f);
            tile->SetCornerRadius(4.0f);
            tile->SetPadding(Thickness{8.0f});
            tile->SetBackground(D2D1::ColorF(0x107C10, 0.14f));
            auto* label = tile->SetChild(std::make_unique<TextBlock>());
            label->SetText(n);
            label->SetFontSize(12.0f);
            label->SetVAlign(VAlign::Center);
        }
    }

    // --- 4. 常见陷阱 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"常见陷阱");

        auto* sec1 = AddSubSection(card, L"想「一行等分 N 个」不要用 WrapPanel");
        AddNote(sec1, L"WrapPanel 只管换行，不管等分：格子有多宽就占多宽，行尾的剩余宽度留空。"
                      L"UniformGrid 也不行（它把格子定成最大子元素的 desired 尺寸，不撑满）。"
                      L"要等分只有 Grid 的 Star 列。");
        auto* pair1 = AddEqualColumns(sec1, 2);
        {
            auto* wrong = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ WrapPanel：宽度按内容，行尾留白");
            auto* wrap = wrong->Add(std::make_unique<WrapPanel>());
            for (const wchar_t* t : {L"短", L"中等", L"长一些的"}) {
                Border* cell = AddCell(wrap, t, 0xC42B1C);
                cell->SetMargin(Thickness{2.0f});
            }

            auto* right = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ Grid + Star(1) × 3：三格等分且撑满");
            auto* g = right->Add(std::make_unique<Grid>());
            g->SetColumnSpacing(4.0f);
            g->AddRow(GridLength::Auto());
            for (int i = 0; i < 3; ++i) g->AddColumn(GridLength::Star(1.0f));
            const wchar_t* labels[] = {L"短", L"中等", L"长一些的"};
            for (int i = 0; i < 3; ++i) {
                Border* cell = AddCell(g, labels[i], 0x107C10);
                g->SetCell(cell, 0, i);
            }
        }

        auto* sec2 = AddSubSection(card, L"给会换行的内容写死高度 = 换行后被切掉");
        AddNote(sec2, L"这是 WrapPanel 上最容易犯的一个：写死高度时按「一行」估的，"
                      L"容器一窄内容换成两行，第二行就没了 —— 而换行本来就是 WrapPanel 存在的理由。");
        auto* pair2 = AddEqualColumns(sec2, 2);
        {
            const wchar_t* chips[] = {L"标签一", L"标签二", L"标签三", L"标签四", L"标签五"};

            auto* wrong = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ 外层行 Pixels(34)：换到第二行的标签被切掉");
            auto* g = wrong->Add(std::make_unique<Grid>());
            g->AddRow(GridLength::Pixels(34.0f));
            g->AddColumn(GridLength::Star(1.0f));
            auto* clipped = g->Add(std::make_unique<WrapPanel>());
            g->SetCell(clipped, 0, 0);
            for (const wchar_t* t : chips) {
                Border* cell = AddCell(clipped, t, 0xC42B1C);
                cell->SetMargin(Thickness{2.0f});
            }

            auto* right = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ 外层行 Auto()：换几行就长几行的高度");
            auto* g2 = right->Add(std::make_unique<Grid>());
            g2->AddRow(GridLength::Auto());
            g2->AddColumn(GridLength::Star(1.0f));
            auto* ok = g2->Add(std::make_unique<WrapPanel>());
            g2->SetCell(ok, 0, 0);
            for (const wchar_t* t : chips) {
                Border* cell = AddCell(ok, t, 0x107C10);
                cell->SetMargin(Thickness{2.0f});
            }
        }
    }

    CreateCodeExample(content, LR"(auto* wrap = parent->Add(std::make_unique<WrapPanel>());

// 换行阈值是容器在主轴上的可用尺寸：Horizontal 看宽度，Vertical 看高度。
// Arrange 会按实际排布宽度重算换行，所以窗口变宽时换行位置立刻跟着变。
wrap->SetOrientation(WrapPanel::Orientation::Vertical);  // 默认 Horizontal

// 统一格子尺寸（磁贴网格）：该轴上子元素的 desired 尺寸被无视
wrap->SetItemWidth(120.0f);
wrap->SetItemHeight(44.0f);
wrap->ClearItemWidth();   // 恢复按内容

// 没有 Spacing 属性（同 UniformGrid）：间隙来自子元素 Margin，且计入换行判断
child->SetMargin(Thickness{3.0f});

// 想「一行等分 N 个」用 Grid 的 Star 列，WrapPanel 和 UniformGrid 都做不到。
// 装 WrapPanel 的行永远用 GridLength::Auto()：换行会让它变高。)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateUniformGridPage() {
    auto [page, content] = CreatePageShell(L"UniformGrid");

    // Auto columns: the cell is the widest/tallest child, and the column count
    // follows from how many cells fit the available width.
    auto* card1 = CreateExampleCard(content, L"Auto columns (cell = largest child)");
    auto* auto1 = card1->Add(std::make_unique<UniformGrid>());
    auto1->SetWidth(520.0f);
    for (int i = 1; i <= 8; ++i) {
        auto* b = auto1->Add(std::make_unique<Button>());
        b->SetText(L"Item " + std::to_wstring(i));
        b->SetMargin(Thickness(4.0f));
    }

    // Explicit column count: wraps every N regardless of available width.
    auto* card2 = CreateExampleCard(content, L"Columns = 3");
    auto* cols3 = card2->Add(std::make_unique<UniformGrid>());
    cols3->SetWidth(520.0f);
    cols3->SetColumns(3);
    for (int i = 1; i <= 7; ++i) {
        auto* b = cols3->Add(std::make_unique<Button>());
        b->SetText(L"Cell " + std::to_wstring(i));
        b->SetMargin(Thickness(4.0f));
    }

    // FirstColumn leaves the leading cells of the first row empty — WPF uses this
    // for calendar layouts, where the 1st of the month is not a Sunday.
    auto* card3 = CreateExampleCard(content, L"Columns = 7, FirstColumn = 3 (calendar-style)");
    auto* cal = card3->Add(std::make_unique<UniformGrid>());
    cal->SetWidth(520.0f);
    cal->SetColumns(7);
    cal->SetFirstColumn(3);
    for (int day = 1; day <= 14; ++day) {
        auto* b = cal->Add(std::make_unique<Button>());
        b->SetText(std::to_wstring(day));
        b->SetMargin(Thickness(2.0f));
    }

    CreateCodeExample(content, LR"(// Auto: cell = largest child, columns fit the width
auto* grid = parent->Add(std::make_unique<UniformGrid>());
grid->Add(std::make_unique<Button>());

// Explicit shape
grid->SetColumns(3);
grid->SetRows(2);        // 0 = derive from child count

// Leave the first N cells of row 0 empty (calendar start-of-month)
grid->SetFirstColumn(3);

// Gaps come from child Margin — UniformGrid has no spacing property,
// matching WPF.
child->SetMargin(Thickness(4.0f));)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateDockPanelPage() {
    auto [page, content] = CreatePageShell(L"DockPanel");

    // --- 1. 基础用法 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"基础用法");
        AddNote(card, L"每个子元素从「当前剩余矩形」的某条边上切走一条，下一个子元素只看到"
                      L"剩下的部分。Dock 是附加属性（存在 DockPanel 的一张静态表里，"
                      L"键是元素指针），所以要先 Add 拿到指针再 SetDock —— 和 Canvas 的 Left/Top 同理。"
                      L"没设过的默认是 Dock::Left。");
        AddNote(card, L"DockPanel 必须放在高度确定的容器里：条的厚度来自子元素的 desired 尺寸，"
                      L"而中间那块「剩余」需要有一个有限的高度可减。");

        auto* stage = AddStage(card, 230.0f);
        auto* dock = stage->SetChild(std::make_unique<DockPanel>());
        AddDocked(dock, Dock::Top, L"Dock::Top —— 先切，所以横跨整个宽度", 0x8764B8);
        AddDocked(dock, Dock::Bottom, L"Dock::Bottom", 0x8764B8);
        AddDocked(dock, Dock::Left, L"Dock::Left", 0xF7630C);
        AddDocked(dock, Dock::Right, L"Dock::Right", 0xF7630C);
        AddCell(dock, L"最后一个子元素：忽略自己的 Dock，填满剩余", 0x0078D4);
    }

    // --- 2. 尺寸模式 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"尺寸模式：停靠顺序与 LastChildFill");

        auto* sec1 = AddSubSection(card, L"顺序决定谁横跨到底 —— 这不是缺陷，是全部意义");
        AddNote(sec1, L"下面两组的子元素完全一样，只是 Add 的先后不同。先 Add 的那个从完整矩形上"
                      L"切走一条，所以它是横跨的那一个；后 Add 的只能在剩余矩形里切。"
                      L"和 WPF 完全一致，调用方是靠这个行为吃饭的。");
        auto* pair1 = AddEqualColumns(sec1, 2);
        {
            auto* leftCol = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(leftCol, 0, 0);
            leftCol->SetSpacing(4.0f);
            leftCol->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(leftCol, L"先 Top 后 Left：Top 横跨整个宽度");
            auto* stage = AddStage(leftCol, 150.0f);
            auto* dock = stage->SetChild(std::make_unique<DockPanel>());
            AddDocked(dock, Dock::Top, L"Top（先）", 0x8764B8);
            AddDocked(dock, Dock::Left, L"Left（后）", 0xF7630C);
            AddCell(dock, L"填充", 0x0078D4);

            auto* rightCol = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(rightCol, 0, 1);
            rightCol->SetSpacing(4.0f);
            AddCaption(rightCol, L"先 Left 后 Top：Left 纵贯整个高度");
            auto* stage2 = AddStage(rightCol, 150.0f);
            auto* dock2 = stage2->SetChild(std::make_unique<DockPanel>());
            AddDocked(dock2, Dock::Left, L"Left（先）", 0xF7630C);
            AddDocked(dock2, Dock::Top, L"Top（后）", 0x8764B8);
            AddCell(dock2, L"填充", 0x0078D4);
        }

        auto* sec2 = AddSubSection(card, L"LastChildFill：最后一个子元素填满，还是照自己的边停靠");
        AddNote(sec2, L"默认 true：最后一个子元素的 Dock 被忽略，直接吃掉剩余矩形 —— 这就是"
                      L"「上工具栏 + 下状态栏 + 中间内容」这个形状能一句话写出来的原因。"
                      L"关掉之后每个子元素都各自贴边，中间留空。");
        auto* pair2 = AddEqualColumns(sec2, 2);
        {
            auto* leftCol = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(leftCol, 0, 0);
            leftCol->SetSpacing(4.0f);
            leftCol->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(leftCol, L"LastChildFill = true（默认）");
            auto* stage = AddStage(leftCol, 150.0f);
            auto* dock = stage->SetChild(std::make_unique<DockPanel>());
            AddDocked(dock, Dock::Top, L"Top", 0x8764B8);
            AddDocked(dock, Dock::Bottom, L"Bottom", 0x8764B8);
            AddDocked(dock, Dock::Left, L"最后一个：Dock 被忽略", 0x0078D4);

            auto* rightCol = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(rightCol, 0, 1);
            rightCol->SetSpacing(4.0f);
            AddCaption(rightCol, L"LastChildFill = false：中间空着");
            auto* stage2 = AddStage(rightCol, 150.0f);
            auto* dock2 = stage2->SetChild(std::make_unique<DockPanel>());
            dock2->SetLastChildFill(false);
            AddDocked(dock2, Dock::Top, L"Top", 0x8764B8);
            AddDocked(dock2, Dock::Bottom, L"Bottom", 0x8764B8);
            AddDocked(dock2, Dock::Left, L"最后一个：真的贴左", 0x0078D4);
        }

        auto* sec3 = AddSubSection(card, L"条的厚度来自子元素自己");
        AddNote(sec3, L"Top/Bottom 切走的是子元素的 desired 高度，Left/Right 切走的是 desired 宽度 ——"
                      L"另一个方向上它拿到整条边。所以给 Left 条写 Width 是正当的（侧栏宽度是设计决定），"
                      L"给 Top 条写 Height 就要小心：那是在猜文字的行高。");
        auto* stage = AddStage(sec3, 170.0f);
        auto* dock = stage->SetChild(std::make_unique<DockPanel>());
        Border* top = AddDocked(dock, Dock::Top, L"Top：高度由文字定（没写 Height）", 0x8764B8);
        Border* side = AddDocked(dock, Dock::Left, L"Left：Width = 130", 0xF7630C);
        side->SetWidth(130.0f);
        AddCell(dock, L"填充：剩下的全归它", 0x0078D4);
        (void)top;  // 只为强调「刻意没有 SetHeight」这个对照
    }

    // --- 3. 嵌套与组合 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"嵌套与组合：工具栏 + 侧栏 + 内容 + 状态栏");
        AddNote(card, L"典型的应用窗口外壳：外层 DockPanel 依次切「顶部工具栏 → 底部状态栏 →"
                      L"左侧导航」，剩下的交给内容区。顺序很讲究 —— 先切顶和底，工具栏和状态栏"
                      L"才会横贯整宽；如果先切左侧，侧栏会从顶一直通到底。");

        auto* stage = AddStage(card, 300.0f);
        auto* shell = stage->SetChild(std::make_unique<DockPanel>());

        // 顶部工具栏：内部再套横向 StackPanel。厚度由按钮的 desired 高度决定。
        auto* toolbarFrame = shell->Add(std::make_unique<Border>());
        DockPanel::SetDock(toolbarFrame, Dock::Top);
        toolbarFrame->SetBorderThickness(1.0f);
        toolbarFrame->SetCornerRadius(4.0f);
        toolbarFrame->SetPadding(Thickness{6.0f});
        auto* toolbar = toolbarFrame->SetChild(std::make_unique<StackPanel>());
        toolbar->SetOrientation(StackPanel::Orientation::Horizontal);
        toolbar->SetSpacing(6.0f);
        for (const wchar_t* label : {L"新建", L"打开", L"保存", L"运行"}) {
            auto* b = toolbar->Add(std::make_unique<Button>());
            b->SetText(label);
            b->SetKind(Button::Kind::Subtle);
        }

        // 状态栏：内部用 Grid 而不是 StackPanel —— 右侧那段要贴右，
        // 这需要一个 Star 列把中间的剩余宽度吃掉。
        auto* statusFrame = shell->Add(std::make_unique<Border>());
        DockPanel::SetDock(statusFrame, Dock::Bottom);
        statusFrame->SetBorderThickness(1.0f);
        statusFrame->SetCornerRadius(4.0f);
        statusFrame->SetPadding(Thickness{8.0f, 4.0f, 8.0f, 4.0f});
        auto* status = statusFrame->SetChild(std::make_unique<Grid>());
        status->AddRow(GridLength::Auto());
        status->AddColumn(GridLength::Auto());
        status->AddColumn(GridLength::Star(1.0f));
        status->AddColumn(GridLength::Auto());
        auto* statusLeft = status->Add(std::make_unique<TextBlock>());
        status->SetCell(statusLeft, 0, 0);
        statusLeft->SetText(L"就绪");
        statusLeft->SetFontSize(12.0f);
        auto* statusRight = status->Add(std::make_unique<TextBlock>());
        status->SetCell(statusRight, 0, 2);
        statusRight->SetText(L"行 1，列 1    UTF-8");
        statusRight->SetFontSize(12.0f);
        statusRight->SetAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

        // 左侧导航：宽度是设计决定，所以写死 Width 是正当的（不是在猜字体度量）。
        auto* navFrame = shell->Add(std::make_unique<Border>());
        DockPanel::SetDock(navFrame, Dock::Left);
        navFrame->SetWidth(150.0f);
        navFrame->SetBorderThickness(1.0f);
        navFrame->SetCornerRadius(4.0f);
        navFrame->SetPadding(Thickness{8.0f});
        auto* nav = navFrame->SetChild(std::make_unique<StackPanel>());
        nav->SetSpacing(6.0f);
        nav->SetVAlign(VAlign::Top);  // 不然会被拉满，条目垂直居中，看不出是列表
        for (const wchar_t* label : {L"项目", L"搜索", L"源代码管理", L"运行与调试", L"扩展"}) {
            auto* t = nav->Add(std::make_unique<TextBlock>());
            t->SetText(label);
            t->SetFontSize(13.0f);
        }

        // 内容区（最后一个）：内部再嵌一个 DockPanel，切出一条标签栏。
        // 嵌套是 DockPanel 唯一的「多区域」手段 —— 它没有行列概念。
        auto* contentFrame = shell->Add(std::make_unique<Border>());
        contentFrame->SetBorderThickness(1.0f);
        contentFrame->SetCornerRadius(4.0f);
        contentFrame->SetPadding(Thickness{6.0f});
        auto* inner = contentFrame->SetChild(std::make_unique<DockPanel>());
        auto* tabsFrame = inner->Add(std::make_unique<Border>());
        DockPanel::SetDock(tabsFrame, Dock::Top);
        tabsFrame->SetPadding(Thickness{0, 0, 0, 6.0f});
        auto* tabs = tabsFrame->SetChild(std::make_unique<StackPanel>());
        tabs->SetOrientation(StackPanel::Orientation::Horizontal);
        tabs->SetSpacing(4.0f);
        for (const wchar_t* label : {L"Grid.cpp", L"DockPanel.cpp"}) {
            Border* tab = AddCell(tabs, label, 0x0078D4);
            tab->SetCornerRadius(4.0f);
        }
        AddCell(inner, L"编辑区：内层 DockPanel 的最后一个子元素，吃掉剩余", 0x107C10);
    }

    // --- 4. 常见陷阱 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"常见陷阱");

        auto* sec1 = AddSubSection(card, L"给放文字的停靠条写死高度 = 悄悄切掉文字");
        AddNote(sec1, L"Arrange 把子元素压进它切出的那一条，既不溢出也不报警。工具栏、状态栏的"
                      L"高度看起来「就那么高」，但那个数字是字体的行度量 —— 换字体、换 DPI、"
                      L"换字号都会变。不写 Height，让条的厚度来自 desired 尺寸。");
        auto* pair1 = AddEqualColumns(sec1, 2);
        {
            auto* wrong = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ Top 条 SetHeight(18)：文字下缘被切");
            auto* stage = AddStage(wrong, 140.0f);
            auto* dock = stage->SetChild(std::make_unique<DockPanel>());
            Border* bar = AddDocked(dock, Dock::Top, L"写死 18 的工具栏", 0xC42B1C);
            bar->SetHeight(18.0f);
            AddCell(dock, L"填充", 0x0078D4);

            auto* right = pair1->Add(std::make_unique<StackPanel>());
            pair1->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ 不写 Height：厚度来自 desired 尺寸");
            auto* stage2 = AddStage(right, 140.0f);
            auto* dock2 = stage2->SetChild(std::make_unique<DockPanel>());
            AddDocked(dock2, Dock::Top, L"按内容定高的工具栏", 0x107C10);
            AddCell(dock2, L"填充", 0x0078D4);
        }

        auto* sec2 = AddSubSection(card, L"忘了顺序：以为「设了 Dock 就该在那个位置」");
        AddNote(sec2, L"Dock 只说「贴哪条边」，不说「占多大范围」—— 范围由 Add 的先后决定。"
                      L"想让侧栏在工具栏下方，就必须在工具栏之后 Add；反过来侧栏会顶到最上面，"
                      L"工具栏被挤成半宽。");
        auto* pair2 = AddEqualColumns(sec2, 2);
        {
            auto* wrong = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ 先 Add 侧栏：工具栏只剩右半边");
            auto* stage = AddStage(wrong, 150.0f);
            auto* dock = stage->SetChild(std::make_unique<DockPanel>());
            Border* side = AddDocked(dock, Dock::Left, L"侧栏", 0xC42B1C);
            side->SetWidth(90.0f);
            AddDocked(dock, Dock::Top, L"工具栏", 0xF7630C);
            AddCell(dock, L"内容", 0x0078D4);

            auto* right = pair2->Add(std::make_unique<StackPanel>());
            pair2->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ 先 Add 工具栏：横贯整宽，侧栏在其下");
            auto* stage2 = AddStage(right, 150.0f);
            auto* dock2 = stage2->SetChild(std::make_unique<DockPanel>());
            AddDocked(dock2, Dock::Top, L"工具栏", 0x107C10);
            Border* side2 = AddDocked(dock2, Dock::Left, L"侧栏", 0xF7630C);
            side2->SetWidth(90.0f);
            AddCell(dock2, L"内容", 0x0078D4);
        }

        auto* sec3 = AddSubSection(card, L"单子元素容器不认子元素的 Margin");
        AddNote(sec3, L"停靠条通常是个 Border，往里塞内容时会想用 Margin 留白 —— 不生效。"
                      L"Border / GroupBox / Viewbox / Expander 都只认自己的 Padding，"
                      L"这是本框架的约定。DockPanel 本身（Panel::ArrangeChild）是认 Margin 的，"
                      L"所以「条与条之间」用 Margin 反而没问题。");
        auto* pair3 = AddEqualColumns(sec3, 2);
        {
            auto* wrong = pair3->Add(std::make_unique<StackPanel>());
            pair3->SetCell(wrong, 0, 0);
            wrong->SetSpacing(4.0f);
            wrong->SetMargin(Thickness{0, 0, 8.0f, 0});
            AddCaption(wrong, L"✗ 条内文字写 Margin(12)：贴着边");
            auto* stage = AddStage(wrong, 110.0f);
            auto* dock = stage->SetChild(std::make_unique<DockPanel>());
            auto* bar = dock->Add(std::make_unique<Border>());
            DockPanel::SetDock(bar, Dock::Top);
            bar->SetBorderThickness(1.0f);
            bar->SetCornerRadius(4.0f);
            bar->SetBackground(D2D1::ColorF(0xC42B1C, 0.18f));
            auto* barText = bar->SetChild(std::make_unique<TextBlock>());
            barText->SetText(L"我设了 Margin(12)");
            barText->SetFontSize(12.0f);
            barText->SetMargin(Thickness{12.0f});
            AddCell(dock, L"填充", 0x0078D4);

            auto* right = pair3->Add(std::make_unique<StackPanel>());
            pair3->SetCell(right, 0, 1);
            right->SetSpacing(4.0f);
            AddCaption(right, L"✓ 条上写 Padding(12)：留白生效");
            auto* stage2 = AddStage(right, 110.0f);
            auto* dock2 = stage2->SetChild(std::make_unique<DockPanel>());
            auto* bar2 = dock2->Add(std::make_unique<Border>());
            DockPanel::SetDock(bar2, Dock::Top);
            bar2->SetBorderThickness(1.0f);
            bar2->SetCornerRadius(4.0f);
            bar2->SetPadding(Thickness{12.0f});
            bar2->SetBackground(D2D1::ColorF(0x107C10, 0.18f));
            auto* barText2 = bar2->SetChild(std::make_unique<TextBlock>());
            barText2->SetText(L"容器的 Padding(12)");
            barText2->SetFontSize(12.0f);
            AddCell(dock2, L"填充", 0x0078D4);
        }
    }

    CreateCodeExample(content, LR"(auto* dock = parent->Add(std::make_unique<DockPanel>());

// Dock 是附加属性（静态表，键是元素指针）—— 先 Add 拿指针，再 SetDock。
// 默认 Dock::Left。
auto* toolbar = dock->Add(std::make_unique<Border>());
DockPanel::SetDock(toolbar, Dock::Top);      // 先 Add 的先切，所以横贯整宽

auto* statusBar = dock->Add(std::make_unique<Border>());
DockPanel::SetDock(statusBar, Dock::Bottom);

auto* nav = dock->Add(std::make_unique<Border>());
DockPanel::SetDock(nav, Dock::Left);
nav->SetWidth(150.0f);   // 侧栏宽度是设计决定，写死是正当的

// 最后一个子元素默认忽略自己的 Dock，填满剩余矩形
dock->Add(std::make_unique<Border>());
dock->SetLastChildFill(false);   // 关掉：每个都贴边，中间留空

// 条的厚度 = 子元素的 desired 尺寸。放文字的条不要写 Height ——
// 那是在猜字体行度量，Arrange 会把文字压进去并切掉下缘。
// DockPanel 没有行列概念，多区域靠嵌套 DockPanel / Grid。)");
    return std::move(page);
}


std::unique_ptr<ScrollPanel> GalleryApp::CreateCanvasPage() {
    auto [page, content] = CreatePageShell(L"Canvas");

    // Example 1: Absolute positioning with Left/Top
    auto* card1 = CreateExampleCard(content, L"Absolute positioning");
    auto* canvasContainer = card1->Add(std::make_unique<Border>());
    canvasContainer->SetWidth(520.0f); canvasContainer->SetHeight(240.0f);
    canvasContainer->SetBorderThickness(1.0f); canvasContainer->SetCornerRadius(6.0f);
    auto* canvas = canvasContainer->SetChild(std::make_unique<Canvas>());

    for (int i = 0; i < 5; ++i) {
        auto* btn = canvas->Add(std::make_unique<Button>());
        btn->SetText(L"Item " + std::to_wstring(i + 1));
        Canvas::SetLeft(btn, 20.0f + i * 80.0f);
        Canvas::SetTop(btn, 30.0f + i * 35.0f);
    }

    CreateCodeExample(content, LR"(auto* canvas = panel->Add(std::make_unique<Canvas>());
auto* btn = canvas->Add(std::make_unique<Button>());
Canvas::SetLeft(btn, 100.0f);
Canvas::SetTop(btn, 50.0f);)");

    // Example 2: Right/Bottom edge positioning
    auto* card2 = CreateExampleCard(content, L"Edge positioning (Right/Bottom)");
    auto* canvasContainer2 = card2->Add(std::make_unique<Border>());
    canvasContainer2->SetWidth(520.0f); canvasContainer2->SetHeight(180.0f);
    canvasContainer2->SetBorderThickness(1.0f); canvasContainer2->SetCornerRadius(6.0f);
    auto* canvas2 = canvasContainer2->SetChild(std::make_unique<Canvas>());

    auto* topLeft = canvas2->Add(std::make_unique<Button>());
    topLeft->SetText(L"Top-Left");
    Canvas::SetLeft(topLeft, 8.0f);
    Canvas::SetTop(topLeft, 8.0f);

    auto* topRight = canvas2->Add(std::make_unique<Button>());
    topRight->SetText(L"Top-Right");
    Canvas::SetRight(topRight, 8.0f);
    Canvas::SetTop(topRight, 8.0f);

    auto* bottomLeft = canvas2->Add(std::make_unique<Button>());
    bottomLeft->SetText(L"Bottom-Left");
    Canvas::SetLeft(bottomLeft, 8.0f);
    Canvas::SetBottom(bottomLeft, 8.0f);

    auto* bottomRight = canvas2->Add(std::make_unique<Button>());
    bottomRight->SetText(L"Bottom-Right");
    Canvas::SetRight(bottomRight, 8.0f);
    Canvas::SetBottom(bottomRight, 8.0f);

    CreateCodeExample(content, LR"(Canvas::SetRight(btn, 8.0f);   // position from right edge
Canvas::SetBottom(btn, 8.0f);  // position from bottom edge)");

    // Example 3: ZIndex layering
    auto* card3 = CreateExampleCard(content, L"ZIndex layering");
    auto* canvasContainer3 = card3->Add(std::make_unique<Border>());
    canvasContainer3->SetWidth(520.0f); canvasContainer3->SetHeight(200.0f);
    canvasContainer3->SetBorderThickness(1.0f); canvasContainer3->SetCornerRadius(6.0f);
    auto* canvas3 = canvasContainer3->SetChild(std::make_unique<Canvas>());

    // Three overlapping borders with different ZIndex values.
    auto* layer0 = canvas3->Add(std::make_unique<Border>());
    layer0->SetWidth(160.0f); layer0->SetHeight(120.0f);
    layer0->SetBorderThickness(2.0f); layer0->SetCornerRadius(8.0f);
    auto* text0 = layer0->SetChild(std::make_unique<TextBlock>());
    text0->SetText(L"ZIndex = 0");
    Canvas::SetLeft(layer0, 20.0f);
    Canvas::SetTop(layer0, 20.0f);
    Canvas::SetZIndex(layer0, 0);

    auto* layer2 = canvas3->Add(std::make_unique<Border>());
    layer2->SetWidth(160.0f); layer2->SetHeight(120.0f);
    layer2->SetBorderThickness(2.0f); layer2->SetCornerRadius(8.0f);
    auto* text2 = layer2->SetChild(std::make_unique<TextBlock>());
    text2->SetText(L"ZIndex = 2 (top)");
    Canvas::SetLeft(layer2, 100.0f);
    Canvas::SetTop(layer2, 60.0f);
    Canvas::SetZIndex(layer2, 2);

    auto* layer1 = canvas3->Add(std::make_unique<Border>());
    layer1->SetWidth(160.0f); layer1->SetHeight(120.0f);
    layer1->SetBorderThickness(2.0f); layer1->SetCornerRadius(8.0f);
    auto* text1 = layer1->SetChild(std::make_unique<TextBlock>());
    text1->SetText(L"ZIndex = 1");
    Canvas::SetLeft(layer1, 60.0f);
    Canvas::SetTop(layer1, 40.0f);
    Canvas::SetZIndex(layer1, 1);

    CreateCodeExample(content, LR"(Canvas::SetZIndex(element, 2);  // higher ZIndex paints on top
// Children with the same ZIndex follow insertion order)");

    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateScrollViewerPage() {
    auto [page, content] = CreatePageShell(L"ScrollViewer");

    // 示例 1：ScrollPanel 演示（现有实现）
    auto* card1 = CreateExampleCard(content, L"ScrollPanel: 现有的滚动容器");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"FluentUI 使用 ScrollPanel 作为滚动容器。它支持鼠标滚轮、拖拽滚动条、键盘导航（PageUp/PageDown/Home/End）。");
    desc1->SetWrap(true);

    // 创建一个 ScrollPanel 包含大量内容
    auto* scrollBorder = card1->Add(std::make_unique<Border>());
    scrollBorder->SetWidth(520.0f);
    scrollBorder->SetHeight(200.0f);
    scrollBorder->SetBorderThickness(1.0f);
    scrollBorder->SetCornerRadius(6.0f);

    auto* scroll = scrollBorder->SetChild(std::make_unique<ScrollPanel>());
    auto* stack = scroll->Add(std::make_unique<StackPanel>());
    stack->SetSpacing(8.0f);

    for (int i = 1; i <= 30; ++i) {
        auto* item = stack->Add(std::make_unique<Border>());
        item->SetHeight(40.0f);
        item->SetBorderThickness(1.0f);
        item->SetCornerRadius(4.0f);
        item->SetMargin(Thickness{4.0f});
        auto* text = item->SetChild(std::make_unique<TextBlock>());
        text->SetText(L"Scrollable item " + std::to_wstring(i));
        text->SetVAlign(VAlign::Center);
        text->SetMargin(Thickness{12.0f, 0, 0, 0});
    }

    auto* hint1 = card1->Add(std::make_unique<TextBlock>());
    hint1->SetText(L"💡 使用鼠标滚轮、拖拽滚动条或 PageUp/PageDown 键滚动。");
    hint1->SetWrap(true);
    hint1->SetFontSize(12.0f);
    hint1->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 示例 2：虚拟化 vs 非虚拟化
    auto* card2 = CreateExampleCard(content, L"何时使用虚拟化控件");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"ScrollPanel 不虚拟化——所有子元素都会创建和渲染。对于大量数据（>100 项），应该使用虚拟化控件：");
    desc2->SetWrap(true);

    auto* list = card2->Add(std::make_unique<StackPanel>());
    list->SetSpacing(4.0f);
    list->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* item1 = list->Add(std::make_unique<TextBlock>());
    item1->SetText(L"• TextArea: 大型文本文档（已测试 50,000 行）");
    item1->SetFontSize(13.0f);

    auto* item2 = list->Add(std::make_unique<TextBlock>());
    item2->SetText(L"• TreeView: 大型树结构（已测试 10,000 节点）");
    item2->SetFontSize(13.0f);

    auto* item3 = list->Add(std::make_unique<TextBlock>());
    item3->SetText(L"• ListBox: 长列表（支持虚拟化）");
    item3->SetFontSize(13.0f);

    CreateCodeExample(content, LR"(// ScrollPanel 用法
auto* scroll = panel->Add(std::make_unique<ScrollPanel>());
scroll->SetPadding(16.0f);

auto* content = scroll->Add(std::make_unique<StackPanel>());
content->SetSpacing(8.0f);

// 添加子元素
for (int i = 0; i < 50; ++i) {
    auto* item = content->Add(std::make_unique<Button>());
    item->SetText(L"Item " + std::to_wstring(i));
}

// 对于大量数据，使用虚拟化控件：
// - TextArea: 大文档
// - TreeView: 大树
// - ListBox: 长列表)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateExpanderPage() {
    auto [page, content] = CreatePageShell(L"Expander");

    // 示例 1: 基本用法（这一个开启了展开动画）
    auto* card1 = CreateExampleCard(content, L"Basic Expander（动画展开）");
    auto* exp1 = card1->Add(std::make_unique<Expander>());
    exp1->SetHeader(L"Click to expand");
    exp1->SetWidth(400.0f);
    // 唯一开启动画的一个，便于与下面两个瞬间展开的卡片直接对比。
    // 默认是 Transition::Instant —— 动画是显式选择，不是升级后自动变化的行为。
    // 注意收起时内容会立即 detach（见 Expander::SyncContentAttachment 的注释），
    // 所以收起动画擦除的是空白区域，不是内容滑走。
    exp1->SetUserToggleTransition(Expander::Transition::Animate);
    auto expContent1 = std::make_unique<StackPanel>();
    expContent1->SetSpacing(8.0f);
    expContent1->SetMargin(Thickness(12.0f));
    for (int i = 1; i <= 3; ++i) {
        auto* item = expContent1->Add(std::make_unique<TextBlock>());
        item->SetText(L"Item " + std::to_wstring(i));
    }
    exp1->SetContent(std::move(expContent1));

    // 示例 2: 默认展开
    auto* card2 = CreateExampleCard(content, L"Initially expanded");
    auto* exp2 = card2->Add(std::make_unique<Expander>());
    exp2->SetHeader(L"Settings");
    exp2->SetWidth(400.0f);
    exp2->SetExpanded(true);
    auto expContent2 = std::make_unique<StackPanel>();
    expContent2->SetSpacing(8.0f);
    expContent2->SetMargin(Thickness(12.0f));
    auto* opt1 = expContent2->Add(std::make_unique<Button>());
    opt1->SetText(L"Option 1");
    opt1->SetKind(Button::Kind::Standard);
    auto* opt2 = expContent2->Add(std::make_unique<Button>());
    opt2->SetText(L"Option 2");
    opt2->SetKind(Button::Kind::Standard);
    exp2->SetContent(std::move(expContent2));

    // 示例 3: 嵌套 Expander
    auto* card3 = CreateExampleCard(content, L"Nested Expanders");
    auto* exp3 = card3->Add(std::make_unique<Expander>());
    exp3->SetHeader(L"Category A");
    exp3->SetWidth(400.0f);
    auto expContent3 = std::make_unique<StackPanel>();
    expContent3->SetSpacing(8.0f);
    expContent3->SetMargin(Thickness(12.0f));

    auto* nested1 = expContent3->Add(std::make_unique<Expander>());
    nested1->SetHeader(L"Subcategory 1");
    auto nestedContent1 = std::make_unique<TextBlock>();
    nestedContent1->SetText(L"Nested content 1");
    nestedContent1->SetMargin(Thickness(12.0f));
    nested1->SetContent(std::move(nestedContent1));

    auto* nested2 = expContent3->Add(std::make_unique<Expander>());
    nested2->SetHeader(L"Subcategory 2");
    auto nestedContent2 = std::make_unique<TextBlock>();
    nestedContent2->SetText(L"Nested content 2");
    nestedContent2->SetMargin(Thickness(12.0f));
    nested2->SetContent(std::move(nestedContent2));

    exp3->SetContent(std::move(expContent3));

    CreateCodeExample(content, LR"(auto* expander = panel->Add(std::make_unique<Expander>());
expander->SetHeader(L"Settings");

auto content = std::make_unique<StackPanel>();
content->SetSpacing(8.0f);
// ... add children
expander->SetContent(std::move(content));

// 展开/收起的过渡方式由调用者决定，默认 Instant（瞬间）。
expander->SetExpanded(true);                          // 瞬间，等同 Instant
expander->SetExpanded(true, Expander::Transition::Instant);
expander->SetExpanded(true, Expander::Transition::Animate);   // 缓动

// 用户点击表头（或 Space/Enter）时的过渡，是控件自身的属性 ——
// 点击没法传参数，所以单独设置；同样默认 Instant。
expander->SetUserToggleTransition(Expander::Transition::Animate);)");

    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateViewboxPage() {
    auto [page, content] = CreatePageShell(L"Viewbox");

    // 示例 1: Uniform scaling (preserve aspect ratio)
    auto* card1 = CreateExampleCard(content, L"Uniform - Preserve Aspect Ratio");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"Viewbox 将子元素缩放到容器大小，保持宽高比。内容以自然尺寸测量，然后通过变换缩放。");
    desc1->SetWrap(true);
    desc1->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* viewbox1 = card1->Add(std::make_unique<Viewbox>());
    viewbox1->SetWidth(300.0f);
    viewbox1->SetHeight(200.0f);
    viewbox1->SetStretch(Stretch::Uniform);
    auto child1 = std::make_unique<Button>();
    child1->SetText(L"按钮 120x40");
    child1->SetWidth(120.0f);
    child1->SetHeight(40.0f);
    viewbox1->SetChild(std::move(child1));

    // 示例 2: Fill (may distort)
    auto* card2 = CreateExampleCard(content, L"Fill - Stretch to Fill");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"Fill 模式拉伸内容填满容器，可能改变宽高比。");
    desc2->SetWrap(true);
    desc2->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* viewbox2 = card2->Add(std::make_unique<Viewbox>());
    viewbox2->SetWidth(300.0f);
    viewbox2->SetHeight(200.0f);
    viewbox2->SetStretch(Stretch::Fill);
    auto child2 = std::make_unique<TextBlock>();
    child2->SetText(L"文本块 100x50");
    child2->SetWidth(100.0f);
    child2->SetHeight(50.0f);
    viewbox2->SetChild(std::move(child2));

    // 示例 3: UniformToFill (crop overflow)
    auto* card3 = CreateExampleCard(content, L"UniformToFill - Crop to Fill");
    auto* desc3 = card3->Add(std::make_unique<TextBlock>());
    desc3->SetText(L"UniformToFill 保持宽高比，缩放至完全填满容器，裁剪溢出部分。");
    desc3->SetWrap(true);
    desc3->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* viewbox3 = card3->Add(std::make_unique<Viewbox>());
    viewbox3->SetWidth(300.0f);
    viewbox3->SetHeight(200.0f);
    viewbox3->SetStretch(Stretch::UniformToFill);
    auto child3 = std::make_unique<Border>();
    child3->SetWidth(100.0f);
    child3->SetHeight(100.0f);
    child3->SetBackground(D2D1::ColorF(0x0078D4, 1.0f));  // Accent blue
    viewbox3->SetChild(std::move(child3));

    CreateCodeExample(content, LR"(auto viewbox = std::make_unique<Viewbox>();
viewbox->SetStretch(Stretch::Uniform);  // None / Fill / Uniform / UniformToFill
auto child = std::make_unique<Button>();
child->SetText(L"Content");
viewbox->SetChild(std::move(child));)");

    return std::move(page);
}

} // namespace fluent
