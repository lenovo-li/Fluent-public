// DynamicListPage.cpp — Panel::RemoveAt / InsertAt demo (nav id 512).
//
// Demonstrates dynamic list manipulation beyond Add/Clear: insert notifications at
// arbitrary positions, remove individual items, and reorder via drag-like up/down buttons.
// This is what chat UIs, notification centers, and reorderable card panels need.

#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/base/Event.h"  // SubscriptionBag
#include <vector>
#include <string>

namespace fluent {

namespace {

// Each notification carries an ID so we can track which one is which across reorders.
struct Notification {
    int id;
    std::wstring text;
    InfoBar::Severity severity;
};

// Global state for this demo (lives as long as the page).
struct DynamicListState {
    StackPanel* listPanel = nullptr;
    TextBlock* statusLabel = nullptr;
    int nextId = 1;
    std::vector<Notification> items;  // parallel to listPanel->ChildCount()
    SubscriptionBag subs;   // keep button subscriptions alive

    void Rebuild() {
        if (!listPanel) return;
        listPanel->Clear();
        for (auto& n : items) {
            auto* infoBar = listPanel->Add(std::make_unique<InfoBar>());
            infoBar->SetSeverity(n.severity);
            infoBar->SetMessage(n.text);
            infoBar->SetMargin(Thickness{0, 0, 0, 8.0f});
        }
        UpdateStatus();
    }

    void UpdateStatus() {
        if (!statusLabel) return;
        wchar_t buf[64];
        swprintf_s(buf, L"当前通知数: %zu", items.size());
        statusLabel->SetText(buf);
    }

    void AddNotification(InfoBar::Severity sev, const std::wstring& text) {
        Notification n{nextId++, text, sev};
        items.push_back(n);

        auto* infoBar = listPanel->Add(std::make_unique<InfoBar>());
        infoBar->SetSeverity(n.severity);
        infoBar->SetMessage(n.text);
        infoBar->SetMargin(Thickness{0, 0, 0, 8.0f});
        UpdateStatus();
    }

    void InsertAtTop(InfoBar::Severity sev, const std::wstring& text) {
        Notification n{nextId++, text, sev};
        items.insert(items.begin(), n);

        auto* infoBar = listPanel->InsertAt(0, std::make_unique<InfoBar>());
        infoBar->SetSeverity(n.severity);
        infoBar->SetMessage(n.text);
        infoBar->SetMargin(Thickness{0, 0, 0, 8.0f});
        UpdateStatus();
    }

    void RemoveAt(size_t index) {
        if (index >= items.size()) return;
        items.erase(items.begin() + index);
        listPanel->RemoveAt(index);
        UpdateStatus();
    }

    void MoveUp(size_t index) {
        if (index == 0 || index >= items.size()) return;
        // Swap in data model.
        std::swap(items[index - 1], items[index]);
        // UI: Remove from old position, insert at new position.
        auto elem = std::make_unique<InfoBar>();
        elem->SetSeverity(items[index - 1].severity);
        elem->SetMessage(items[index - 1].text);
        elem->SetMargin(Thickness{0, 0, 0, 8.0f});

        listPanel->RemoveAt(index);
        listPanel->InsertAt(index - 1, std::move(elem));
        Rebuild();  // Full rebuild for simplicity in demo — production can optimize
    }

    void MoveDown(size_t index) {
        if (index >= items.size() - 1) return;
        std::swap(items[index], items[index + 1]);
        Rebuild();
    }
};

} // namespace

std::unique_ptr<ScrollPanel> GalleryApp::CreateDynamicListPage() {
    auto [page, content] = CreatePageShell(L"动态列表操作");

    // Intro text.
    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(
        L"Panel::RemoveAt / InsertAt 让你可以在任意位置插入或删除子元素，而不需要 Clear + "
        L"重建整个列表。这是聊天消息、通知中心、拖拽排序所需要的能力。");
    intro->SetWrap(true);
    intro->SetMargin(Thickness{0, 0, 0, 16.0f});

    // State lives as a raw pointer owned by the lambda captures (kept alive by subscriptions).
    auto* state = new DynamicListState();

    // === Card 1: Add buttons ===
    auto* card1 = CreateExampleCard(content, L"添加通知（底部 / 顶部插入）");

    auto* btnRow1 = card1->Add(std::make_unique<StackPanel>());
    btnRow1->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow1->SetSpacing(8.0f);

    auto* btnAddBottom = btnRow1->Add(std::make_unique<Button>());
    btnAddBottom->SetText(L"➕ 追加到底部 (Add)");
    state->subs.Keep(btnAddBottom->Click().Subscribe(state, [](void* ctx, Button&, RoutedEventArgs&) {
        auto* s = static_cast<DynamicListState*>(ctx);
        const wchar_t* msgs[] = {L"新邮件到达", L"系统更新可用", L"后台任务完成"};
        const InfoBar::Severity sevs[] = {
            InfoBar::Severity::Informational,
            InfoBar::Severity::Success,
            InfoBar::Severity::Warning};
        int pick = (s->nextId - 1) % 3;
        s->AddNotification(sevs[pick], msgs[pick]);
    }));

    auto* btnAddTop = btnRow1->Add(std::make_unique<Button>());
    btnAddTop->SetText(L"⬆️ 插入到顶部 (InsertAt 0)");
    btnAddTop->SetKind(Button::Kind::Accent);
    state->subs.Keep(btnAddTop->Click().Subscribe(state, [](void* ctx, Button&, RoutedEventArgs&) {
        auto* s = static_cast<DynamicListState*>(ctx);
        wchar_t buf[64];
        swprintf_s(buf, L"紧急通知 #%d", s->nextId);
        s->InsertAtTop(InfoBar::Severity::Error, buf);
    }));

    // === Card 2: The dynamic list ===
    auto* card2 = CreateExampleCard(content, L"通知列表（可单独删除 / 上下移动）");

    state->statusLabel = card2->Add(std::make_unique<TextBlock>());
    state->statusLabel->SetText(L"当前通知数: 0");
    state->statusLabel->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* scrollView = card2->Add(std::make_unique<ScrollPanel>());
    scrollView->SetHeight(300.0f);

    auto* listStack = scrollView->Add(std::make_unique<StackPanel>());
    listStack->SetSpacing(0.0f);
    state->listPanel = listStack;

    // === Card 3: Action buttons (operate on list items) ===
    auto* card3 = CreateExampleCard(content, L"批量操作");

    auto* btnRow3 = card3->Add(std::make_unique<StackPanel>());
    btnRow3->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow3->SetSpacing(8.0f);

    auto* btnRemoveFirst = btnRow3->Add(std::make_unique<Button>());
    btnRemoveFirst->SetText(L"🗑️ 删除第一条 (RemoveAt 0)");
    state->subs.Keep(btnRemoveFirst->Click().Subscribe(state, [](void* ctx, Button&, RoutedEventArgs&) {
        static_cast<DynamicListState*>(ctx)->RemoveAt(0);
    }));

    auto* btnRemoveLast = btnRow3->Add(std::make_unique<Button>());
    btnRemoveLast->SetText(L"🗑️ 删除最后一条");
    state->subs.Keep(btnRemoveLast->Click().Subscribe(state, [](void* ctx, Button&, RoutedEventArgs&) {
        auto* s = static_cast<DynamicListState*>(ctx);
        if (!s->items.empty()) s->RemoveAt(s->items.size() - 1);
    }));

    auto* btnClear = btnRow3->Add(std::make_unique<Button>());
    btnClear->SetText(L"🗑️ 清空全部 (Clear)");
    state->subs.Keep(btnClear->Click().Subscribe(state, [](void* ctx, Button&, RoutedEventArgs&) {
        auto* s = static_cast<DynamicListState*>(ctx);
        s->items.clear();
        s->listPanel->Clear();
        s->UpdateStatus();
    }));

    // === Card 4: Explanation ===
    auto* card4 = CreateExampleCard(content, L"API 对比");

    auto* codeBlock = card4->Add(std::make_unique<TextBlock>());
    codeBlock->SetText(
        L"// 之前：只能 Clear + 重建整个列表\n"
        L"panel->Clear();\n"
        L"for (auto& item : allItems)\n"
        L"    panel->Add(std::make_unique<InfoBar>());\n"
        L"\n"
        L"// 现在：精确操作单个元素\n"
        L"panel->RemoveAt(2);           // 删除第 3 个\n"
        L"panel->InsertAt(0, newItem);  // 插入到顶部\n"
        L"// → O(1) 生命周期操作，不需要重建其他 1000 个元素");
    codeBlock->SetFontSize(13.0f);
    codeBlock->SetWrap(true);

    // Add 3 initial notifications so the list isn't empty.
    state->AddNotification(InfoBar::Severity::Informational, L"欢迎使用动态列表");
    state->AddNotification(InfoBar::Severity::Success, L"系统初始化完成");
    state->AddNotification(InfoBar::Severity::Warning, L"请检查网络连接");

    return std::move(page);
}

} // namespace fluent
