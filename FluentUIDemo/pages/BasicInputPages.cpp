// BasicInputPages.cpp — 基础输入控件页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/RepeatButton.h"
#include "../../FluentUI/controls/Separator.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/Hyperlink.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateButtonPage() {
    auto [page, content] = CreatePageShell(L"Button");

    // 示例 1：Button Kind 样式
    auto* card1 = CreateExampleCard(content, L"Button Kind");
    auto* btnRow1 = card1->Add(std::make_unique<StackPanel>());
    btnRow1->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow1->SetSpacing(12.0f);

    auto* btnStandard = btnRow1->Add(std::make_unique<Button>());
    btnStandard->SetText(L"Standard");
    btnStandard->SetTooltip(L"标准按钮样式");

    auto* btnAccent = btnRow1->Add(std::make_unique<Button>());
    btnAccent->SetText(L"Accent");
    btnAccent->SetKind(Button::Kind::Accent);
    btnAccent->SetTooltip(L"强调按钮样式，用于主要操作");

    auto* btnSubtle = btnRow1->Add(std::make_unique<Button>());
    btnSubtle->SetText(L"Subtle");
    btnSubtle->SetKind(Button::Kind::Subtle);
    btnSubtle->SetTooltip(L"柔和按钮样式，用于次要操作");

    // 示例 2：禁用状态
    auto* card2 = CreateExampleCard(content, L"禁用状态");
    auto* btnRow2 = card2->Add(std::make_unique<StackPanel>());
    btnRow2->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow2->SetSpacing(12.0f);

    auto* btnDisabled1 = btnRow2->Add(std::make_unique<Button>());
    btnDisabled1->SetText(L"Disabled Standard");
    btnDisabled1->SetEnabled(false);
    btnDisabled1->SetTooltip(L"禁用状态的标准按钮");

    auto* btnDisabled2 = btnRow2->Add(std::make_unique<Button>());
    btnDisabled2->SetText(L"Disabled Accent");
    btnDisabled2->SetKind(Button::Kind::Accent);
    btnDisabled2->SetEnabled(false);
    btnDisabled2->SetTooltip(L"禁用状态的强调按钮");

    // 示例 3：尺寸和边距
    auto* card3 = CreateExampleCard(content, L"尺寸和边距");
    auto* btnRow3 = card3->Add(std::make_unique<StackPanel>());
    btnRow3->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow3->SetSpacing(12.0f);

    auto* btnSmall = btnRow3->Add(std::make_unique<Button>());
    btnSmall->SetText(L"Small");
    btnSmall->SetWidth(80.0f);
    btnSmall->SetHeight(32.0f);
    btnSmall->SetTooltip(L"小尺寸按钮：80×32");

    auto* btnMedium = btnRow3->Add(std::make_unique<Button>());
    btnMedium->SetText(L"Medium");
    btnMedium->SetWidth(120.0f);
    btnMedium->SetTooltip(L"中等尺寸按钮：120×默认高度");

    auto* btnLarge = btnRow3->Add(std::make_unique<Button>());
    btnLarge->SetText(L"Large");
    btnLarge->SetWidth(160.0f);
    btnLarge->SetHeight(40.0f);
    btnLarge->SetTooltip(L"大尺寸按钮：160×40");

    // 示例 4：Flyout 集成
    auto* card4 = CreateExampleCard(content, L"Flyout 集成");
    auto* btnFlyout = card4->Add(std::make_unique<Button>());
    btnFlyout->SetText(L"打开菜单");
    btnFlyout->SetTooltip(L"点击显示上下文菜单");

    auto flyout = std::make_shared<MenuFlyout>();
    flyout->SetItems({
        MenuItem{L"复制", L"Ctrl+C", false, true, false, {}, []() {}},
        MenuItem{L"粘贴", L"Ctrl+V", false, true, false, {}, []() {}},
        MenuItem::Sep(),
        MenuItem{L"删除", L"Del", false, true, false, {}, []() {}},
    });
    btnFlyout->SetFlyout(flyout.get());
    flyouts_.push_back(flyout);

    // 示例 5：自定义颜色
    auto* card5 = CreateExampleCard(content, L"自定义颜色");
    auto* btnRow5 = card5->Add(std::make_unique<StackPanel>());
    btnRow5->SetOrientation(StackPanel::Orientation::Horizontal);
    btnRow5->SetSpacing(12.0f);

    auto* btnCustomBg = btnRow5->Add(std::make_unique<Button>());
    btnCustomBg->SetText(L"自定义背景");
    btnCustomBg->SetBackground(D2D1::ColorF(0x6A5ACD, 1.0f));  // SlateBlue
    btnCustomBg->SetForeground(D2D1::ColorF(D2D1::ColorF::White));
    btnCustomBg->SetTooltip(L"自定义背景色和前景色");

    auto* btnCustomAccent = btnRow5->Add(std::make_unique<Button>());
    btnCustomAccent->SetText(L"自定义 Accent");
    btnCustomAccent->SetKind(Button::Kind::Accent);
    btnCustomAccent->SetAccentColor(D2D1::ColorF(0xFF4500, 1.0f));  // OrangeRed
    btnCustomAccent->SetTooltip(L"自定义 Accent 强调色");

    // --- 样式定制（原「样式定制大全」706 页的内容，已并回本页）--------------
    {
        auto* card = CreateExampleCard(content, L"样式定制：同一个控件的六种外观");
        AddNote(card, L"Control 层提供 8 个可选属性：Foreground / Background / BorderBrush / "
                      L"AccentColor / CornerRadius / BorderThickness / FontSize / FontWeight。"
                      L"未设置的跟随主题，设置过的优先 —— 这就是 WPF 的属性优先级模型。");

        auto* row = AddRow(card, 12.0f);

        auto* plain = row->Add(std::make_unique<Button>());
        plain->SetText(L"完全主题驱动");

        auto* accent = row->Add(std::make_unique<Button>());
        accent->SetText(L"Accent 主操作");
        accent->SetKind(Button::Kind::Accent);

        auto* pill = row->Add(std::make_unique<Button>());
        pill->SetText(L"胶囊圆角");
        pill->SetKind(Button::Kind::Accent);
        pill->SetCornerRadius(16.0f);

        auto* outline = row->Add(std::make_unique<Button>());
        outline->SetText(L"描边风格");
        // 透明填充而不是「不画」：调用者只需要测一个统一的值
        outline->SetBackground(D2D1::ColorF(0x000000, 0.0f));
        outline->SetBorderThickness(2.0f);
        outline->SetForeground(D2D1::ColorF(0x0078D4, 1.0f));

        auto* danger = row->Add(std::make_unique<Button>());
        danger->SetText(L"危险操作");
        danger->SetKind(Button::Kind::Accent);
        // 只固定语义色，前景背景留给主题 —— 这样两种主题下都成立
        danger->SetAccentColor(D2D1::ColorF(0xC42B1C, 1.0f));

        auto* big = row->Add(std::make_unique<Button>());
        big->SetText(L"大字号 CTA");
        big->SetKind(Button::Kind::Accent);
        big->SetAccentColor(D2D1::ColorF(0x107C10, 1.0f));
        big->SetFontSize(17.0f);
        big->SetCornerRadius(10.0f);

        auto* sec = AddSubSection(card, L"Kind 是 Measure 级，不是 Render 级");
        AddNote(sec, L"Kind 选择字重（Accent 画 SemiBold），而 SemiBold 比 Normal 宽，"
                     L"所以它会改变 desired 尺寸。曾经把它标成 Render 级，导致 Accent "
                     L"按钮在窄容器里标题溢出自己申请的框。");
        auto* cmp = AddRow(sec, 12.0f);
        for (auto [label, kind] : {
                 std::pair<const wchar_t*, Button::Kind>{L"Standard（Normal 字重）", Button::Kind::Standard},
                 {L"Accent（SemiBold，更宽）", Button::Kind::Accent},
                 {L"Subtle（静止无填充）", Button::Kind::Subtle}}) {
            auto* b = cmp->Add(std::make_unique<Button>());
            b->SetText(label);
            b->SetKind(kind);
        }

        auto* dsec = AddSubSection(card, L"禁用态");
        AddNote(dsec, L"填充 alpha ×0.4、文字走 textSecondary。但显式设过的 Foreground / "
                      L"Background 在禁用态【原样保留】—— 调用者给了精确色值，框架不该改它。");
        auto* drow = AddRow(dsec, 12.0f);
        for (auto [label, kind] : {
                 std::pair<const wchar_t*, Button::Kind>{L"禁用 Standard", Button::Kind::Standard},
                 {L"禁用 Accent", Button::Kind::Accent}}) {
            auto* b = drow->Add(std::make_unique<Button>());
            b->SetText(label);
            b->SetKind(kind);
            b->SetEnabled(false);
        }
    }

    CreateCodeExample(content, LR"(// Button 基本用法
auto* btn = panel->Add(std::make_unique<Button>());
btn->SetText(L"点击我");
btn->SetKind(Button::Kind::Accent);

// 点击事件
btn->Click().Subscribe(this, [](void* owner, Button&, RoutedEventArgs&) {
    // 处理点击
});

// Flyout 集成
auto flyout = std::make_shared<MenuFlyout>();
flyout->AddItem(L"选项1", []() { /* ... */ });
btn->SetFlyout(flyout);

// 自定义颜色
btn->SetBackground(D2D1::ColorF(0x6A5ACD, 1.0f));
btn->SetForeground(D2D1::ColorF(D2D1::ColorF::White));
)");

    return std::move(page);
}



std::unique_ptr<ScrollPanel> GalleryApp::CreateToggleSwitchPage() {
    auto [page, content] = CreatePageShell(L"ToggleSwitch");

    // --- 1. 状态 ---------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"四种状态");
        AddNote(card, L"开 / 关 / 禁用（开）/ 禁用（关）。注意禁用态是淡化的 —— "
                      L"保留色相、alpha ×0.4，和禁用的 Button 一致。"
                      L"（这一点之前是坏的：禁用的开关和启用的看起来一模一样。）");

        auto* on = card->Add(std::make_unique<ToggleSwitch>());
        on->SetText(L"Wi-Fi（开）");
        on->SetOn(true);

        auto* off = card->Add(std::make_unique<ToggleSwitch>());
        off->SetText(L"蓝牙（关）");
        off->SetOn(false);

        auto* dOn = card->Add(std::make_unique<ToggleSwitch>());
        dOn->SetText(L"禁用（开）");
        dOn->SetOn(true);
        dOn->SetEnabled(false);

        auto* dOff = card->Add(std::make_unique<ToggleSwitch>());
        dOff->SetText(L"禁用（关）");
        dOff->SetOn(false);
        dOff->SetEnabled(false);
    }

    // --- 2. 布局行为 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"在布局里的行为");
        AddNote(card, L"ToggleSwitch 的 desired 宽度是「旋钮轨道 + 间距 + 文字」。"
                      L"默认 HAlign::Stretch 会占满可用宽度（文字左对齐，右边留白），"
                      L"所以在很宽的页面上看不出它的固有宽度 —— 下面用画出边界的容器"
                      L"把区别显示出来。");

        auto* sec1 = AddSubSection(card, L"对齐：Left / Center / Right / Stretch");
        for (auto [label, align] : {
                 std::pair<const wchar_t*, HAlign>{L"Left（收缩到内容宽度）", HAlign::Left},
                 {L"Center", HAlign::Center},
                 {L"Right", HAlign::Right},
                 {L"Stretch（默认，占满）", HAlign::Stretch}}) {
            auto* box = AddBoundedBox(sec1, 420.0f, label);
            auto* t = box->Add(std::make_unique<ToggleSwitch>());
            t->SetText(L"设置项");
            t->SetOn(true);
            t->SetHAlign(align);
        }

        auto* sec2 = AddSubSection(card, L"窄容器：文字被压缩时");
        AddNote(sec2, L"容器比 desired 窄时，本框架的 Arrange 契约是「子元素永不超出容器」"
                      L"—— 超出部分被裁掉，不会溢出也不会警告。所以给它足够宽度是调用者的责任。");
        for (float w : {320.0f, 200.0f, 130.0f}) {
            auto* box = AddBoundedBox(sec2, w,
                std::wstring(L"容器宽 ") + std::to_wstring(static_cast<int>(w)) + L" DIP");
            auto* t = box->Add(std::make_unique<ToggleSwitch>());
            t->SetText(L"启用后台同步");
            t->SetOn(true);
        }

        auto* sec3 = AddSubSection(card, L"设置组：Grid 两列标签 + 开关");
        AddNote(sec3, L"实际设置页常见形状：左列说明文字用 Star 列，右列开关用 Auto 列"
                      L"并右对齐，这样多行开关会纵向对齐成一条线。");
        auto* grid = sec3->Add(std::make_unique<Grid>());
        grid->AddColumn(GridLength::Star(1.0f));
        grid->AddColumn(GridLength::Auto());
        const wchar_t* rows[] = {L"自动更新", L"使用移动数据同步", L"崩溃时发送诊断报告"};
        for (int i = 0; i < 3; ++i) {
            grid->AddRow(GridLength::Auto());
            auto* label = grid->Add(std::make_unique<TextBlock>());
            grid->SetCell(label, i, 0);
            label->SetText(rows[i]);
            label->SetVAlign(VAlign::Center);
            label->SetMargin(Thickness{0, 6.0f, 12.0f, 6.0f});

            auto* t = grid->Add(std::make_unique<ToggleSwitch>());
            grid->SetCell(t, i, 1);
            t->SetOn(i != 1);
            t->SetHAlign(HAlign::Right);
            t->SetVAlign(VAlign::Center);
        }
    }

    // --- 3. 样式定制 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");
        AddNote(card, L"Control 层提供 8 个可选属性，未设置的跟随主题。对 toggle 类控件"
                      L"最有用的是 AccentColor（开启态轨道色）与 FontSize。");

        auto* sec1 = AddSubSection(card, L"AccentColor：五种语义色");
        auto* row = AddRow(sec1, 18.0f);
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        for (int i = 0; i < n; ++i) {
            auto* t = row->Add(std::make_unique<ToggleSwitch>());
            t->SetText(sw[i].label);
            t->SetOn(true);
            t->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
        }

        auto* sec2 = AddSubSection(card, L"FontSize：标签字号");
        AddNote(sec2, L"字号变化会改变控件的 desired 高度（Measure 级），"
                      L"旋钮尺寸不跟着变 —— 开关的命中区应当保持一致。");
        for (float fs : {12.0f, 14.0f, 18.0f}) {
            auto* t = sec2->Add(std::make_unique<ToggleSwitch>());
            t->SetText(std::wstring(L"FontSize ") + std::to_wstring(static_cast<int>(fs)));
            t->SetOn(true);
            t->SetHAlign(HAlign::Left);
            t->SetFontSize(fs);
        }

        auto* sec3 = AddSubSection(card, L"Foreground 的含义（易错点）");
        AddNote(sec3, L"对 toggle 类控件，Foreground 是【画在轨道上的旋钮】的颜色，"
                      L"不是标签文字色。把它和 AccentColor 设成同一个颜色会让旋钮消失 —— "
                      L"下面左边就是这个错误，右边是正确做法（只设 AccentColor）。");
        auto* pair = AddRow(sec3, 24.0f);
        auto* wrong = pair->Add(std::make_unique<ToggleSwitch>());
        wrong->SetText(L"错：旋钮看不见");
        wrong->SetOn(true);
        wrong->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));
        wrong->SetForeground(D2D1::ColorF(0x8764B8, 1.0f));
        auto* right = pair->Add(std::make_unique<ToggleSwitch>());
        right->SetText(L"对：只设 AccentColor");
        right->SetOn(true);
        right->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));
    }

    // --- 4. 混合使用 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：开关联动其他控件");
        AddNote(card, L"开关最常见的用途是启用/禁用一组相关控件。");

        auto* master = card->Add(std::make_unique<ToggleSwitch>());
        master->SetText(L"启用高级选项");
        master->SetOn(false);
        master->SetHAlign(HAlign::Left);

        auto* group = AddSubSection(card, L"受控的一组");
        auto* slider = group->Add(std::make_unique<Slider>());
        slider->SetMin(0.0f);
        slider->SetMax(100.0f);
        slider->SetValue(40.0f);
        slider->SetEnabled(false);
        auto* check = group->Add(std::make_unique<CheckBox>());
        check->SetText(L"同时上传日志");
        check->SetEnabled(false);
        auto* btn = group->Add(std::make_unique<Button>());
        btn->SetText(L"立即执行");
        btn->SetHAlign(HAlign::Left);
        btn->SetEnabled(false);

        toggleGroupSlider_ = slider;
        toggleGroupCheck_ = check;
        toggleGroupButton_ = btn;
        pageSubs_ += master->Toggled().Subscribe(this,
            [](void* o, ToggleSwitch&, bool& on) {
                auto* self = static_cast<GalleryApp*>(o);
                if (self->toggleGroupSlider_) self->toggleGroupSlider_->SetEnabled(on);
                if (self->toggleGroupCheck_) self->toggleGroupCheck_->SetEnabled(on);
                if (self->toggleGroupButton_) self->toggleGroupButton_->SetEnabled(on);
            });
    }

    CreateCodeExample(content, LR"(auto* toggle = panel->Add(std::make_unique<ToggleSwitch>());
toggle->SetText(L"Wi-Fi");
toggle->SetOn(true);
toggle->SetAccentColor(D2D1::ColorF(0x107C10, 1.0f));   // 开启态轨道色
toggle->SetHAlign(HAlign::Left);                        // 默认 Stretch，会占满

// 注意：Foreground 是旋钮色，不是标签文字色。
// 和 AccentColor 设成同色会让旋钮消失。

sub_ = toggle->Toggled().Subscribe(this, [](void* o, ToggleSwitch&, bool& on) {
    static_cast<MyPage*>(o)->advanced_->SetEnabled(on);
});)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateHyperlinkPage() {
    auto [page, content] = CreatePageShell(L"Hyperlink");

    // 这一页以前写着「计划中，暂未实现」并推荐用 Subtle Button 代替 —— 那是过期文档：
    // Hyperlink 早就实现了，包含 URI 激活策略、悬停下划线、访问过状态。
    // 文档撒谎比没有文档更糟，因为它会让人去写替代方案。

    // --- 1. 基础用法 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"基础链接");
        auto* desc = card->Add(std::make_unique<TextBlock>());
        desc->SetText(L"带下划线的可点击文字，悬停时下划线加深，可获得焦点（Tab 可达，"
                      L"Space / Enter 激活）。");
        desc->SetWrap(true);
        desc->SetDimmed(true);
        desc->SetMargin(Thickness{0, 0, 0, 8.0f});

        auto* plain = card->Add(std::make_unique<Hyperlink>());
        plain->SetText(L"这是一个链接（只触发 Click，不打开浏览器）");
        plain->SetHAlign(HAlign::Left);

        auto* log = card->Add(std::make_unique<TextBlock>());
        log->SetText(L"（还没点）");
        log->SetDimmed(true);
        log->SetMargin(Thickness{0, 8.0f, 0, 0});
        hyperlinkLog_ = log;
        pageSubs_ += plain->Click().Subscribe(this,
            [](void* o, Hyperlink&, RoutedEventArgs&) {
                auto* self = static_cast<GalleryApp*>(o);
                if (!self->hyperlinkLog_) return;
                ++self->hyperlinkClicks_;
                self->hyperlinkLog_->SetText(
                    L"Click 已触发 " + std::to_wstring(self->hyperlinkClicks_) + L" 次。");
            });
    }

    // --- 2. 激活策略 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"激活策略：谁来决定点击是否打开浏览器");
        auto* desc = card->Add(std::make_unique<TextBlock>());
        desc->SetText(L"设了 URI 之后，「普通点击是否直接跳浏览器」是个安全取舍，所以做成"
                      L"可选策略：库默认 CtrlClick（普通点击只发 Click 事件，"
                      L"Ctrl+点击才交给 shell），因为在应用内容里嵌一个一点就跳走的链接"
                      L"容易误触。想要 WinUI 那样的行为就显式选 Click。");
        desc->SetWrap(true);
        desc->SetDimmed(true);
        desc->SetMargin(Thickness{0, 0, 0, 8.0f});

        auto* ctrlLink = card->Add(std::make_unique<Hyperlink>());
        ctrlLink->SetText(L"CtrlClick 策略：Ctrl+点击才打开（库默认）");
        ctrlLink->SetUri(L"https://github.com/");
        ctrlLink->SetActivation(HyperlinkActivation::CtrlClick);
        ctrlLink->SetHAlign(HAlign::Left);
        ctrlLink->SetTooltip(L"Ctrl+点击会真的打开浏览器");

        auto* clickLink = card->Add(std::make_unique<Hyperlink>());
        clickLink->SetText(L"Click 策略：普通点击直接打开（WinUI 行为）");
        clickLink->SetUri(L"https://github.com/");
        clickLink->SetActivation(HyperlinkActivation::Click);
        clickLink->SetHAlign(HAlign::Left);
        clickLink->SetTooltip(L"点一下就会打开浏览器");

        auto* defLink = card->Add(std::make_unique<Hyperlink>());
        defLink->SetText(L"Default 策略：跟随全局 BehaviorSettings");
        defLink->SetUri(L"https://github.com/");
        defLink->SetActivation(HyperlinkActivation::Default);
        defLink->SetHAlign(HAlign::Left);
    }

    // --- 3. 混合使用 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：句子里的链接 / 免责声明");
        auto* hint = card->Add(std::make_unique<TextBlock>());
        hint->SetText(L"Hyperlink 是行内元素的常见用法，和 TextBlock 并排放在 "
                      L"StackPanel 里即可。注意本框架没有富文本，所以「一句话中间嵌链接」"
                      L"要靠水平 StackPanel 拼，而不是在一段文字里插标记。");
        hint->SetWrap(true);
        hint->SetDimmed(true);

        auto* row = card->Add(std::make_unique<StackPanel>());
        row->SetOrientation(StackPanel::Orientation::Horizontal);
        row->SetSpacing(4.0f);
        row->SetMargin(Thickness{0, 8.0f, 0, 0});

        auto* pre = row->Add(std::make_unique<TextBlock>());
        pre->SetText(L"继续即表示你同意");
        pre->SetVAlign(VAlign::Center);

        auto* terms = row->Add(std::make_unique<Hyperlink>());
        terms->SetText(L"服务条款");
        terms->SetVAlign(VAlign::Center);

        auto* mid = row->Add(std::make_unique<TextBlock>());
        mid->SetText(L"与");
        mid->SetVAlign(VAlign::Center);

        auto* privacy = row->Add(std::make_unique<Hyperlink>());
        privacy->SetText(L"隐私政策");
        privacy->SetVAlign(VAlign::Center);

        auto* post = row->Add(std::make_unique<TextBlock>());
        post->SetText(L"。");
        post->SetVAlign(VAlign::Center);
    }

    // --- 4. 禁用态 -------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"禁用");
        auto* disabled = card->Add(std::make_unique<Hyperlink>());
        disabled->SetText(L"禁用的链接（不可点、不可聚焦）");
        disabled->SetUri(L"https://github.com/");
        disabled->SetEnabled(false);
        disabled->SetHAlign(HAlign::Left);
    }

    CreateCodeExample(content, LR"(auto* link = panel->Add(std::make_unique<Hyperlink>());
link->SetText(L"Visit FluentUI on GitHub");
link->SetUri(L"https://github.com/...");

// 激活策略（默认 CtrlClick）：
//   CtrlClick — 普通点击只发 Click，Ctrl+点击交给 shell
//   Click     — 普通点击直接交给 shell（WinUI 行为）
//   Default   — 跟随 BehaviorSettings::hyperlinkRequireCtrl
link->SetActivation(HyperlinkActivation::CtrlClick);

// URI 被 shell 打开时不会触发 Click —— 两者是互斥的分支
sub_ = link->Click().Subscribe(this, [](void*, Hyperlink&, RoutedEventArgs&) {
    // 只在没有走 shell 的那条路上运行
});)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateImagePage() {
    auto [page, content] = CreatePageShell(L"Image");

    // 示例 1：四种拉伸模式
    auto* card1 = CreateExampleCard(content, L"拉伸模式（Stretch）");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"Image 控件支持四种拉伸模式，控制图像如何适配控件边界。通过 WIC 解码，支持 PNG/JPG/BMP/ICO/GIF/TIFF。");
    desc1->SetWrap(true);

    auto* modeList = card1->Add(std::make_unique<StackPanel>());
    modeList->SetSpacing(6.0f);
    modeList->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* mode1 = modeList->Add(std::make_unique<TextBlock>());
    mode1->SetText(L"• None: 原始尺寸居中，超出边界时裁剪");
    mode1->SetFontSize(13.0f);

    auto* mode2 = modeList->Add(std::make_unique<TextBlock>());
    mode2->SetText(L"• Uniform: 等比缩放以适配边界，完整显示（可能有黑边）");
    mode2->SetFontSize(13.0f);

    auto* mode3 = modeList->Add(std::make_unique<TextBlock>());
    mode3->SetText(L"• UniformToFill: 等比缩放填充边界，保持宽高比（可能裁剪）");
    mode3->SetFontSize(13.0f);

    auto* mode4 = modeList->Add(std::make_unique<TextBlock>());
    mode4->SetText(L"• Fill: 拉伸到边界，不保持宽高比（可能变形）");
    mode4->SetFontSize(13.0f);

    // 示例 2：实际演示（需要图像文件）
    auto* card2 = CreateExampleCard(content, L"加载与显示");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"Image 从文件路径或 WIC 源加载位图。加载在 Attach 时进行（SetSource 只记录路径）。");
    desc2->SetWrap(true);

    auto* hint2 = card2->Add(std::make_unique<TextBlock>());
    hint2->SetText(L"💡 实际演示需要图像文件。Image 控件已实现，但 Gallery 中暂未嵌入演示图像。");
    hint2->SetWrap(true);
    hint2->SetFontSize(12.0f);
    hint2->SetMargin(Thickness{0, 8.0f, 0, 0});
    hint2->SetForeground(D2D1::ColorF(0xE9A568, 1.0f));

    // 示例 3：失败回退
    auto* card3 = CreateExampleCard(content, L"失败回退（Fallback）");
    auto* desc3 = card3->Add(std::make_unique<TextBlock>());
    desc3->SetText(L"SetFailureSource 设置主图像加载失败时的回退图像（如占位图）。ImageFailed 事件在主图像失败时触发。");
    desc3->SetWrap(true);

    CreateCodeExample(content, LR"(auto* image = panel->Add(std::make_unique<Image>());
image->SetSource(L"photo.png");
image->SetStretch(ImageStretch::Uniform);
image->SetWidth(200.0f);
image->SetHeight(200.0f);

// 失败回退
image->SetFailureSource(L"placeholder.png");
image->ImageFailed().Subscribe(owner, [](void*, Image&, auto& args) {
    // 主图像加载失败
});

// 拉伸模式
// - None: 原始尺寸居中
// - Uniform: 等比适配（默认）
// - UniformToFill: 等比填充
// - Fill: 拉伸填充)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateRepeatButtonPage() {
    auto [page, content] = CreatePageShell(L"RepeatButton");

    auto* card = CreateExampleCard(content, L"Auto-repeat while held");
    auto* desc = card->Add(std::make_unique<TextBlock>());
    desc->SetText(L"RepeatButton 在按住时持续触发 Click 事件，用于数值调节、滚动条等场景。");
    desc->SetWrap(true);
    desc->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* counterLabel = card->Add(std::make_unique<TextBlock>());
    counterLabel->SetText(L"计数: 0");
    counterLabel->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* repeatBtn = card->Add(std::make_unique<RepeatButton>());
    repeatBtn->SetText(L"按住我");
    repeatBtn->SetWidth(120.0f);
    repeatBtn->SetTooltip(L"按住触发重复事件");

    // 使用页面生命期的计数器（不回收，与 ComboBox 示例一致）
    struct CounterPair { RepeatButton* btn; TextBlock* label; int count; };
    auto* pair = new CounterPair{repeatBtn, counterLabel, 0};
    pageSubs_ += repeatBtn->Click().Subscribe(pair, [](void* owner, Button&, RoutedEventArgs&) {
        auto* p = static_cast<CounterPair*>(owner);
        p->count++;
        p->label->SetText(L"计数: " + std::to_wstring(p->count));
    });

    CreateCodeExample(content, LR"(auto* repeatBtn = panel->Add(std::make_unique<RepeatButton>());
repeatBtn->SetText(L"Volume +");
repeatBtn->Click().Subscribe(owner, [](void* owner, Button&, RoutedEventArgs&) {
    // 按住时持续触发，首次延迟 0.5 秒，之后 20 Hz 重复率
});)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateSeparatorPage() {
    auto [page, content] = CreatePageShell(L"Separator");

    auto* card1 = CreateExampleCard(content, L"Horizontal separator");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"水平分隔线，默认 1 DIP 粗，横向拉伸填充父容器。");
    desc1->SetWrap(true);
    desc1->SetMargin(Thickness{0, 0, 0, 8.0f});

    card1->Add(std::make_unique<TextBlock>())->SetText(L"Section 1");
    card1->Add(std::make_unique<Separator>());
    card1->Add(std::make_unique<TextBlock>())->SetText(L"Section 2");
    card1->Add(std::make_unique<Separator>());
    card1->Add(std::make_unique<TextBlock>())->SetText(L"Section 3");

    auto* card2 = CreateExampleCard(content, L"Vertical separator");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"垂直分隔线，用于并排控件之间。需要放在水平容器中。");
    desc2->SetWrap(true);
    desc2->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* row = card2->Add(std::make_unique<StackPanel>());
    row->SetOrientation(StackPanel::Orientation::Horizontal);
    row->SetSpacing(12.0f);
    row->Add(std::make_unique<Button>())->SetText(L"Button 1");
    auto* sep = row->Add(std::make_unique<Separator>());
    sep->SetOrientation(Separator::Orientation::Vertical);
    sep->SetHeight(32.0f);
    row->Add(std::make_unique<Button>())->SetText(L"Button 2");

    auto* card3 = CreateExampleCard(content, L"Custom thickness");
    auto* desc3 = card3->Add(std::make_unique<TextBlock>());
    desc3->SetText(L"SetThickness 调整分隔线粗细（DIP）。");
    desc3->SetWrap(true);
    desc3->SetMargin(Thickness{0, 0, 0, 8.0f});

    auto* thin = card3->Add(std::make_unique<Separator>());
    thin->SetThickness(0.5f);
    card3->Add(std::make_unique<TextBlock>())->SetText(L"Thin (0.5 DIP)");

    auto* thick = card3->Add(std::make_unique<Separator>());
    thick->SetThickness(3.0f);
    card3->Add(std::make_unique<TextBlock>())->SetText(L"Thick (3.0 DIP)");

    CreateCodeExample(content, LR"(// 水平分隔线
auto* separator = panel->Add(std::make_unique<Separator>());

// 垂直分隔线
auto* vSep = panel->Add(std::make_unique<Separator>());
vSep->SetOrientation(Separator::Orientation::Vertical);
vSep->SetHeight(32.0f);

// 自定义粗细
separator->SetThickness(2.0f);)");
    return std::move(page);
}

} // namespace fluent
