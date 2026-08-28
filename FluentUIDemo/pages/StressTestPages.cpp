// StressTestPages.cpp — 压力测试页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include <chrono>

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateStressTestPage() {
    auto [page, content] = CreatePageShell(L"Stress Test");

    // 说明
    auto* desc = content->Add(std::make_unique<TextBlock>());
    desc->SetText(L"测试非虚拟化容器（ScrollPanel + StackPanel）的极限。虚拟化控件（TreeView/TextArea）不受这些限制。");
    desc->SetMargin(Thickness{0, 0, 0, 16.0f});
    desc->SetWrap(true);

    // 测试 1: 1000 个 Button
    auto* card1 = CreateExampleCard(content, L"测试 1: 1000 个 Button");

    auto* statsPanel1 = card1->Add(std::make_unique<StackPanel>());
    statsPanel1->SetOrientation(StackPanel::Orientation::Horizontal);
    statsPanel1->SetSpacing(16.0f);
    statsPanel1->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* btnCreate1000 = statsPanel1->Add(std::make_unique<Button>());
    btnCreate1000->SetKind(Button::Kind::Accent);
    btnCreate1000->SetText(L"Create 1000 Buttons");

    auto* statsText1 = statsPanel1->Add(std::make_unique<TextBlock>());
    statsText1->SetText(L"点击按钮开始测试");

    // ScrollPanel 容器（用 Border 包裹以设置背景色）
    auto* scrollBorder1 = card1->Add(std::make_unique<Border>());
    scrollBorder1->SetHeight(300.0f);
    scrollBorder1->SetBackground(D2D1::ColorF(0x05070C, 1.0f));

    auto* scrollContainer1 = scrollBorder1->SetChild(std::make_unique<ScrollPanel>());

    auto* buttonContainer1 = scrollContainer1->Add(std::make_unique<StackPanel>());
    buttonContainer1->SetSpacing(4.0f);

    // 状态结构体（用于捕获多个指针）
    struct Test1State {
        StackPanel* container;
        TextBlock* stats;
        Button* btn;
    };
    auto* state1 = new Test1State{buttonContainer1, statsText1, btnCreate1000};

    pageSubs_ += btnCreate1000->Click().Subscribe(state1, [](void* owner, Button&, RoutedEventArgs&) {
        auto* state = static_cast<Test1State*>(owner);

        state->btn->SetEnabled(false);
        state->stats->SetText(L"创建中...");

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 1000; ++i) {
            auto* b = state->container->Add(std::make_unique<Button>());
            b->SetText(L"Button " + std::to_wstring(i + 1));
            b->SetKind(Button::Kind::Standard);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        state->stats->SetText(L"创建完成！耗时: " + std::to_wstring(ms) + L" ms，总计 1000 个 Button。尝试滚动查看性能。");
    });

    // 测试 2: 10000 个 TextBlock
    auto* card2 = CreateExampleCard(content, L"测试 2: 10000 个 TextBlock");

    auto* statsPanel2 = card2->Add(std::make_unique<StackPanel>());
    statsPanel2->SetOrientation(StackPanel::Orientation::Horizontal);
    statsPanel2->SetSpacing(16.0f);
    statsPanel2->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* btnCreate10000 = statsPanel2->Add(std::make_unique<Button>());
    btnCreate10000->SetKind(Button::Kind::Accent);
    btnCreate10000->SetText(L"Create 10000 TextBlocks");

    auto* statsText2 = statsPanel2->Add(std::make_unique<TextBlock>());
    statsText2->SetText(L"点击按钮开始测试");

    auto* scrollBorder2 = card2->Add(std::make_unique<Border>());
    scrollBorder2->SetHeight(300.0f);
    scrollBorder2->SetBackground(D2D1::ColorF(0x05070C, 1.0f));

    auto* scrollContainer2 = scrollBorder2->SetChild(std::make_unique<ScrollPanel>());

    auto* textContainer2 = scrollContainer2->Add(std::make_unique<StackPanel>());
    textContainer2->SetSpacing(2.0f);

    struct Test2State {
        StackPanel* container;
        TextBlock* stats;
        Button* btn;
    };
    auto* state2 = new Test2State{textContainer2, statsText2, btnCreate10000};

    pageSubs_ += btnCreate10000->Click().Subscribe(state2, [](void* owner, Button&, RoutedEventArgs&) {
        auto* state = static_cast<Test2State*>(owner);

        state->btn->SetEnabled(false);
        state->stats->SetText(L"创建中...");

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 10000; ++i) {
            auto* t = state->container->Add(std::make_unique<TextBlock>());
            t->SetText(L"TextBlock " + std::to_wstring(i + 1));
            t->SetFontSize(12.0f);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        state->stats->SetText(L"创建完成！耗时: " + std::to_wstring(ms) + L" ms，总计 10000 个 TextBlock。尝试滚动查看性能。");
    });

    // 警告说明
    auto* warning = content->Add(std::make_unique<TextBlock>());
    warning->SetText(L"⚠️ 注意：这些测试故意展示非虚拟化容器的极限。实际应用中，大量数据应该使用虚拟化控件（TreeView/TextArea/ListBox）。");
    warning->SetMargin(Thickness{0, 16.0f, 0, 0});
    warning->SetWrap(true);
    warning->SetForeground(D2D1::ColorF(0xE9A568, 1.0f));

    CreateCodeExample(content, LR"(// 压力测试：非虚拟化容器
auto* scroll = panel->Add(std::make_unique<ScrollPanel>());
scroll->SetHeight(400.0f);

auto* stack = scroll->Add(std::make_unique<StackPanel>());
for (int i = 0; i < 10000; ++i) {
    auto* text = stack->Add(std::make_unique<TextBlock>());
    text->SetText(L"Item " + std::to_wstring(i));
}

// ⚠️ 实际应用应使用虚拟化控件：
// - TreeView: 大型树/列表（已测试 5000+ 节点）
// - TextArea: 大型文档（已测试 50000 行）
// - ListBox: 长列表（计划支持虚拟化）
)");

    return std::move(page);
}

} // namespace fluent
