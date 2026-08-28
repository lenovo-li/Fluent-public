// CheckRadioPages.cpp — CheckBox 与 RadioButton 的展示页（nav 102 / 103）
//
// 这两页曾经叫「完整演示」并挂在单独的「样式定制大全」分类下，与各控件自己的
// 基础页并列。那种组织有问题：同一个控件出现在两个地方，读者不知道该看哪一页，
// 而两页的第一张卡片内容还是一样的。既然这里本来就是基础页的超集，就直接接到
// 控件自己的 nav id 上，基础页删掉。
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/Grid.h"

namespace fluent {

// ============================================================================
// 增强的 CheckBox 页面 - 展示分组逻辑和高级样式
// ============================================================================
std::unique_ptr<ScrollPanel> GalleryApp::CreateCheckBoxPage() {
    auto [page, content] = CreatePageShell(L"CheckBox 完整演示");

    // 示例 1：基础状态
    auto* card1 = CreateExampleCard(content, L"基础状态");
    auto* checked = card1->Add(std::make_unique<CheckBox>());
    checked->SetText(L"Checked");
    checked->SetChecked(true);
    checked->SetTooltip(L"已选中状态");

    auto* unchecked = card1->Add(std::make_unique<CheckBox>());
    unchecked->SetText(L"Unchecked");
    unchecked->SetChecked(false);
    unchecked->SetTooltip(L"未选中状态");

    auto* disabled = card1->Add(std::make_unique<CheckBox>());
    disabled->SetText(L"Disabled (checked)");
    disabled->SetChecked(true);
    disabled->SetEnabled(false);
    disabled->SetTooltip(L"禁用状态（但仍可显示 Tooltip）");

    // 示例 2：复选框组（功能权限设置）
    auto* card2 = CreateExampleCard(content, L"复选框组 - 功能权限设置");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"选择要启用的功能模块（多选）：");
    desc2->SetMargin(Thickness{0, 0, 0, 8.0f});

    // 使用一个结构体存储所有复选框的状态
    struct FeatureState {
        CheckBox* readPerm = nullptr;
        CheckBox* writePerm = nullptr;
        CheckBox* deletePerm = nullptr;
        CheckBox* adminPerm = nullptr;
        TextBlock* statusLabel = nullptr;
    };
    auto* features = new FeatureState();

    features->readPerm = card2->Add(std::make_unique<CheckBox>());
    features->readPerm->SetText(L"📖 读取权限");
    features->readPerm->SetChecked(true);
    features->readPerm->SetTooltip(L"允许读取数据");

    features->writePerm = card2->Add(std::make_unique<CheckBox>());
    features->writePerm->SetText(L"✏️ 写入权限");
    features->writePerm->SetChecked(true);
    features->writePerm->SetTooltip(L"允许修改数据");

    features->deletePerm = card2->Add(std::make_unique<CheckBox>());
    features->deletePerm->SetText(L"🗑️ 删除权限");
    features->deletePerm->SetChecked(false);
    features->deletePerm->SetTooltip(L"允许删除数据");

    features->adminPerm = card2->Add(std::make_unique<CheckBox>());
    features->adminPerm->SetText(L"🔐 管理员权限");
    features->adminPerm->SetChecked(false);
    features->adminPerm->SetTooltip(L"完全控制权限");

    features->statusLabel = card2->Add(std::make_unique<TextBlock>());
    features->statusLabel->SetText(L"当前已选: 读取, 写入");
    features->statusLabel->SetFontSize(12.0f);
    features->statusLabel->SetMargin(Thickness{0, 12.0f, 0, 0});

    // 更新状态的处理器。Event 的 handler 是无捕获函数指针，签名必须是
    // void(void*, Sender&, Args&) —— Args 是引用，写成 bool 值会导致
    // 隐式转换失败（这是第一版编译不过的原因）。
    auto updateStatus = +[](void* owner, CheckBox&, bool&) {
        auto* state = static_cast<FeatureState*>(owner);
        std::wstring status = L"当前已选: ";
        std::vector<std::wstring> selected;
        if (state->readPerm->IsChecked()) selected.push_back(L"读取");
        if (state->writePerm->IsChecked()) selected.push_back(L"写入");
        if (state->deletePerm->IsChecked()) selected.push_back(L"删除");
        if (state->adminPerm->IsChecked()) selected.push_back(L"管理员");

        if (selected.empty()) {
            status += L"无";
        } else {
            for (size_t i = 0; i < selected.size(); ++i) {
                status += selected[i];
                if (i < selected.size() - 1) status += L", ";
            }
        }
        state->statusLabel->SetText(status);
    };

    pageSubs_.Keep(features->readPerm->Checked().Subscribe(features, updateStatus));
    pageSubs_.Keep(features->writePerm->Checked().Subscribe(features, updateStatus));
    pageSubs_.Keep(features->deletePerm->Checked().Subscribe(features, updateStatus));
    pageSubs_.Keep(features->adminPerm->Checked().Subscribe(features, updateStatus));

    // 示例 3：自定义颜色和字体
    auto* card3 = CreateExampleCard(content, L"自定义样式");

    auto* custom1 = card3->Add(std::make_unique<CheckBox>());
    custom1->SetText(L"大字号复选框");
    custom1->SetFontSize(18.0f);
    custom1->SetChecked(true);
    custom1->SetTooltip(L"字号 18pt");

    auto* custom2 = card3->Add(std::make_unique<CheckBox>());
    custom2->SetText(L"粗体复选框");
    custom2->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);
    custom2->SetTooltip(L"粗体文本");

    auto* custom3 = card3->Add(std::make_unique<CheckBox>());
    custom3->SetText(L"自定义前景色");
    custom3->SetForeground(D2D1::ColorF(0xFF4500, 1.0f));  // OrangeRed
    custom3->SetChecked(true);
    custom3->SetTooltip(L"橙红色文字");

    auto* custom4 = card3->Add(std::make_unique<CheckBox>());
    custom4->SetText(L"自定义 Accent 颜色");
    custom4->SetAccentColor(D2D1::ColorF(0x32CD32, 1.0f));  // LimeGreen
    custom4->SetChecked(true);
    custom4->SetTooltip(L"青柠绿选中色");

    // --- 样式定制（原「样式定制大全」706 页的内容，已并回本页）--------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");
        AddNote(card, L"Control 层提供 8 个可选属性，未设置的跟随主题。对 CheckBox 最有用的"
                      L"是 AccentColor（勾选态方框填充）与 FontSize。");

        auto* sec1 = AddSubSection(card, L"AccentColor：五种语义色");
        auto* row = AddRow(sec1, 18.0f);
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        for (int i = 0; i < n; ++i) {
            auto* cb = row->Add(std::make_unique<CheckBox>());
            cb->SetText(sw[i].label);
            cb->SetChecked(true);
            cb->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
        }

        auto* sec2 = AddSubSection(card, L"FontSize：标签字号");
        AddNote(sec2, L"字号改变控件的 desired 高度（Measure 级），但方框尺寸不跟着变 —— "
                      L"命中区应当保持一致。");
        for (float fs : {12.0f, 14.0f, 18.0f}) {
            auto* cb = sec2->Add(std::make_unique<CheckBox>());
            cb->SetText(std::wstring(L"FontSize ") + std::to_wstring(static_cast<int>(fs)));
            cb->SetChecked(true);
            cb->SetHAlign(HAlign::Left);
            cb->SetFontSize(fs);
        }

        auto* sec3 = AddSubSection(card, L"Foreground 的含义（易错点）");
        AddNote(sec3, L"对 toggle 类控件，Foreground 是【画在方框上的勾】的颜色，"
                      L"不是标签文字色。和 AccentColor 设成同色会让勾消失 —— "
                      L"左边是这个错误，右边是正确做法。");
        auto* pair = AddRow(sec3, 24.0f);
        auto* wrong = pair->Add(std::make_unique<CheckBox>());
        wrong->SetText(L"错：勾看不见");
        wrong->SetChecked(true);
        wrong->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));
        wrong->SetForeground(D2D1::ColorF(0x8764B8, 1.0f));
        auto* right = pair->Add(std::make_unique<CheckBox>());
        right->SetText(L"对：只设 AccentColor");
        right->SetChecked(true);
        right->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));

        auto* sec4 = AddSubSection(card, L"禁用态是淡化的");
        AddNote(sec4, L"保留色相、alpha ×0.4，和禁用的 Button 一致。这一点之前是坏的："
                      L"禁用的勾选框和启用的看起来一模一样。");
        auto* drow = AddRow(sec4, 24.0f);
        for (auto [label, chk] : {std::pair<const wchar_t*, bool>{L"禁用（勾选）", true},
                                  {L"禁用（未勾）", false}}) {
            auto* cb = drow->Add(std::make_unique<CheckBox>());
            cb->SetText(label);
            cb->SetChecked(chk);
            cb->SetEnabled(false);
        }
    }

    CreateCodeExample(content, LR"(// CheckBox 基本用法
auto* check = panel->Add(std::make_unique<CheckBox>());
check->SetText(L"Accept terms");
check->SetChecked(true);

// 自定义样式
check->SetFontSize(18.0f);
check->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);
check->SetForeground(D2D1::ColorF(0xFF4500, 1.0f));
check->SetAccentColor(D2D1::ColorF(0x32CD32, 1.0f));

// 复选框组逻辑（多选）
check->Checked().Subscribe(owner, [](void* ctx, CheckBox& cb, bool checked) {
    // 处理状态变化
    // CheckBox 支持多个同时选中
});)");

    return std::move(page);
}

// ============================================================================
// 增强的 RadioButton 页面 - 展示多个分组
// ============================================================================
std::unique_ptr<ScrollPanel> GalleryApp::CreateRadioButtonPage() {
    auto [page, content] = CreatePageShell(L"RadioButton 完整演示");

    // 示例 1：单选组 - 主题选择
    auto* card1 = CreateExampleCard(content, L"单选组 1 - 主题选择");
    auto* label1 = card1->Add(std::make_unique<TextBlock>());
    label1->SetText(L"选择主题：");
    label1->SetMargin(Thickness{0, 0, 0, 8.0f});

    // 每个分组需要一个共享的 int 变量
    static int themeChoice = 1;  // 默认选中 Dark

    auto* themeLight = card1->Add(std::make_unique<RadioButton>());
    themeLight->SetText(L"☀️ 浅色主题");
    themeLight->SetGroup(&themeChoice, 0);
    themeLight->SetTooltip(L"切换到浅色主题");

    auto* themeDark = card1->Add(std::make_unique<RadioButton>());
    themeDark->SetText(L"🌙 深色主题");
    themeDark->SetGroup(&themeChoice, 1);
    themeDark->SetTooltip(L"切换到深色主题");

    auto* themeAuto = card1->Add(std::make_unique<RadioButton>());
    themeAuto->SetText(L"🔄 跟随系统");
    themeAuto->SetGroup(&themeChoice, 2);
    themeAuto->SetTooltip(L"根据系统设置自动切换");

    auto* themeStatus = card1->Add(std::make_unique<TextBlock>());
    themeStatus->SetText(L"当前: 深色主题");
    themeStatus->SetFontSize(12.0f);
    themeStatus->SetMargin(Thickness{0, 8.0f, 0, 0});

    struct ThemeCtx { TextBlock* label; };
    auto* themeCtx = new ThemeCtx{themeStatus};
    auto updateTheme = +[](void* owner, RadioButton&, int& value) {
        auto* ctx = static_cast<ThemeCtx*>(owner);
        const wchar_t* names[] = {L"浅色主题", L"深色主题", L"跟随系统"};
        if (value >= 0 && value < 3)
            ctx->label->SetText(std::wstring(L"当前: ") + names[value]);
    };
    pageSubs_.Keep(themeLight->Selected().Subscribe(themeCtx, updateTheme));
    pageSubs_.Keep(themeDark->Selected().Subscribe(themeCtx, updateTheme));
    pageSubs_.Keep(themeAuto->Selected().Subscribe(themeCtx, updateTheme));

    // 示例 2：单选组 - 语言选择（独立分组）
    auto* card2 = CreateExampleCard(content, L"单选组 2 - 语言选择（独立分组）");
    auto* label2 = card2->Add(std::make_unique<TextBlock>());
    label2->SetText(L"选择语言：");
    label2->SetMargin(Thickness{0, 0, 0, 8.0f});

    // 另一个独立的分组变量
    static int langChoice = 0;  // 默认中文

    auto* langCN = card2->Add(std::make_unique<RadioButton>());
    langCN->SetText(L"🇨🇳 简体中文");
    langCN->SetGroup(&langChoice, 0);
    langCN->SetTooltip(L"切换到简体中文");

    auto* langEN = card2->Add(std::make_unique<RadioButton>());
    langEN->SetText(L"🇺🇸 English");
    langEN->SetGroup(&langChoice, 1);
    langEN->SetTooltip(L"Switch to English");

    auto* langJP = card2->Add(std::make_unique<RadioButton>());
    langJP->SetText(L"🇯🇵 日本語");
    langJP->SetGroup(&langChoice, 2);
    langJP->SetTooltip(L"日本語に切り替え");

    auto* langStatus = card2->Add(std::make_unique<TextBlock>());
    langStatus->SetText(L"当前: 简体中文");
    langStatus->SetFontSize(12.0f);
    langStatus->SetMargin(Thickness{0, 8.0f, 0, 0});

    struct LangCtx { TextBlock* label; };
    auto* langCtx = new LangCtx{langStatus};
    auto updateLang = +[](void* owner, RadioButton&, int& value) {
        auto* ctx = static_cast<LangCtx*>(owner);
        const wchar_t* names[] = {L"简体中文", L"English", L"日本語"};
        if (value >= 0 && value < 3)
            ctx->label->SetText(std::wstring(L"当前: ") + names[value]);
    };
    pageSubs_.Keep(langCN->Selected().Subscribe(langCtx, updateLang));
    pageSubs_.Keep(langEN->Selected().Subscribe(langCtx, updateLang));
    pageSubs_.Keep(langJP->Selected().Subscribe(langCtx, updateLang));

    // 示例 3：单选组 - 文件保存格式
    auto* card3 = CreateExampleCard(content, L"单选组 3 - 文件格式（第三个独立分组）");
    auto* label3 = card3->Add(std::make_unique<TextBlock>());
    label3->SetText(L"选择保存格式：");
    label3->SetMargin(Thickness{0, 0, 0, 8.0f});

    static int formatChoice = 0;

    auto* formatJSON = card3->Add(std::make_unique<RadioButton>());
    formatJSON->SetText(L"📄 JSON (推荐)");
    formatJSON->SetGroup(&formatChoice, 0);
    formatJSON->SetTooltip(L"JSON 格式，易读易编辑");

    auto* formatXML = card3->Add(std::make_unique<RadioButton>());
    formatXML->SetText(L"📋 XML");
    formatXML->SetGroup(&formatChoice, 1);
    formatXML->SetTooltip(L"XML 格式");

    auto* formatBinary = card3->Add(std::make_unique<RadioButton>());
    formatBinary->SetText(L"💾 Binary");
    formatBinary->SetGroup(&formatChoice, 2);
    formatBinary->SetTooltip(L"二进制格式，体积小但不可读");

    // 示例 4：自定义样式
    auto* card4 = CreateExampleCard(content, L"自定义样式");

    auto* custom1 = card4->Add(std::make_unique<RadioButton>());
    custom1->SetText(L"大字号单选框");
    custom1->SetFontSize(18.0f);
    custom1->SetGroup(&formatChoice, 3);
    custom1->SetTooltip(L"字号 18pt");

    auto* custom2 = card4->Add(std::make_unique<RadioButton>());
    custom2->SetText(L"粗体单选框");
    custom2->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);
    custom2->SetGroup(&formatChoice, 4);
    custom2->SetTooltip(L"粗体文本");

    auto* custom3 = card4->Add(std::make_unique<RadioButton>());
    custom3->SetText(L"自定义颜色");
    custom3->SetForeground(D2D1::ColorF(0x9370DB, 1.0f));  // MediumPurple
    custom3->SetAccentColor(D2D1::ColorF(0xFF1493, 1.0f));  // DeepPink
    custom3->SetGroup(&formatChoice, 5);
    custom3->SetTooltip(L"紫色文字 + 粉色选中");

    // --- 样式定制（原「样式定制大全」706 页的内容，已并回本页）--------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");

        auto* sec1 = AddSubSection(card, L"AccentColor：五种语义色");
        AddNote(sec1, L"注意只有【选中】的那个按钮才显示强调色 —— 未选中的是空心圆环，"
                      L"所以这里每个颜色各用一个独立的组，让五个都处于选中态。");
        auto* row = AddRow(sec1, 18.0f);
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        static int rbGroups[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < n; ++i) {
            auto* rb = row->Add(std::make_unique<RadioButton>());
            rb->SetText(sw[i].label);
            rb->SetGroup(&rbGroups[i], 0);   // 每个自成一组，故都选中
            rb->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
        }

        auto* sec2 = AddSubSection(card, L"FontSize");
        static int fsGroup = 1;
        for (int i = 0; i < 3; ++i) {
            const float fs = 12.0f + i * 3.0f;
            auto* rb = sec2->Add(std::make_unique<RadioButton>());
            rb->SetText(std::wstring(L"FontSize ") + std::to_wstring(static_cast<int>(fs)));
            rb->SetGroup(&fsGroup, i);
            rb->SetHAlign(HAlign::Left);
            rb->SetFontSize(fs);
        }

        auto* sec3 = AddSubSection(card, L"禁用态");
        AddNote(sec3, L"填充与圆点保留色相、alpha ×0.4。");
        static int dGroup = 0;
        auto* drow = AddRow(sec3, 24.0f);
        for (int i = 0; i < 2; ++i) {
            auto* rb = drow->Add(std::make_unique<RadioButton>());
            rb->SetText(i == 0 ? L"禁用（选中）" : L"禁用（未选）");
            rb->SetGroup(&dGroup, i);
            rb->SetEnabled(false);
        }
    }

    CreateCodeExample(content, LR"(// RadioButton 分组用法
// 每个分组需要一个共享的 int 变量
static int themeChoice = 0;
static int langChoice = 0;  // 不同分组，独立的变量

// 分组 1：主题选择
auto* light = panel->Add(std::make_unique<RadioButton>());
light->SetText(L"浅色");
light->SetGroup(&themeChoice, 0);  // 值 0

auto* dark = panel->Add(std::make_unique<RadioButton>());
dark->SetText(L"深色");
dark->SetGroup(&themeChoice, 1);  // 值 1

// 分组 2：语言选择（独立）
auto* cn = panel->Add(std::make_unique<RadioButton>());
cn->SetText(L"中文");
cn->SetGroup(&langChoice, 0);  // 不同的分组变量

auto* en = panel->Add(std::make_unique<RadioButton>());
en->SetText(L"English");
en->SetGroup(&langChoice, 1);

// 自定义样式
radio->SetFontSize(18.0f);
radio->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);
radio->SetForeground(D2D1::ColorF(0x9370DB, 1.0f));
radio->SetAccentColor(D2D1::ColorF(0xFF1493, 1.0f));

// 事件订阅
radio->Selected().Subscribe(owner, [](void*, RadioButton&, int value) {
    // value 是选中的值（SetGroup 的第二个参数）
});)");

    return std::move(page);
}

} // namespace fluent
