// GalleryMain.h — 阶段 F Gallery 重写的主入口
//
// 架构：左侧 TreeView 导航（220 DIP 固定宽度）+ 右侧详情页容器（Grid）。
// 页面切换：TreeView SelectionChanged → 按 row id 查表 → 旧页 SetVisible(false) + 新页 SetVisible(true)。
//
// 页面组织：
//   - 每个详情页是一个 ScrollPanel（包含一个 StackPanel），包含：
//     * 标题（TextBlock，FontSize=24）
//     * 示例区（多个 Border 卡片，每个卡片一个功能演示）
//     * 代码示例区（只读 TextArea，显示 C++ 代码）
//   - 所有详情页预创建，存储在 std::unordered_map<int, ScrollPanel*>
//   - 所有页面始终 Attached 在容器内，通过 SetVisible 切换显示（SetVisible(false) 从布局中移除）
//   - 每个页面的 ScrollPanel 独立保持自己的滚动位置
//
// TreeView 节点结构：
//   id=1xx：基础控件分组（depth=0 父节点）
//   id=101/102/...：具体控件（depth=1 子节点）
//   id=2xx：输入控件分组
//   id=3xx：文本控件分组
//   id=4xx：布局容器分组
//   id=5xx：数据控件分组
//   id=6xx：对话框/窗口分组
//   id=7xx：性能演示分组

#pragma once
#include "../FluentUI/controls/TreeView.h"
#include "../FluentUI/controls/MenuFlyout.h"
#include "../FluentUI/controls/Button.h"
#include "../FluentUI/layout/Panel.h"
#include "../FluentUI/core/Subscription.h"
#include "../FluentUI/base/Event.h"
#include "../FluentUI/text/LogSink.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fluent {

class TextArea;
class ScrollPanel;
class StackPanel;
class TextBlock;
class Grid;
class NativeWindowHost;
class ContentDialog;
class TextBox;
class InfoBar;
class Metric;
class DataGrid;
class Chart;
class Hyperlink;
class ProgressBar;
class ToggleSwitch;
class CheckBox;
class Slider;
class ComboBox;
class NumericUpDown;
class Rating;

// 五个强调色样本，被所有 AccentColor 演示复用，放一处以免各页给出不同的五个。
struct AccentSwatch {
    const wchar_t* label;
    UINT32 color;
};

class GalleryApp {
public:
    GalleryApp();
    // Out-of-line so unique_ptr<ContentDialog> only needs the complete type in the
    // .cpp — the header keeps ContentDialog (and with it DialogWindow / Window) as
    // a forward declaration.
    ~GalleryApp();

    // 主题切换回调：GalleryApp 拿不到窗口（UIElement::Window() 是 protected，
    // 且 SetDarkMode 在 NativeWindowHost 上而不在 WindowServices 接口上），所以由
    // 宿主窗口注入。参数是"要不要暗色"，返回值是"切换后是否为暗色"。
    using ThemeToggleFn = std::function<bool(bool wantDark)>;
    using IsDarkFn = std::function<bool()>;
    void SetThemeHooks(ThemeToggleFn toggle, IsDarkFn isDark) {
        themeToggle_ = std::move(toggle);
        isDark_ = std::move(isDark);
    }

    // 把工作投递到 UI 线程执行（Application::Post）。日志演示的后台生产者
    // 需要它把 LogSink 的数据交回 UI 线程 —— GalleryApp 本身拿不到 Application。
    using PostFn = std::function<void(std::function<void()>)>;
    void SetUiPost(PostFn post) { postToUi_ = std::move(post); }

    // 宿主窗口自身的引用，供对话框演示使用。
    //
    // 为什么必须由宿主注入：DialogWindow::ShowDialog / Show 和
    // MessageDialog::Show 的第一个参数是 NativeWindowHost&（要禁用/恢复 owner、
    // 继承 owner 的主题配置、拿 owner 的 Application 加入同一帧循环）。
    // UIElement 只能通过 Context().window 看到 WindowServices 接口，那上面
    // 没有也不应该有"我是哪个 NativeWindowHost"，所以和 SetThemeHooks 一样，
    // 由 GalleryWindow 在 OnInitialize 里把 *this 交进来。
    using OwnerWindowFn = std::function<NativeWindowHost*()>;
    void SetOwnerWindowProvider(OwnerWindowFn fn) { ownerWindow_ = std::move(fn); }

    // 构建完整的 Gallery 窗口内容树：左侧导航 + 右侧详情页容器
    std::unique_ptr<Panel> BuildContent();

private:
    // 构建左侧 TreeView 的节点数据（7 大分类 + 31 个控件）
    std::vector<TreeViewRow> BuildNavigationTree();

    // TreeView SelectionChanged 回调：切换右侧详情页
    void OnNavigationChanged(TreeView& tree, const TreeSelection& sel);

    // 详情页工厂（启动时预创建全部页面，返回 unique_ptr，容器持有所有权）
    std::unique_ptr<ScrollPanel> CreatePageForId(int id);

    // 启动时构建全部详情页并加入容器，初始全部隐藏（见 pages_ 的注释）
    void BuildAllPages();

    // 详情页工厂（每个控件一个 CreateXxxPage 方法）
    std::unique_ptr<ScrollPanel> CreateButtonPage();
    std::unique_ptr<ScrollPanel> CreateCheckBoxPage();
    std::unique_ptr<ScrollPanel> CreateRadioButtonPage();
    std::unique_ptr<ScrollPanel> CreateToggleSwitchPage();
    std::unique_ptr<ScrollPanel> CreateTextBoxPage();
    std::unique_ptr<ScrollPanel> CreatePasswordBoxPage();
    std::unique_ptr<ScrollPanel> CreateTextBlockPage();
    std::unique_ptr<ScrollPanel> CreateHyperlinkPage();
    std::unique_ptr<ScrollPanel> CreateImagePage();
    std::unique_ptr<ScrollPanel> CreateRepeatButtonPage();
    std::unique_ptr<ScrollPanel> CreateSeparatorPage();

    std::unique_ptr<ScrollPanel> CreateTextAreaPage();
    std::unique_ptr<ScrollPanel> CreateTreeViewPage();
    std::unique_ptr<ScrollPanel> CreateCalendarPage();
    std::unique_ptr<ScrollPanel> CreateDatePickerPage();
    std::unique_ptr<ScrollPanel> CreateSliderPage();
    std::unique_ptr<ScrollPanel> CreateComboBoxPage();
    std::unique_ptr<ScrollPanel> CreateListBoxPage();
    std::unique_ptr<ScrollPanel> CreateNumericUpDownPage();
    std::unique_ptr<ScrollPanel> CreateRatingPage();

    std::unique_ptr<ScrollPanel> CreateProgressBarPage();
    std::unique_ptr<ScrollPanel> CreateTabControlPage();
    std::unique_ptr<ScrollPanel> CreateMenuFlyoutPage();
    std::unique_ptr<ScrollPanel> CreateMenuBarPage();
    std::unique_ptr<ScrollPanel> CreateStatusBarPage();
    std::unique_ptr<ScrollPanel> CreateToolBarPage();

    std::unique_ptr<ScrollPanel> CreateGridPage();
    std::unique_ptr<ScrollPanel> CreateStackPanelPage();
    std::unique_ptr<ScrollPanel> CreateWrapPanelPage();
    std::unique_ptr<ScrollPanel> CreateDockPanelPage();
    std::unique_ptr<ScrollPanel> CreateBorderPage();
    std::unique_ptr<ScrollPanel> CreateCanvasPage();
    std::unique_ptr<ScrollPanel> CreateScrollViewerPage();
    std::unique_ptr<ScrollPanel> CreateExpanderPage();
    // 新控件的独立展示页（pages/InfoBarMetricPages.cpp、pages/DataGridChartPages.cpp）。
    std::unique_ptr<ScrollPanel> CreateInfoBarPage();
    std::unique_ptr<ScrollPanel> CreateMetricPage();
    std::unique_ptr<ScrollPanel> CreateDataGridPage();
    std::unique_ptr<ScrollPanel> CreateChartPage();
    // 自我调试页：用本框架的控件检查本框架的运行状态（pages/InspectorPage.cpp）。
    std::unique_ptr<ScrollPanel> CreateInspectorPage();
    // A-share analysis tool UI replica (see pages/StockAnalyzerPage.cpp).
    std::unique_ptr<ScrollPanel> CreateStockAnalyzerPage();
    std::unique_ptr<ScrollPanel> CreateUniformGridPage();
    std::unique_ptr<ScrollPanel> CreateViewboxPage();
    std::unique_ptr<ScrollPanel> CreateDynamicListPage();
    std::unique_ptr<ScrollPanel> CreateGroupBoxPage();

    std::unique_ptr<ScrollPanel> CreateContentDialogPage();
    std::unique_ptr<ScrollPanel> CreateMessageDialogPage();
    std::unique_ptr<ScrollPanel> CreateGridSplitterPage();

    // 跨控件的样式话题（pages/BorderThemePages.cpp）。
    //
    // 这两个留在单独的分类里，因为它们不属于任何一个控件：一个是「同一批属性在
    // 多个控件上的效果对照」，一个是「主题 token 与手工覆盖的取舍」。
    //
    // 曾经和它们并列的 CheckBox/RadioButton/TextBox/TextArea/Border 五个「完整演示」
    // 页已经并回各控件自己的页面（102/103/201/302/505）——它们本来就是基础页的
    // 超集，一个控件分两页展示只会让人不知道该看哪一页。
    std::unique_ptr<ScrollPanel> CreateThemeVsCustomPage();

    // 对话框演示的动作。成员函数而不是内联 lambda：Event 的 handler 是无捕获
    // 函数指针，回调只拿到 void* owner，真正的逻辑必须挂在 GalleryApp 上。
    void ShowModalContentDialog();
    void ShowModelessContentDialog();
    void ShowMessageDialogOk();
    void ShowMessageDialogOkCancel();
    void ShowMessageDialogYesNo();

    // 性能演示页
    std::unique_ptr<ScrollPanel> CreateVirtualizationDemoPage();  // TextArea/TreeView 虚拟化对比
    std::unique_ptr<ScrollPanel> CreateLogDemoPage();             // TextArea 日志追尾
    std::unique_ptr<ScrollPanel> CreateThemeDemoPage();           // 主题切换
    std::unique_ptr<ScrollPanel> CreateAnimationDemoPage();       // 动画流畅度
    std::unique_ptr<ScrollPanel> CreateStressTestPage();          // 非虚拟化容器压力测试

    // 一个详情页的外壳：ScrollPanel（所有权 + 滚动条）里放一个 StackPanel（内容）。
    // 滚动位置天然是每页独立的 —— 它是各自 ScrollPanel 的成员，所以切页不需要
    // 保存/恢复任何东西。
    struct PageShell {
        std::unique_ptr<ScrollPanel> page;
        StackPanel* content;  // 调用方往这里 Add 卡片
    };

    // 辅助：创建详情页外壳（滚动容器 + 内边距 + 页面标题）
    PageShell CreatePageShell(std::wstring title);

    // 辅助：创建示例卡片（Border 包裹，标题 + 内容）。
    // 返回卡片内部的 StackPanel —— 调用方直接往它里面 Add 控件。
    StackPanel* CreateExampleCard(Panel* parent, std::wstring title);

    // --- 展示页共用构件（pages/ShowcaseHelpers.cpp）-----------------------
    //
    // 每个控件页要回答四个问题：有哪些状态、在布局里怎么表现（拉伸/定尺寸/
    // Min-Max/对齐）、能改哪些样式、和别的控件放一起什么样。后三个以前在单独的
    // 「样式定制大全」里，等于把一个控件拆到导航的两个地方；并回来之后，重复的
    // 卡片形状放这里，否则同样的代码写二十遍必然互相漂移。
    TextBlock*  AddNote(Panel* parent, std::wstring text);
    StackPanel* AddSubSection(Panel* parent, std::wstring label);
    // 画出边界的定宽容器：在 2000 DIP 宽的页面上，「拉伸」和「宽 120」看起来
    // 一模一样，必须把容器的边画出来才看得出区别。
    StackPanel* AddBoundedBox(Panel* parent, float width, std::wstring caption = {});
    StackPanel* AddRow(Panel* parent, float spacing = 8.0f);
    // 等分 Star 列（不是 UniformGrid —— 它按最大子元素的 desired 定 cell，
    // 不会撑满 arrange 宽度）。行高用 Auto，写死行高正是之前切掉文字的原因。
    Grid*       AddEqualColumns(Panel* parent, int columns);
    static const AccentSwatch* AccentSwatches(int& count);

    // 辅助：创建代码示例区（只读 TextArea，等宽字体）
    TextArea* CreateCodeExample(Panel* parent, std::wstring code);

    // 右侧容器：Grid 单元格（0,0），持有全部详情页（所有权），同时只有一个可见。
    // Grid 而非 StackPanel，因为每个页面都需要铺满可用空间（1* row × 1* column），
    // 而不是按 desired size 堆叠。
    Grid* detailContainer_ = nullptr;

    // 页面 id -> 页面指针（弱引用，所有权在 detailContainer_）。
    //
    // 为什么全部预创建而不是按需创建：按需创建会在每次切换时销毁旧页，用户在
    // TextBox 里输入的文字、Slider 拖到的位置、CheckBox 的勾选状态全部丢失 ——
    // 切回去看到的是一个重置过的页面，这不是任何人对"换个标签页"的预期。
    // 附带收益是启动即压力测试：31 个页面、上百个控件同时挂在一棵树上，
    // 布局/失效/主题切换的成本立刻暴露，而不是每次只测一个页面的理想情况。
    //
    // 代价是启动时构建全部页面。这一项是可接受的：页面构建是纯 CPU 的控件
    // 树搭建，没有设备资源，而 SetVisible(false) 的页面既不参与 Measure/Arrange
    // 也不 Render（见 SetVisibleLayoutTests），所以常驻成本只是内存。
    std::unordered_map<int, ScrollPanel*> pages_;

    // 导航订阅（RAII —— 必须持有，否则订阅立即失效）
    Subscription navSub_;

    // 页面内控件的订阅（Button Click、TreeView SelectionChanged 等）。
    // 页面预创建后不再销毁，所以这些订阅和 GalleryApp 同生命期 —— 不再随
    // 页面切换注销。
    SubscriptionBag pageSubs_;

    // 当前选中页的 id
    int currentPageId_ = -1;

    // 主题切换按钮（点击时要改自己的图标，所以存指针）
    Button* themeToggleBtn_ = nullptr;
    // 主题按钮的订阅：按钮和 GalleryApp 同生命期，所以不放 pageSubs_
    Subscription themeSub_;

    // Button 页"点击事件"示例的状态。Event 的 handler 是无捕获函数指针，
    // 状态只能挂在 owner 指向的对象上 —— 就是 GalleryApp 自己。
    TextBlock* clickResultText_ = nullptr;
    int clickCount_ = 0;

    // TreeView 页"动态加载"示例的状态，同上。
    struct TreeContext {
        TreeView* tree = nullptr;
        int nextId = 2;
        bool loaded = false;
    };
    TreeContext treeContext_;

    // 宿主注入的主题钩子（见 SetThemeHooks）
    ThemeToggleFn themeToggle_;
    IsDarkFn isDark_;
    PostFn postToUi_;
    // --- 新控件展示页的交互状态 ---------------------------------------
    // 这些页面要演示「事件真的会触发」，所以必须持有 Subscription：它是 RAII 的，
    // 丢掉返回值等于立刻退订，处理器再也不会被调用。
    ProgressBar* sliderMirrorBar_ = nullptr;
    // ComboBox 页「混合使用」的导出面板：三个控件共同决定一句摘要，而 handler 是
    // 无捕获函数指针，只拿到一个 void* owner —— 所以参与计算的控件必须在这里留名，
    // 由 owner（GalleryApp 自己）转手取到，而不是被 lambda 捕获。
    ComboBox* comboMixFormat_ = nullptr;
    ComboBox* comboMixQuality_ = nullptr;
    CheckBox* comboMixMeta_ = nullptr;
    TextBlock* comboMixSummary_ = nullptr;
    // NumericUpDown 页「混合使用」的下单小票：数量 × 单价（可选含税）→ 合计。
    // 三个输入任意一个变化都要重算同一个结果，所以结果控件也必须是成员。
    NumericUpDown* numMixQty_ = nullptr;
    NumericUpDown* numMixPrice_ = nullptr;
    CheckBox* numMixTax_ = nullptr;
    TextBlock* numMixTotal_ = nullptr;
    // Rating 页「混合使用」的评价卡：一个评分值同时驱动结论文字和满意度条，
    // 演示一个值源可以有多个消费者。
    Rating* ratingMixStars_ = nullptr;
    TextBlock* ratingMixText_ = nullptr;
    ProgressBar* ratingMixBar_ = nullptr;
    Slider* toggleGroupSlider_ = nullptr;
    CheckBox* toggleGroupCheck_ = nullptr;
    Button* toggleGroupButton_ = nullptr;
    TextBlock* hyperlinkLog_ = nullptr;
    int hyperlinkClicks_ = 0;
    TextBlock* infoBarCloseLog_ = nullptr;
    int infoBarCloseCount_ = 0;
    Subscription infoBarSub_;
    Subscription infoBarWidthSub_;
    TextBox* formInput_ = nullptr;
    InfoBar* formFeedback_ = nullptr;
    Subscription formSubmitSub_;
    Metric* metricLive_ = nullptr;
    Subscription metricLiveSub_;
    DataGrid* gridDemo_ = nullptr;
    TextBlock* gridStatus_ = nullptr;
    SubscriptionBag gridSubs_;
    Chart* chartDemo_ = nullptr;
    TextBlock* chartStatus_ = nullptr;
    SubscriptionBag chartSubs_;
    // 自我调试页
    TextBlock* inspectorOut_ = nullptr;
    Panel* inspectorTarget_ = nullptr;
    SubscriptionBag inspectorSubs_;

    OwnerWindowFn ownerWindow_;

    // 日志演示的共享状态。shared_ptr 而不是普通成员：后台线程按值持有它，
    // 所以即使 GalleryApp 先走一步，线程读 running 退出时也不会碰到已释放的
    // 内存。页面现在是预创建且常驻的，所以 area 在 GalleryApp 存活期间始终
    // 有效 —— 但 shared_ptr 仍是必要的，线程的退出时机不由 UI 线程决定。
    struct LogDemoState {
        LogSink sink;
        TextArea* area = nullptr;
        std::atomic<bool> running{false};
        int counter = 1;
    };
    std::shared_ptr<LogDemoState> logState_;

    // 日志页的"高速追加/停止追加"按钮。导航离开时生产线程会被停掉，按钮的文字
    // 必须跟着回到"高速追加" —— 否则切回来看到的是一个写着"停止追加"、但其实
    // 什么都没在跑的按钮，再点一下才会真正开始。
    Button* logAppendBtn_ = nullptr;

    // MenuFlyout 持有者（shared_ptr 保持生命期，避免 SetFlyout 的原始指针悬空）
    std::vector<std::shared_ptr<MenuFlyout>> flyouts_;

    // 非模态对话框演示的实例。
    //
    // 模态的可以是栈变量（ShowDialog 直到关闭才返回，对象在整个可见期都活着），
    // 非模态的不行：Show() 立刻返回，栈上的对话框会在返回时析构，窗口随即消失。
    // 所以它必须活得比调用点长 —— 挂在 GalleryApp 上。
    // 每次点击"打开非模态对话框"都会 reset 这个成员，所以同时最多一个。
    std::unique_ptr<ContentDialog> modelessDialog_;

    // 对话框演示的状态显示。两个页面各一个文本，显示对话框返回的结果。
    TextBlock* dialogResultText_ = nullptr;
    TextBlock* messageResultText_ = nullptr;
};

} // namespace fluent
