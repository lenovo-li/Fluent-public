// DataPages.cpp — 数据控件页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/TreeView.h"
#include "../../FluentUI/controls/TabControl.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include <algorithm>

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateTreeViewPage() {
    auto [page, content] = CreatePageShell(L"TreeView");

    // 示例 1：虚拟化（100 节点树）
    auto* card1 = CreateExampleCard(content, L"虚拟化（100 节点）");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"TreeView 只渲染可见行，与节点总数无关。滚动 100 节点应该流畅。");
    desc1->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* tree1 = card1->Add(std::make_unique<TreeView>());
    tree1->SetHeight(300.0f);

    std::vector<TreeViewRow> rows;
    int idCounter = 1;
    for (int parent = 0; parent < 10; ++parent) {
        int parentId = idCounter++;
        rows.push_back(TreeViewRow{
            parentId, -1, L"父节点 " + std::to_wstring(parent + 1), 0,
            false, true, true, TreeViewIcon::Folder, nullptr
        });
        for (int child = 0; child < 9; ++child) {
            rows.push_back(TreeViewRow{
                idCounter++, parentId, L"子节点 " + std::to_wstring(child + 1), 1,
                false, false, false, TreeViewIcon::Device, nullptr
            });
        }
    }
    tree1->SetRows(std::move(rows));

    // 示例 2：多选模式
    auto* card2 = CreateExampleCard(content, L"多选模式");
    auto* tree2 = card2->Add(std::make_unique<TreeView>());
    tree2->SetHeight(200.0f);
    tree2->SetSelectionMode(SelectionMode::Multiple);

    std::vector<TreeViewRow> rows2;
    rows2.push_back(TreeViewRow{1, -1, L"项目", 0, false, true, true, TreeViewIcon::Folder, nullptr});
    rows2.push_back(TreeViewRow{2, 1, L"main.cpp", 1, false, false, false, TreeViewIcon::None, nullptr});
    rows2.push_back(TreeViewRow{3, 1, L"utils.cpp", 1, false, false, false, TreeViewIcon::None, nullptr});
    rows2.push_back(TreeViewRow{4, -1, L"文档", 0, false, true, true, TreeViewIcon::Folder, nullptr});
    rows2.push_back(TreeViewRow{5, 4, L"readme.md", 1, false, false, false, TreeViewIcon::None, nullptr});
    tree2->SetRows(std::move(rows2));

    auto* multiHint = card2->Add(std::make_unique<TextBlock>());
    multiHint->SetText(L"Ctrl+点击：切换选中；Shift+点击：范围选择");
    multiHint->SetFontSize(12.0f);
    multiHint->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 示例 3：图标演示
    auto* card3 = CreateExampleCard(content, L"图标");
    auto* tree3 = card3->Add(std::make_unique<TreeView>());
    tree3->SetHeight(200.0f);

    std::vector<TreeViewRow> rows3;
    rows3.push_back(TreeViewRow{1, -1, L"此电脑", 0, false, true, true, TreeViewIcon::Computer, nullptr});
    rows3.push_back(TreeViewRow{2, 1, L"C: 系统盘", 1, false, false, false, TreeViewIcon::Disk, nullptr});
    rows3.push_back(TreeViewRow{3, 1, L"D: 数据盘", 1, false, false, false, TreeViewIcon::Disk, nullptr});
    rows3.push_back(TreeViewRow{4, -1, L"设备", 0, false, true, true, TreeViewIcon::Device, nullptr});
    rows3.push_back(TreeViewRow{5, 4, L"蓝牙设备", 1, false, false, false, TreeViewIcon::Bluetooth, nullptr});
    rows3.push_back(TreeViewRow{6, -1, L"警告", 0, false, false, false, TreeViewIcon::Warning, nullptr});
    tree3->SetRows(std::move(rows3));

    // 示例 4：动态操作
    auto* card4 = CreateExampleCard(content, L"动态操作");

    // 按钮面板
    auto* btnPanel = card4->Add(std::make_unique<StackPanel>());
    btnPanel->SetOrientation(StackPanel::Orientation::Horizontal);
    btnPanel->SetSpacing(8.0f);
    btnPanel->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* btnAddRoot = btnPanel->Add(std::make_unique<Button>());
    btnAddRoot->SetKind(Button::Kind::Standard);
    btnAddRoot->SetText(L"Add Root Node");

    auto* btnAddChild = btnPanel->Add(std::make_unique<Button>());
    btnAddChild->SetKind(Button::Kind::Standard);
    btnAddChild->SetText(L"Add Child to Selected");

    auto* btnRemove = btnPanel->Add(std::make_unique<Button>());
    btnRemove->SetKind(Button::Kind::Standard);
    btnRemove->SetText(L"Remove Selected");

    auto* btnExpandAll = btnPanel->Add(std::make_unique<Button>());
    btnExpandAll->SetKind(Button::Kind::Standard);
    btnExpandAll->SetText(L"Expand All");

    auto* btnCollapseAll = btnPanel->Add(std::make_unique<Button>());
    btnCollapseAll->SetKind(Button::Kind::Standard);
    btnCollapseAll->SetText(L"Collapse All");

    // TreeView
    auto* tree4 = card4->Add(std::make_unique<TreeView>());
    tree4->SetHeight(250.0f);

    std::vector<TreeViewRow> rows4;
    rows4.push_back(TreeViewRow{1, -1, L"Root Node 1", 0, false, true, true, TreeViewIcon::Folder, nullptr});
    rows4.push_back(TreeViewRow{2, 1, L"Child 1-1", 1, false, false, false, TreeViewIcon::Device, nullptr});
    rows4.push_back(TreeViewRow{3, 1, L"Child 1-2", 1, false, false, false, TreeViewIcon::Device, nullptr});
    tree4->SetRows(std::move(rows4));

    treeContext_.tree = tree4;
    treeContext_.nextId = 4;
    treeContext_.loaded = false;

    // Add Root Node 按钮
    pageSubs_ += btnAddRoot->Click().Subscribe(&treeContext_, [](void* owner, Button&, RoutedEventArgs&) {
        auto* ctx = static_cast<GalleryApp::TreeContext*>(owner);
        auto rows = ctx->tree->Items();
        int newId = ctx->nextId++;
        rows.push_back(TreeViewRow{
            newId, -1, L"Root Node " + std::to_wstring(newId), 0,
            false, true, true, TreeViewIcon::Folder, nullptr
        });
        ctx->tree->SetRows(std::move(rows));
    });

    // Add Child to Selected 按钮
    pageSubs_ += btnAddChild->Click().Subscribe(&treeContext_, [](void* owner, Button&, RoutedEventArgs&) {
        auto* ctx = static_cast<GalleryApp::TreeContext*>(owner);
        auto* selected = ctx->tree->SelectedRow();
        if (!selected) return;

        auto rows = ctx->tree->Items();
        int newId = ctx->nextId++;
        int parentId = selected->id;

        // 确保父节点有 hasChildren
        for (auto& row : rows) {
            if (row.id == parentId) {
                row.hasChildren = true;
                row.expanded = true;
            }
        }

        // 插入子节点到父节点后
        auto it = std::find_if(rows.begin(), rows.end(), [parentId](const TreeViewRow& r) { return r.id == parentId; });
        if (it != rows.end()) {
            rows.insert(it + 1, TreeViewRow{
                newId, parentId, L"Child " + std::to_wstring(newId), selected->depth + 1,
                false, false, false, TreeViewIcon::Device, nullptr
            });
        }
        ctx->tree->SetRows(std::move(rows));
    });

    // Remove Selected 按钮
    pageSubs_ += btnRemove->Click().Subscribe(&treeContext_, [](void* owner, Button&, RoutedEventArgs&) {
        auto* ctx = static_cast<GalleryApp::TreeContext*>(owner);
        auto* selected = ctx->tree->SelectedRow();
        if (!selected) return;

        auto rows = ctx->tree->Items();
        int selectedId = selected->id;

        // 移除选中节点及其所有子孙
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&rows, selectedId](const TreeViewRow& r) {
            if (r.id == selectedId) return true;
            // 递归查找是否是 selectedId 的子孙
            int pid = r.parentId;
            while (pid != -1) {
                if (pid == selectedId) return true;
                auto parent = std::find_if(rows.begin(), rows.end(), [pid](const TreeViewRow& p) { return p.id == pid; });
                if (parent == rows.end()) break;
                pid = parent->parentId;
            }
            return false;
        }), rows.end());

        ctx->tree->SetRows(std::move(rows));
    });

    // Expand All 按钮
    pageSubs_ += btnExpandAll->Click().Subscribe(&treeContext_, [](void* owner, Button&, RoutedEventArgs&) {
        auto* ctx = static_cast<GalleryApp::TreeContext*>(owner);
        auto rows = ctx->tree->Items();
        for (auto& row : rows) {
            if (row.hasChildren) row.expanded = true;
        }
        ctx->tree->SetRows(std::move(rows));
    });

    // Collapse All 按钮
    pageSubs_ += btnCollapseAll->Click().Subscribe(&treeContext_, [](void* owner, Button&, RoutedEventArgs&) {
        auto* ctx = static_cast<GalleryApp::TreeContext*>(owner);
        auto rows = ctx->tree->Items();
        for (auto& row : rows) {
            if (row.hasChildren) row.expanded = false;
        }
        ctx->tree->SetRows(std::move(rows));
    });

    CreateCodeExample(content, LR"(// TreeView 动态操作示例
auto* tree = panel->Add(std::make_unique<TreeView>());
tree->SetHeight(400.0f);

// 初始数据
std::vector<TreeViewRow> rows;
rows.push_back(TreeViewRow{1, -1, L"Root", 0, false, true, true, TreeViewIcon::Folder, nullptr});
tree->SetRows(std::move(rows));

// 动态添加节点
button->Clicked().Subscribe(tree, [](void* owner, Button&) {
    auto* tree = static_cast<TreeView*>(owner);
    auto rows = tree->Items();  // 读取当前数据
    rows.push_back(TreeViewRow{newId, -1, L"New Root", 0, false, true, true, TreeViewIcon::Folder, nullptr});
    tree->SetRows(std::move(rows));  // 写回
});

// 展开/折叠所有节点
expandAll->Clicked().Subscribe(tree, [](void* owner, Button&) {
    auto* tree = static_cast<TreeView*>(owner);
    auto rows = tree->Items();
    for (auto& row : rows) {
        if (row.hasChildren) row.expanded = true;
    }
    tree->SetRows(std::move(rows));
});
)");

    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateProgressBarPage() {
    auto [page, content] = CreatePageShell(L"ProgressBar");
    auto* card = CreateExampleCard(content, L"Determinate and compositor-driven indeterminate");

    auto* bar1 = card->Add(std::make_unique<ProgressBar>());
    bar1->SetWidth(420.0f); bar1->SetValue(0.15f);
    bar1->SetTooltip(L"15% 进度");

    auto* bar2 = card->Add(std::make_unique<ProgressBar>());
    bar2->SetWidth(420.0f); bar2->SetValue(0.50f);
    bar2->SetTooltip(L"50% 进度");

    auto* bar3 = card->Add(std::make_unique<ProgressBar>());
    bar3->SetWidth(420.0f); bar3->SetValue(0.85f);
    bar3->SetTooltip(L"85% 进度");

    auto* indeterminate = card->Add(std::make_unique<ProgressBar>());
    indeterminate->SetWidth(420.0f); indeterminate->SetIndeterminate(true);
    indeterminate->SetTooltip(L"不确定进度（合成器驱动动画）");

    // 动态进度演示：点击按钮模拟进度增长
    auto* animCard = CreateExampleCard(content, L"动态进度");
    auto* animBar = animCard->Add(std::make_unique<ProgressBar>());
    animBar->SetWidth(420.0f); animBar->SetValue(0.0f);

    auto* buttonPanel = animCard->Add(std::make_unique<StackPanel>());
    buttonPanel->SetOrientation(StackPanel::Orientation::Horizontal);
    buttonPanel->SetSpacing(8.0f);

    auto* startBtn = buttonPanel->Add(std::make_unique<Button>());
    startBtn->SetText(L"模拟进度增长");

    struct ProgressState { ProgressBar* bar; float step; };
    auto* state = new ProgressState{animBar, 0.0f};

    pageSubs_ += startBtn->Click().Subscribe(state, [](void* owner, Button&, RoutedEventArgs&) {
        auto* s = static_cast<ProgressState*>(owner);
        s->step = 0.0f;
        s->bar->SetValue(0.0f);
        // 真实应用应该监听后台任务进度更新，这里简化演示
        // 实际场景中，任务完成一步 → 调用 bar->SetValue(progress)
    });

    CreateCodeExample(content, LR"(progress->SetValue(0.65f);
indeterminate->SetIndeterminate(true);)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateTabControlPage() {
    auto [page, content] = CreatePageShell(L"TabControl");
    auto* card = CreateExampleCard(content, L"Tabs with dynamic add/remove");

    auto* tabs = card->Add(std::make_unique<TabControl>());
    tabs->SetWidth(600.0f); tabs->SetHeight(240.0f); tabs->SetCloseButtonVisible(true);

    // 初始三个标签
    for (int i = 1; i <= 3; ++i) {
        auto panel = std::make_unique<StackPanel>();
        panel->SetMargin(Thickness(16.0f)); panel->SetSpacing(8.0f);
        auto* text = panel->Add(std::make_unique<TextBlock>());
        text->SetText(L"Content of Tab " + std::to_wstring(i));
        tabs->AddTab(L"Tab " + std::to_wstring(i), std::move(panel));
    }

    // 操作按钮
    auto* btnPanel = card->Add(std::make_unique<StackPanel>());
    btnPanel->SetOrientation(StackPanel::Orientation::Horizontal);
    btnPanel->SetSpacing(8.0f);
    btnPanel->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* btnAdd = btnPanel->Add(std::make_unique<Button>());
    btnAdd->SetText(L"Add Tab");
    btnAdd->SetKind(Button::Kind::Accent);

    auto* btnRemove = btnPanel->Add(std::make_unique<Button>());
    btnRemove->SetText(L"Remove Last");

    auto* statusText = btnPanel->Add(std::make_unique<TextBlock>());
    statusText->SetText(L"");
    statusText->SetMargin(Thickness{12.0f, 0, 0, 0});

    // 状态结构
    struct TabState {
        TabControl* tabs;
        TextBlock* status;
        int nextId = 4;
    };
    auto* state = new TabState{tabs, statusText};

    // 添加标签
    pageSubs_ += btnAdd->Click().Subscribe(state, [](void* owner, Button&, RoutedEventArgs&) {
        auto* s = static_cast<TabState*>(owner);
        auto panel = std::make_unique<StackPanel>();
        panel->SetMargin(Thickness(16.0f));
        auto* t = panel->Add(std::make_unique<TextBlock>());
        t->SetText(L"Content of Tab " + std::to_wstring(s->nextId));
        s->tabs->AddTab(L"Tab " + std::to_wstring(s->nextId), std::move(panel));
        s->status->SetText(L"Added Tab " + std::to_wstring(s->nextId));
        s->nextId++;
    });

    // 删除最后一个标签
    pageSubs_ += btnRemove->Click().Subscribe(state, [](void* owner, Button&, RoutedEventArgs&) {
        auto* s = static_cast<TabState*>(owner);
        int count = s->tabs->TabCount();
        if (count > 0) {
            int lastIndex = count - 1;
            auto header = s->tabs->HeaderAt(lastIndex);
            s->tabs->RemoveTab(lastIndex);
            s->status->SetText(L"Removed " + header);
        } else {
            s->status->SetText(L"No tabs to remove");
        }
    });

    // 关闭按钮事件
    pageSubs_ += tabs->TabCloseRequested().Subscribe(state, [](void* owner, TabControl&, TabControl::TabCloseRequestedArgs& e) {
        auto* s = static_cast<TabState*>(owner);
        auto header = s->tabs->HeaderAt(e.index);
        s->tabs->RemoveTab(e.index);
        s->status->SetText(L"Closed " + header + L" via × button");
    });

    CreateCodeExample(content, LR"(// 添加标签
auto content = std::make_unique<StackPanel>();
tabs->AddTab(L"New Tab", std::move(content));

// 删除标签
tabs->RemoveTab(index);

// 关闭按钮事件
tabs->TabCloseRequested().Subscribe(owner, [](void*, TabControl& t, auto& e) {
    t.RemoveTab(e.index);  // 或先提示保存
});)");

    // 示例 2：Tab placement
    auto* card2 = CreateExampleCard(content, L"Tab Placement");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"TabControl supports four placements: Top (default), Bottom, Left, Right.");
    desc2->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* placementPanel = card2->Add(std::make_unique<StackPanel>());
    placementPanel->SetOrientation(StackPanel::Orientation::Horizontal);
    placementPanel->SetSpacing(16.0f);

    // Top
    auto* topTabs = placementPanel->Add(std::make_unique<TabControl>());
    topTabs->SetWidth(200.0f); topTabs->SetHeight(150.0f);
    topTabs->SetTabStripPlacement(TabStripPlacement::Top);
    for (int i = 1; i <= 3; ++i) {
        auto p = std::make_unique<StackPanel>();
        p->SetMargin(Thickness(12.0f));
        auto* t = p->Add(std::make_unique<TextBlock>());
        t->SetText(L"Top " + std::to_wstring(i));
        topTabs->AddTab(L"T" + std::to_wstring(i), std::move(p));
    }

    // Bottom
    auto* bottomTabs = placementPanel->Add(std::make_unique<TabControl>());
    bottomTabs->SetWidth(200.0f); bottomTabs->SetHeight(150.0f);
    bottomTabs->SetTabStripPlacement(TabStripPlacement::Bottom);
    for (int i = 1; i <= 3; ++i) {
        auto p = std::make_unique<StackPanel>();
        p->SetMargin(Thickness(12.0f));
        auto* t = p->Add(std::make_unique<TextBlock>());
        t->SetText(L"Bottom " + std::to_wstring(i));
        bottomTabs->AddTab(L"B" + std::to_wstring(i), std::move(p));
    }

    // Left
    auto* leftTabs = placementPanel->Add(std::make_unique<TabControl>());
    leftTabs->SetWidth(200.0f); leftTabs->SetHeight(150.0f);
    leftTabs->SetTabStripPlacement(TabStripPlacement::Left);
    for (int i = 1; i <= 3; ++i) {
        auto p = std::make_unique<StackPanel>();
        p->SetMargin(Thickness(12.0f));
        auto* t = p->Add(std::make_unique<TextBlock>());
        t->SetText(L"Left " + std::to_wstring(i));
        leftTabs->AddTab(L"L" + std::to_wstring(i), std::move(p));
    }

    // Right
    auto* rightTabs = placementPanel->Add(std::make_unique<TabControl>());
    rightTabs->SetWidth(200.0f); rightTabs->SetHeight(150.0f);
    rightTabs->SetTabStripPlacement(TabStripPlacement::Right);
    for (int i = 1; i <= 3; ++i) {
        auto p = std::make_unique<StackPanel>();
        p->SetMargin(Thickness(12.0f));
        auto* t = p->Add(std::make_unique<TextBlock>());
        t->SetText(L"Right " + std::to_wstring(i));
        rightTabs->AddTab(L"R" + std::to_wstring(i), std::move(p));
    }

    CreateCodeExample(content, LR"(// 设置 Tab 位置
tabs->SetTabStripPlacement(TabStripPlacement::Top);     // 默认，顶部
tabs->SetTabStripPlacement(TabStripPlacement::Bottom);  // 底部
tabs->SetTabStripPlacement(TabStripPlacement::Left);    // 左侧
tabs->SetTabStripPlacement(TabStripPlacement::Right);   // 右侧)");

    return std::move(page);
}

} // namespace fluent
