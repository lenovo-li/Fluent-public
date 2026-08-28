// GalleryMain.cpp — 核心框架逻辑（导航 + 页面切换）
#include "GalleryMain.h"
#include "../FluentUI/layout/DockPanel.h"
#include "../FluentUI/layout/Grid.h"
#include "../FluentUI/layout/StackPanel.h"
#include "../FluentUI/layout/Border.h"
#include "../FluentUI/layout/ScrollPanel.h"
#include "../FluentUI/controls/TextBlock.h"
#include "../FluentUI/controls/TextArea.h"
#include "../FluentUI/controls/Button.h"
#include "../FluentUI/controls/ContentDialog.h"

namespace fluent {

// Both defined out-of-line so unique_ptr<ContentDialog>'s destructor is
// instantiated here, where ContentDialog is complete — the header only forward
// declares it (see GalleryMain.h).
GalleryApp::GalleryApp() = default;
GalleryApp::~GalleryApp() = default;

std::unique_ptr<Panel> GalleryApp::BuildContent() {
    // 根布局：DockPanel（标题栏 Top + Grid 填充）
    auto root = std::make_unique<DockPanel>();

    // 标题栏：48 DIP 高，左侧标题 + 右侧主题切换按钮
    auto* titleBar = root->Add(std::make_unique<StackPanel>());
    titleBar->SetOrientation(StackPanel::Orientation::Horizontal);
    titleBar->SetHeight(48.0f);
    DockPanel::SetDock(titleBar, Dock::Top);

    auto* titleText = titleBar->Add(std::make_unique<TextBlock>());
    titleText->SetText(L"  FluentUI Gallery");
    titleText->SetTypographyRole(TypographyRole::Subtitle);
    titleText->SetVAlign(VAlign::Center);
    titleText->SetHitTestVisible(false);  // 允许通过文字拖动标题栏

    // 右侧弹簧（把主题按钮推到最右）
    auto* spacer = titleBar->Add(std::make_unique<Border>());
    spacer->SetHAlign(HAlign::Stretch);

    // 主题切换按钮（最右侧）
    themeToggleBtn_ = titleBar->Add(std::make_unique<Button>());
    themeToggleBtn_->SetMargin(Thickness{ 12.0f, 12.0f, 12.0f, 12.0f });
    if (isDark_ && isDark_()) {
        themeToggleBtn_->SetText(L"☀️");
    } else {
        themeToggleBtn_->SetText(L"🌙");
    }
    themeSub_ = themeToggleBtn_->Click().Subscribe(this, [](void* owner, Button&, RoutedEventArgs&) {
        auto* app = static_cast<GalleryApp*>(owner);
        if (!app->themeToggle_ || !app->isDark_) return;

        const bool currentlyDark = app->isDark_();
        app->themeToggle_(!currentlyDark);
        const bool nowDark = app->isDark_();
        app->themeToggleBtn_->SetText(nowDark ? L"☀️" : L"🌙");
    });

    // 内容区：Grid 2 列（固定 220 DIP + 剩余空间）
    auto* contentGrid = root->Add(std::make_unique<Grid>());
    contentGrid->SetColumns({GridLength::Pixels(220.0f), GridLength::Star()});
    contentGrid->SetRows({GridLength::Star()});

    // 左侧：TreeView 导航
    auto* nav = contentGrid->Add(std::make_unique<TreeView>());
    contentGrid->SetCell(nav, 0, 0);
    nav->SetMargin(Thickness{12.0f, 12.0f, 12.0f, 12.0f});
    nav->SetRows(BuildNavigationTree());
    navSub_ = nav->SelectionChanged().Subscribe(
        this, [](void* owner, TreeView& tree, TreeSelection& sel) {
            static_cast<GalleryApp*>(owner)->OnNavigationChanged(tree, sel);
        });

    // 右侧：详情页容器
    auto* pageHost = contentGrid->Add(std::make_unique<Grid>());
    contentGrid->SetCell(pageHost, 0, 1);
    pageHost->SetColumns({GridLength::Star()});
    pageHost->SetRows({GridLength::Star()});
    detailContainer_ = pageHost;

    BuildAllPages();
    nav->SetSelectedIndex(1);  // 默认选中第一个控件页（Button）

    return root;
}

void GalleryApp::BuildAllPages() {
    static constexpr int kPageIds[] = {
        101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,  // 基础控件
        201, 208, 202, 203, 204, 205, 206, 207,   // 输入控件
        301, 302,                            // 文本控件
        401, 402, 403, 404, 405, 406, 407,  // 数据控件（含四个新控件）
        501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 512,   // 布局容器（512 新增）
        601, 602, 603, 604, 605, 606, 607, 608,  // 窗口 & 对话框
        701, 702, 703, 704, 705, 707,        // 性能演示 + 跨控件主题话题（707）
        // 801-807 与 706 已撤销：它们都是各控件页的重复。801-804 是 102/103/201/302 的
        // 超集，706「控件样式定制」是 Button/CheckBox/RadioButton/ToggleSwitch/Slider 的
        // 样式演示汇总 —— 现在这些内容都在各控件自己的「样式定制」卡片里。一个控件出现在
        // 两个地方只会让人不知道该看哪页。707 保留：它讲的是「主题驱动 vs 固定色」这个
        // 跨控件取舍，不属于任何单个控件。
        901, 902,                            // 真实应用复刻 + 自我调试
    };

    for (int id : kPageIds) {
        auto page = CreatePageForId(id);
        if (!page) continue;
        ScrollPanel* raw = detailContainer_->Add(std::move(page));
        detailContainer_->SetCell(raw, 0, 0);
        raw->SetVisible(false);
        pages_[id] = raw;
    }
}

std::vector<TreeViewRow> GalleryApp::BuildNavigationTree() {
    std::vector<TreeViewRow> rows;

    rows.push_back({100, -1, L"基础控件", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({101, 100, L"Button", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({102, 100, L"CheckBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({103, 100, L"RadioButton", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({104, 100, L"ToggleSwitch", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({105, 100, L"Hyperlink", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({106, 100, L"Image", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({107, 100, L"RepeatButton", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({108, 100, L"Separator", 1, false, false, true, TreeViewIcon::None});

    rows.push_back({200, -1, L"输入控件", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({201, 200, L"TextBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({208, 200, L"PasswordBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({202, 200, L"Slider", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({203, 200, L"ComboBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({204, 200, L"DatePicker", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({205, 200, L"Calendar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({206, 200, L"NumericUpDown", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({207, 200, L"Rating", 1, false, false, true, TreeViewIcon::None});

    rows.push_back({300, -1, L"文本控件", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({301, 300, L"TextBlock", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({302, 300, L"TextArea", 1, false, false, true, TreeViewIcon::None});

    rows.push_back({400, -1, L"数据控件", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({401, 400, L"ListBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({402, 400, L"TreeView", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({403, 400, L"ProgressBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({404, 400, L"DataGrid", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({405, 400, L"Chart", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({406, 400, L"InfoBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({407, 400, L"Metric", 1, false, false, true, TreeViewIcon::None});

    rows.push_back({500, -1, L"布局容器", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({501, 500, L"Grid", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({502, 500, L"StackPanel", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({503, 500, L"WrapPanel", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({504, 500, L"DockPanel", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({505, 500, L"Border", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({506, 500, L"Canvas", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({507, 500, L"ScrollViewer", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({508, 500, L"Expander", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({509, 500, L"UniformGrid", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({510, 500, L"Viewbox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({511, 500, L"GroupBox", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({512, 500, L"动态列表 (RemoveAt/InsertAt)", 1, false, false, true, TreeViewIcon::None});

    rows.push_back({600, -1, L"窗口 & 对话框", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({601, 600, L"TabControl", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({602, 600, L"MenuBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({603, 600, L"MenuFlyout", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({604, 600, L"ToolBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({605, 600, L"StatusBar", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({606, 600, L"ContentDialog", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({607, 600, L"MessageDialog", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({608, 600, L"GridSplitter", 1, false, false, true, TreeViewIcon::None});

    rows.push_back({700, -1, L"性能演示", 0, false, true, true, TreeViewIcon::Warning});
    rows.push_back({701, 700, L"虚拟化对比", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({702, 700, L"日志追尾", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({703, 700, L"主题切换", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({704, 700, L"动画流畅度", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({705, 700, L"压力测试", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({707, 700, L"主题 vs 自定义", 1, false, false, true, TreeViewIcon::None});


    // 真实应用复刻：验证这套控件能否表达一个数据分析应用的界面。
    rows.push_back({900, -1, L"\U0001F4C8 真实应用复刻", 0, false, true, true, TreeViewIcon::Folder});
    rows.push_back({901, 900, L"A股分析工具", 1, false, false, true, TreeViewIcon::None});
    rows.push_back({902, 900, L"自我调试 · Inspector", 1, false, false, true, TreeViewIcon::None});

    return rows;
}

void GalleryApp::OnNavigationChanged(TreeView& tree, const TreeSelection& sel) {
    if (!sel.row || sel.row->depth == 0) return;

    const int newPageId = sel.row->id;
    if (newPageId == currentPageId_) return;

    if (currentPageId_ != -1) {
        auto it = pages_.find(currentPageId_);
        if (it != pages_.end() && it->second) {
            it->second->SetVisible(false);
        }
    }

    if (logState_ && logState_->running.exchange(false) && logAppendBtn_) {
        logAppendBtn_->SetText(L"高速追加 (10k 行/秒)");
    }

    auto it = pages_.find(newPageId);
    if (it != pages_.end() && it->second) {
        it->second->SetVisible(true);
        currentPageId_ = newPageId;
        // Label the resize traces with the page that is actually on screen. The
        // TreeView row text IS the page name, so there is nothing to map — and
        // nothing to keep in sync when a page is added.
        if (ownerWindow_) {
            if (NativeWindowHost* win = ownerWindow_()) {
                win->SetResizeTraceLabel(sel.row->text);
            }
        }
    }
}

std::unique_ptr<ScrollPanel> GalleryApp::CreatePageForId(int id) {
    switch (id) {
        case 101: return CreateButtonPage();
        case 102: return CreateCheckBoxPage();
        case 103: return CreateRadioButtonPage();
        case 104: return CreateToggleSwitchPage();
        case 105: return CreateHyperlinkPage();
        case 106: return CreateImagePage();
        case 107: return CreateRepeatButtonPage();
        case 108: return CreateSeparatorPage();

        case 201: return CreateTextBoxPage();
        case 208: return CreatePasswordBoxPage();
        case 202: return CreateSliderPage();
        case 203: return CreateComboBoxPage();
        case 204: return CreateDatePickerPage();
        case 205: return CreateCalendarPage();
        case 206: return CreateNumericUpDownPage();
        case 207: return CreateRatingPage();

        case 301: return CreateTextBlockPage();
        case 302: return CreateTextAreaPage();

        case 401: return CreateListBoxPage();
        case 402: return CreateTreeViewPage();
        case 403: return CreateProgressBarPage();
        case 404: return CreateDataGridPage();
        case 405: return CreateChartPage();
        case 406: return CreateInfoBarPage();
        case 407: return CreateMetricPage();

        case 501: return CreateGridPage();
        case 502: return CreateStackPanelPage();
        case 503: return CreateWrapPanelPage();
        case 504: return CreateDockPanelPage();
        case 505: return CreateBorderPage();
        case 506: return CreateCanvasPage();
        case 507: return CreateScrollViewerPage();
        case 508: return CreateExpanderPage();
        case 509: return CreateUniformGridPage();
        case 510: return CreateViewboxPage();
        case 511: return CreateGroupBoxPage();
        case 512: return CreateDynamicListPage();
        case 901: return CreateStockAnalyzerPage();
        case 902: return CreateInspectorPage();

        case 601: return CreateTabControlPage();
        case 602: return CreateMenuBarPage();
        case 603: return CreateMenuFlyoutPage();
        case 604: return CreateToolBarPage();
        case 605: return CreateStatusBarPage();
        case 606: return CreateContentDialogPage();
        case 607: return CreateMessageDialogPage();
        case 608: return CreateGridSplitterPage();

        case 701: return CreateVirtualizationDemoPage();
        case 702: return CreateLogDemoPage();
        case 703: return CreateThemeDemoPage();
        case 704: return CreateAnimationDemoPage();
        case 705: return CreateStressTestPage();

        case 707: return CreateThemeVsCustomPage();

        default: return nullptr;
    }
}

// ============================================================================
// 辅助方法
// ============================================================================

GalleryApp::PageShell GalleryApp::CreatePageShell(std::wstring title) {
    auto scroll = std::make_unique<ScrollPanel>();
    scroll->SetPadding(24.0f);

    auto* content = scroll->Add(std::make_unique<StackPanel>());
    content->SetOrientation(StackPanel::Orientation::Vertical);
    content->SetSpacing(0);

    auto* titleBlock = content->Add(std::make_unique<TextBlock>());
    titleBlock->SetText(std::move(title));
    titleBlock->SetFontSize(32.0f);
    titleBlock->SetMargin(Thickness{0, 0, 0, 24.0f});

    return {std::move(scroll), content};
}

StackPanel* GalleryApp::CreateExampleCard(Panel* parent, std::wstring title) {
    auto* card = parent->Add(std::make_unique<Border>());
    card->SetPadding(Thickness{16.0f});
    card->SetMargin(Thickness{0, 0, 0, 16.0f});
    card->SetBorderThickness(1.0f);
    card->SetCornerRadius(8.0f);

    auto* content = card->SetChild(std::make_unique<StackPanel>());
    content->SetOrientation(StackPanel::Orientation::Vertical);
    content->SetSpacing(8.0f);

    auto* titleBlock = content->Add(std::make_unique<TextBlock>());
    titleBlock->SetText(std::move(title));
    titleBlock->SetFontSize(16.0f);

    return content;
}

TextArea* GalleryApp::CreateCodeExample(Panel* parent, std::wstring code) {
    auto* codeBlock = parent->Add(std::make_unique<TextArea>());
    codeBlock->SetText(std::move(code));
    codeBlock->SetReadOnly(true);
    codeBlock->SetWrapMode(TextWrapMode::NoWrap);
    codeBlock->SetHeight(200.0f);
    codeBlock->SetMargin(Thickness{0, 16.0f, 0, 0});
    codeBlock->SetFontSize(13.0f);
    return codeBlock;
}

} // namespace fluent
