// BorderThemePages.cpp — Border 演示页（505）与「主题驱动 vs 固定色」对照页（707）。
//
// 这个文件曾叫 StylingShowcasePages.cpp，装的是 800「样式定制大全」分类。那些页面是各
// 控件页的重复（801-804 是 102/103/201/302 的超集，706 是五个控件的样式演示汇总），已
// 全部并回各控件自己的「样式定制」卡片。剩下的两页留在这里：Border 是一个真实控件，
// 707 讲的是跨控件的取舍（固定了颜色就要自己保证两种主题下的对比度），不属于任何单个控件。
// 展示所有控件和布局的背景、边框、圆角、字体等定制能力
#include "../GalleryMain.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/Grid.h"
namespace fluent {

// ============================================================================
// Border 样式大全 — 边框、背景、圆角的所有组合
// ============================================================================
std::unique_ptr<ScrollPanel> GalleryApp::CreateBorderPage() {
    auto [page, content] = CreatePageShell(L"Border 样式大全");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(L"Border 是纯布局装饰器，提供背景填充、边框描边、圆角三种独立能力。"
                   L"每一项都可单独设置，组合出任意视觉风格。");
    intro->SetWrap(true);
    intro->SetMargin(Thickness{0, 0, 0, 16.0f});

    // 示例 1：边框粗细梯度
    auto* card1 = CreateExampleCard(content, L"边框粗细（0 / 1 / 2 / 4 / 8 DIP）");
    auto* row1 = card1->Add(std::make_unique<StackPanel>());
    row1->SetOrientation(StackPanel::Orientation::Horizontal);
    row1->SetSpacing(12.0f);

    const float thicknesses[] = {0.0f, 1.0f, 2.0f, 4.0f, 8.0f};
    for (float t : thicknesses) {
        auto* b = row1->Add(std::make_unique<Border>());
        b->SetWidth(110.0f);
        b->SetHeight(70.0f);
        b->SetBorderThickness(t);
        b->SetBorderColor(D2D1::ColorF(0x0078D4, 1.0f));
        b->SetCornerRadius(6.0f);
        b->SetPadding(Thickness{8.0f});
        auto* lbl = b->SetChild(std::make_unique<TextBlock>());
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f DIP", t);
        lbl->SetText(buf);
        lbl->SetVAlign(VAlign::Center);
        lbl->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 示例 2：圆角梯度
    auto* card2 = CreateExampleCard(content, L"圆角半径（0 / 4 / 8 / 16 / 32 DIP）");
    auto* row2 = card2->Add(std::make_unique<StackPanel>());
    row2->SetOrientation(StackPanel::Orientation::Horizontal);
    row2->SetSpacing(12.0f);

    const float radii[] = {0.0f, 4.0f, 8.0f, 16.0f, 32.0f};
    for (float r : radii) {
        auto* b = row2->Add(std::make_unique<Border>());
        b->SetWidth(110.0f);
        b->SetHeight(70.0f);
        b->SetBorderThickness(2.0f);
        b->SetBorderColor(D2D1::ColorF(0x107C10, 1.0f));
        b->SetBackground(D2D1::ColorF(0x107C10, 0.12f));
        b->SetCornerRadius(r);
        b->SetPadding(Thickness{8.0f});
        auto* lbl = b->SetChild(std::make_unique<TextBlock>());
        wchar_t buf[32];
        swprintf_s(buf, L"r = %.0f", r);
        lbl->SetText(buf);
        lbl->SetVAlign(VAlign::Center);
        lbl->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // 示例 3：配色方案
    auto* card3 = CreateExampleCard(content, L"配色方案（语义色卡）");
    auto* row3 = card3->Add(std::make_unique<StackPanel>());
    row3->SetOrientation(StackPanel::Orientation::Horizontal);
    row3->SetSpacing(12.0f);

    struct Swatch { const wchar_t* name; UINT32 stroke; UINT32 fill; };
    const Swatch swatches[] = {
        {L"Info",    0x0078D4, 0x0078D4},
        {L"Success", 0x107C10, 0x107C10},
        {L"Warning", 0xF7630C, 0xF7630C},
        {L"Danger",  0xC42B1C, 0xC42B1C},
        {L"Neutral", 0x8A8A8A, 0x8A8A8A},
        {L"Purple",  0x8764B8, 0x8764B8},
    };
    for (const auto& s : swatches) {
        auto* b = row3->Add(std::make_unique<Border>());
        b->SetWidth(140.0f);
        b->SetHeight(76.0f);
        b->SetBorderThickness(1.5f);
        b->SetBorderColor(D2D1::ColorF(s.stroke, 1.0f));
        b->SetBackground(D2D1::ColorF(s.fill, 0.14f));
        b->SetCornerRadius(8.0f);
        b->SetPadding(Thickness{10.0f});
        auto* stack = b->SetChild(std::make_unique<StackPanel>());
        stack->SetSpacing(2.0f);
        auto* title = stack->Add(std::make_unique<TextBlock>());
        title->SetText(s.name);
        title->SetFontSize(15.0f);
        title->SetWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
        title->SetForeground(D2D1::ColorF(s.stroke, 1.0f));
        auto* hex = stack->Add(std::make_unique<TextBlock>());
        wchar_t buf[32];
        swprintf_s(buf, L"#%06X", s.stroke);
        hex->SetText(buf);
        hex->SetFontSize(12.0f);
        hex->SetDimmed(true);
    }

    // 示例 4：嵌套边框
    auto* card4 = CreateExampleCard(content, L"嵌套 Border（三层）");
    auto* outer = card4->Add(std::make_unique<Border>());
    outer->SetWidth(320.0f);
    outer->SetBorderThickness(3.0f);
    outer->SetBorderColor(D2D1::ColorF(0xC42B1C, 1.0f));
    outer->SetCornerRadius(14.0f);
    outer->SetPadding(Thickness{10.0f});
    auto* middle = outer->SetChild(std::make_unique<Border>());
    middle->SetBorderThickness(2.0f);
    middle->SetBorderColor(D2D1::ColorF(0xF7630C, 1.0f));
    middle->SetCornerRadius(10.0f);
    middle->SetPadding(Thickness{10.0f});
    auto* inner = middle->SetChild(std::make_unique<Border>());
    inner->SetBorderThickness(1.0f);
    inner->SetBorderColor(D2D1::ColorF(0x0078D4, 1.0f));
    inner->SetBackground(D2D1::ColorF(0x0078D4, 0.10f));
    inner->SetCornerRadius(6.0f);
    inner->SetPadding(Thickness{12.0f});
    auto* innerText = inner->SetChild(std::make_unique<TextBlock>());
    innerText->SetText(L"三层嵌套，每层独立的粗细 / 颜色 / 圆角");
    innerText->SetWrap(true);
    innerText->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    CreateCodeExample(content, LR"(auto* border = panel->Add(std::make_unique<Border>());

// 背景填充（未设置 = 透明）
border->SetBackground(D2D1::ColorF(0x0078D4, 0.14f));   // 支持 alpha
border->ClearBackground();                              // 回到透明

// 边框描边：颜色 + 粗细各自独立
border->SetBorderColor(D2D1::ColorF(0x0078D4, 1.0f));
border->SetBorderThickness(2.0f);                       // 0 = 不描边

// 圆角（填充与描边共用同一半径）
border->SetCornerRadius(8.0f);

// 内边距：边框与子元素之间的间隙
border->SetPadding(Thickness{10.0f});
border->SetPadding(Thickness{16, 8, 16, 8});            // 左上右下

// 单一子元素（WPF Border 语义）
auto* child = border->SetChild(std::make_unique<TextBlock>());)");

    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateThemeVsCustomPage() {
    auto [page, content] = CreatePageShell(L"主题切换 vs 自定义样式");

    auto* intro = content->Add(std::make_unique<TextBlock>());
    intro->SetText(L"用右上角的 🌙 / ☀️ 按钮切换主题，观察两列的差异：左列没有任何 override，"
                   L"随主题整体翻转；右列固定了颜色，主题切换时保持不变。"
                   L"这正是 WPF 的属性优先级模型 —— 实例属性覆盖主题，未设置的部分继续跟随。");
    intro->SetWrap(true);
    intro->SetMargin(Thickness{0, 0, 0, 16.0f});

    auto* card = CreateExampleCard(content, L"左：主题驱动　|　右：固定自定义色");
    auto* twoCol = card->Add(std::make_unique<Grid>());
    twoCol->SetColumns({GridLength::Star(), GridLength::Star()});
    twoCol->SetRows({GridLength::Auto()});

    auto* leftCol = twoCol->Add(std::make_unique<StackPanel>());
    twoCol->SetCell(leftCol, 0, 0);
    leftCol->SetSpacing(10.0f);
    leftCol->SetMargin(Thickness{0, 0, 12.0f, 0});

    auto* leftTitle = leftCol->Add(std::make_unique<TextBlock>());
    leftTitle->SetText(L"跟随主题");
    leftTitle->SetFontSize(15.0f);
    leftTitle->SetWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);

    auto* lb = leftCol->Add(std::make_unique<Button>());
    lb->SetText(L"Standard");
    auto* la = leftCol->Add(std::make_unique<Button>());
    la->SetText(L"Accent");
    la->SetKind(Button::Kind::Accent);
    auto* lc = leftCol->Add(std::make_unique<CheckBox>());
    lc->SetText(L"CheckBox");
    lc->SetChecked(true);
    auto* lt = leftCol->Add(std::make_unique<ToggleSwitch>());
    lt->SetText(L"ToggleSwitch");
    lt->SetOn(true);
    auto* lx = leftCol->Add(std::make_unique<TextBox>());
    lx->SetText(L"TextBox");
    auto* lp = leftCol->Add(std::make_unique<ProgressBar>());
    lp->SetValue(0.6f);

    auto* rightCol = twoCol->Add(std::make_unique<StackPanel>());
    twoCol->SetCell(rightCol, 0, 1);
    rightCol->SetSpacing(10.0f);

    auto* rightTitle = rightCol->Add(std::make_unique<TextBlock>());
    rightTitle->SetText(L"固定自定义色");
    rightTitle->SetFontSize(15.0f);
    rightTitle->SetWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
    rightTitle->SetForeground(D2D1::ColorF(0x8764B8, 1.0f));

    auto* rb2 = rightCol->Add(std::make_unique<Button>());
    rb2->SetText(L"Standard");
    rb2->SetBackground(D2D1::ColorF(0x2D2D30, 1.0f));
    rb2->SetForeground(D2D1::ColorF(0xE8E8E8, 1.0f));
    rb2->SetBorderBrush(D2D1::ColorF(0x8764B8, 1.0f));
    rb2->SetBorderThickness(1.5f);

    auto* ra = rightCol->Add(std::make_unique<Button>());
    ra->SetText(L"Accent");
    ra->SetKind(Button::Kind::Accent);
    ra->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));

    // 只设 AccentColor，不设 Foreground —— 这里曾经两个都设成紫色，结果勾不见了。
    //
    // 原因：对 toggle 类控件，Foreground 是「画在 accent 填充上的记号」的颜色
    // （CheckBox 的勾、RadioButton 的点、ToggleSwitch 的旋钮），不是标签文字色。
    // 紫底上画紫勾 = 看不见。库的行为是对的：调用者显式要求前景是紫色，库照做。
    //
    // 这一页自己讲的就是「只固定语义色，其余交给主题」，而它自己的代码违反了这条。
    auto* rc = rightCol->Add(std::make_unique<CheckBox>());
    rc->SetText(L"CheckBox");
    rc->SetChecked(true);
    rc->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));

    auto* rt = rightCol->Add(std::make_unique<ToggleSwitch>());
    rt->SetText(L"ToggleSwitch");
    rt->SetOn(true);
    rt->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));

    auto* rx = rightCol->Add(std::make_unique<TextBox>());
    rx->SetText(L"TextBox");
    rx->SetBackground(D2D1::ColorF(0x1E1E1E, 1.0f));
    rx->SetForeground(D2D1::ColorF(0xD4D4D4, 1.0f));
    rx->SetBorderBrush(D2D1::ColorF(0x8764B8, 1.0f));
    rx->SetBorderThickness(1.5f);

    auto* rp = rightCol->Add(std::make_unique<ProgressBar>());
    rp->SetValue(0.6f);
    rp->SetAccentColor(D2D1::ColorF(0x8764B8, 1.0f));

    auto* note = content->Add(std::make_unique<TextBlock>());
    note->SetText(L"⚠️ 注意右列在浅色主题下的可读性：一旦固定了颜色，就要自己保证两种主题下的对比度。"
                  L"通常的做法是只固定 AccentColor（语义色），把 Foreground / Background 留给主题。");
    note->SetWrap(true);
    note->SetFontSize(12.0f);
    note->SetMargin(Thickness{0, 8.0f, 0, 0});

    CreateCodeExample(content, LR"(// 推荐做法：只固定语义色，其余交给主题
btn->SetAccentColor(D2D1::ColorF(0xC42B1C, 1.0f));   // "危险" 语义，两种主题都成立
// 不设置 Foreground / Background —— 让它们随主题翻转

// 不推荐：把前景背景都钉死
btn->SetBackground(D2D1::ColorF(0x1E1E1E, 1.0f));    // 浅色主题下会突兀
btn->SetForeground(D2D1::ColorF(0xD4D4D4, 1.0f));    // 需要自己处理两套配色

// 主题切换的实现（宿主窗口侧）
window->SetDarkMode(true);
// ThemeManager 就地覆写同一个 ThemeSnapshot 并递增 generation，
// 所以挂在每个 UIContext 上的指针始终有效，只有指向的内容变了。
// 没有 override 的控件下一帧就用上新 token；有 override 的保持原样。)");

    return std::move(page);
}

} // namespace fluent
