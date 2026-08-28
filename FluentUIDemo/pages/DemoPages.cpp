// DemoPages.cpp — 性能演示和主题页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/TreeView.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateVirtualizationDemoPage() {
    auto [page, content] = CreatePageShell(L"Virtualization Demo");

    // TextArea 虚拟化演示
    auto* card1 = CreateExampleCard(content, L"TextArea: 50,000 lines (NoWrap mode)");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"Scrolling should remain smooth regardless of document size. Press F12 to see CPU frame time.");
    desc1->SetWrap(true); desc1->SetFontSize(12.0f);
    auto* area = card1->Add(std::make_unique<TextArea>());
    area->SetWidth(620.0f); area->SetHeight(200.0f); area->SetWrapMode(TextWrapMode::NoWrap);
    std::wstring largeText;
    largeText.reserve(50000 * 50);
    for (int i = 1; i <= 50000; ++i) {
        largeText += std::to_wstring(i) + L": Lorem ipsum dolor sit amet line " + std::to_wstring(i) + L"\n";
    }
    area->SetText(std::move(largeText));

    // TreeView 虚拟化演示
    auto* card2 = CreateExampleCard(content, L"TreeView: 5,000 nodes");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"TreeView virtualizes visible rows. Expand/collapse should be instant.");
    desc2->SetWrap(true); desc2->SetFontSize(12.0f);
    auto* tree = card2->Add(std::make_unique<TreeView>());
    tree->SetWidth(620.0f); tree->SetHeight(200.0f);
    std::vector<TreeViewRow> rows;
    rows.reserve(5000);
    for (int i = 0; i < 100; ++i) {
        rows.push_back(TreeViewRow{i * 50, -1, L"Parent " + std::to_wstring(i), 0, false, true, true, TreeViewIcon::Folder, nullptr});
        for (int j = 0; j < 49; ++j) {
            rows.push_back(TreeViewRow{i * 50 + j + 1, i * 50, L"Child " + std::to_wstring(i) + L"." + std::to_wstring(j), 1, false, false, false, TreeViewIcon::None, nullptr});
        }
    }
    tree->SetRows(std::move(rows));

    CreateCodeExample(content, LR"(// TextArea NoWrap: O(visible) rendering
area->SetWrapMode(TextWrapMode::NoWrap);
// TreeView: O(visible rows), not O(total nodes)
tree->SetRows(largeDataset);)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateLogDemoPage() {
    auto [page, content] = CreatePageShell(L"Log Demo");
    auto* card = CreateExampleCard(content, L"Append-only TextArea");
    auto* area = card->Add(std::make_unique<TextArea>());
    area->SetWidth(620.0f); area->SetHeight(280.0f); area->SetReadOnly(true); area->SetAutoScrollToTail(true);
    area->SetText(LR"([info] Gallery initialized
[info] Text virtualization enabled
[ready] AppendText keeps the tail visible
)");
    auto* add = card->Add(std::make_unique<Button>()); add->SetText(L"Append log line");
    pageSubs_.Keep(add->Click().Subscribe(area, [](void* owner, Button&, RoutedEventArgs&) {
        static_cast<TextArea*>(owner)->AppendText(L"[event] User appended a log line\n");
    }));
    CreateCodeExample(content, LR"(area->SetReadOnly(true);
area->SetAutoScrollToTail(true);
area->AppendText(L"new log line\n");)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateThemeDemoPage() {
    auto [page, content] = CreatePageShell(L"Theme Demo");
    auto* card = CreateExampleCard(content, L"Live theme switching");
    auto* text = card->Add(std::make_unique<TextBlock>());
    text->SetText(L"Click the title-bar theme button (☀️/🌙) to switch between light and dark themes. All controls update instantly without rebuilding the tree.");
    text->SetWrap(true);

    // 展示各种控件在主题下的外观
    auto* samplePanel = card->Add(std::make_unique<StackPanel>());
    samplePanel->SetSpacing(12.0f); samplePanel->SetMargin(Thickness{0, 16.0f, 0, 0});

    auto* btnRow = samplePanel->Add(std::make_unique<StackPanel>());
    btnRow->SetOrientation(StackPanel::Orientation::Horizontal); btnRow->SetSpacing(8.0f);
    auto* btn1 = btnRow->Add(std::make_unique<Button>()); btn1->SetText(L"Default");
    auto* btn2 = btnRow->Add(std::make_unique<Button>()); btn2->SetText(L"Accent"); btn2->SetKind(Button::Kind::Accent);

    auto* check = samplePanel->Add(std::make_unique<CheckBox>());
    check->SetText(L"CheckBox"); check->SetChecked(true);

    auto* toggle = samplePanel->Add(std::make_unique<ToggleSwitch>());
    toggle->SetText(L"ToggleSwitch"); toggle->SetOn(true);

    auto* progress = samplePanel->Add(std::make_unique<ProgressBar>());
    progress->SetWidth(420.0f); progress->SetValue(0.65f);

    auto* hint = card->Add(std::make_unique<TextBlock>());
    hint->SetText(L"Theme tokens are resolved from ThemeSnapshot, which rebuilds layouts and clears caches when the generation stamp changes.");
    hint->SetWrap(true); hint->SetFontSize(12.0f); hint->SetMargin(Thickness{0, 16.0f, 0, 0});

    CreateCodeExample(content, LR"(// Theme switching is handled by ThemeManager
themeManager->SetMode(ThemeMode::Dark);
// All controls read tokens via Theme()->cardFill, etc.
// Generation bump triggers layout/cache invalidation.)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateAnimationDemoPage() {
    auto [page, content] = CreatePageShell(L"Animation Demo");
    auto* card = CreateExampleCard(content, L"Compositor-driven smoothness");
    auto* text = card->Add(std::make_unique<TextBlock>());
    text->SetText(L"Animations run on the DirectComposition thread, independent of UI thread load. Scrolling, button hover, progress sweep, and toggle transitions use the shared AnimationRegistry.");
    text->SetWrap(true);

    // 演示动画控件
    auto* samplePanel = card->Add(std::make_unique<StackPanel>());
    samplePanel->SetSpacing(16.0f); samplePanel->SetMargin(Thickness{0, 16.0f, 0, 0});

    auto* indeterminate = samplePanel->Add(std::make_unique<ProgressBar>());
    indeterminate->SetWidth(420.0f); indeterminate->SetIndeterminate(true);
    auto* label1 = samplePanel->Add(std::make_unique<TextBlock>());
    label1->SetText(L"↑ Indeterminate progress: compositor-driven sweep"); label1->SetFontSize(12.0f);

    auto* btnRow = samplePanel->Add(std::make_unique<StackPanel>());
    btnRow->SetOrientation(StackPanel::Orientation::Horizontal); btnRow->SetSpacing(8.0f);
    auto* btn1 = btnRow->Add(std::make_unique<Button>()); btn1->SetText(L"Hover me");
    auto* btn2 = btnRow->Add(std::make_unique<Button>()); btn2->SetText(L"And me"); btn2->SetKind(Button::Kind::Accent);
    auto* label2 = samplePanel->Add(std::make_unique<TextBlock>());
    label2->SetText(L"↑ Button hover: eased background transition"); label2->SetFontSize(12.0f);

    auto* toggle = samplePanel->Add(std::make_unique<ToggleSwitch>());
    toggle->SetText(L"Toggle me"); toggle->SetOn(false);
    auto* label3 = samplePanel->Add(std::make_unique<TextBlock>());
    label3->SetText(L"↑ ToggleSwitch: eased slider animation"); label3->SetFontSize(12.0f);

    auto* hint = card->Add(std::make_unique<TextBlock>());
    hint->SetText(L"Open F12 HUD to verify smooth frame pacing. CPU-heavy operations on the UI thread should not stutter compositor-driven animations.");
    hint->SetWrap(true); hint->SetFontSize(12.0f); hint->SetMargin(Thickness{0, 16.0f, 0, 0});

    CreateCodeExample(content, LR"(// ScrollContentHost uses compositor offload
scrollHost->AnimateOffset(targetOffset, duration);
// ProgressBar indeterminate mode: compositor visual
progress->SetIndeterminate(true);
// CheckBox/ToggleSwitch: per-frame AnimatedValue easing)");
    return std::move(page);
}

} // namespace fluent
