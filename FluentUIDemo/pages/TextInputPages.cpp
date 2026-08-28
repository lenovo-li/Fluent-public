// TextInputPages.cpp — TextBox 与 TextArea 的展示页（nav 201 / 302）
//
// 同 CheckRadioPages.cpp：原「完整演示」页已并回控件自己的 nav id，
// 不再与基础页并列。
#include "../GalleryMain.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/Grid.h"

namespace fluent {

// ============================================================================
// 增强的 TextBox 页面 - 展示所有样式和输入过滤
// ============================================================================
std::unique_ptr<ScrollPanel> GalleryApp::CreateTextBoxPage() {
    auto [page, content] = CreatePageShell(L"TextBox 完整演示");

    // 示例 1：字体样式
    auto* card1 = CreateExampleCard(content, L"字体样式和大小");

    auto* label1a = card1->Add(std::make_unique<TextBlock>());
    label1a->SetText(L"字号 12pt：");
    label1a->SetFontSize(12.0f);
    label1a->SetMargin(Thickness{0, 0, 0, 4.0f});
    auto* text1a = card1->Add(std::make_unique<TextBox>());
    text1a->SetWidth(360.0f);
    text1a->SetFontSize(12.0f);
    text1a->SetPlaceholder(L"小字号输入框");
    text1a->SetTooltip(L"12pt 字号");

    auto* label1b = card1->Add(std::make_unique<TextBlock>());
    label1b->SetText(L"字号 16pt：");
    label1b->SetFontSize(12.0f);
    label1b->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text1b = card1->Add(std::make_unique<TextBox>());
    text1b->SetWidth(360.0f);
    text1b->SetFontSize(16.0f);
    text1b->SetPlaceholder(L"中字号输入框");
    text1b->SetTooltip(L"16pt 字号");

    auto* label1c = card1->Add(std::make_unique<TextBlock>());
    label1c->SetText(L"字号 20pt + 粗体：");
    label1c->SetFontSize(12.0f);
    label1c->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text1c = card1->Add(std::make_unique<TextBox>());
    text1c->SetWidth(360.0f);
    text1c->SetFontSize(20.0f);
    text1c->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);
    text1c->SetPlaceholder(L"大字号粗体输入框");
    text1c->SetTooltip(L"20pt 粗体");

    // 示例 2：自定义颜色和边框
    auto* card2 = CreateExampleCard(content, L"自定义颜色和边框");

    auto* label2a = card2->Add(std::make_unique<TextBlock>());
    label2a->SetText(L"自定义背景色和前景色：");
    label2a->SetFontSize(12.0f);
    label2a->SetMargin(Thickness{0, 0, 0, 4.0f});
    auto* text2a = card2->Add(std::make_unique<TextBox>());
    text2a->SetWidth(360.0f);
    text2a->SetBackground(D2D1::ColorF(0x2C2C2C, 1.0f));  // 深灰背景
    text2a->SetForeground(D2D1::ColorF(0x00FF00, 1.0f));  // 绿色文字
    text2a->SetText(L"Matrix 风格");
    text2a->SetTooltip(L"深灰背景 + 绿色文字");

    auto* label2b = card2->Add(std::make_unique<TextBlock>());
    label2b->SetText(L"自定义边框颜色和粗细：");
    label2b->SetFontSize(12.0f);
    label2b->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text2b = card2->Add(std::make_unique<TextBox>());
    text2b->SetWidth(360.0f);
    text2b->SetBorderBrush(D2D1::ColorF(0xFF1493, 1.0f));  // DeepPink
    text2b->SetBorderThickness(3.0f);
    text2b->SetPlaceholder(L"粉色粗边框");
    text2b->SetTooltip(L"3px 粉色边框");

    auto* label2c = card2->Add(std::make_unique<TextBlock>());
    label2c->SetText(L"圆角边框：");
    label2c->SetFontSize(12.0f);
    label2c->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text2c = card2->Add(std::make_unique<TextBox>());
    text2c->SetWidth(360.0f);
    text2c->SetCornerRadius(16.0f);
    text2c->SetPlaceholder(L"大圆角输入框");
    text2c->SetTooltip(L"16px 圆角");

    // 示例 3：输入约束
    auto* card3 = CreateExampleCard(content, L"输入约束和过滤");

    auto* label3a = card3->Add(std::make_unique<TextBlock>());
    label3a->SetText(L"最大长度限制（10 字符）：");
    label3a->SetFontSize(12.0f);
    label3a->SetMargin(Thickness{0, 0, 0, 4.0f});
    auto* text3a = card3->Add(std::make_unique<TextBox>());
    text3a->SetWidth(360.0f);
    text3a->SetMaxLength(10);
    text3a->SetPlaceholder(L"最多 10 个字符");
    text3a->SetTooltip(L"超过 10 字符无法输入");

    auto* label3b = card3->Add(std::make_unique<TextBlock>());
    label3b->SetText(L"纯数字输入框（过滤非数字）：");
    label3b->SetFontSize(12.0f);
    label3b->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text3b = card3->Add(std::make_unique<TextBox>());
    text3b->SetWidth(360.0f);
    text3b->SetPlaceholder(L"只能输入数字");
    text3b->SetInputFilter([](wchar_t ch) {
        return ch >= L'0' && ch <= L'9';
    });
    text3b->SetTooltip(L"输入过滤器：只接受 0-9");

    auto* label3c = card3->Add(std::make_unique<TextBlock>());
    label3c->SetText(L"强制大写：");
    label3c->SetFontSize(12.0f);
    label3c->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text3c = card3->Add(std::make_unique<TextBox>());
    text3c->SetWidth(360.0f);
    text3c->SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    text3c->SetPlaceholder(L"自动转大写");
    text3c->SetTooltip(L"所有输入自动转为大写");

    auto* label3d = card3->Add(std::make_unique<TextBlock>());
    label3d->SetText(L"强制小写：");
    label3d->SetFontSize(12.0f);
    label3d->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* text3d = card3->Add(std::make_unique<TextBox>());
    text3d->SetWidth(360.0f);
    text3d->SetCharacterCasing(TextEditBase::CharacterCasing::Lower);
    text3d->SetPlaceholder(L"自动转小写");
    text3d->SetTooltip(L"所有输入自动转为小写");

    // 示例 4：多样化示例（综合）
    auto* card4 = CreateExampleCard(content, L"综合样式示例");

    auto* text4a = card4->Add(std::make_unique<TextBox>());
    text4a->SetWidth(360.0f);
    text4a->SetFontSize(18.0f);
    text4a->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
    text4a->SetBackground(D2D1::ColorF(0x1E1E1E, 1.0f));
    text4a->SetForeground(D2D1::ColorF(0xD4D4D4, 1.0f));
    text4a->SetBorderBrush(D2D1::ColorF(0x007ACC, 1.0f));
    text4a->SetBorderThickness(2.0f);
    text4a->SetCornerRadius(8.0f);
    text4a->SetText(L"VS Code 风格");
    text4a->SetTooltip(L"深色主题 + 蓝色边框");

    auto* text4b = card4->Add(std::make_unique<TextBox>());
    text4b->SetWidth(360.0f);
    text4b->SetFontSize(16.0f);
    text4b->SetBackground(D2D1::ColorF(0xFFF8DC, 1.0f));  // Cornsilk
    text4b->SetForeground(D2D1::ColorF(0x8B4513, 1.0f));  // SaddleBrown
    text4b->SetBorderBrush(D2D1::ColorF(0xD2691E, 1.0f));  // Chocolate
    text4b->SetBorderThickness(2.0f);
    text4b->SetCornerRadius(4.0f);
    text4b->SetText(L"暖色调风格");
    text4b->SetTooltip(L"米色背景 + 棕色文字");

    CreateCodeExample(content, LR"(// TextBox 完整样式定制
auto* text = panel->Add(std::make_unique<TextBox>());
text->SetWidth(360.0f);

// 字体
text->SetFontSize(18.0f);
text->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);

// 颜色
text->SetBackground(D2D1::ColorF(0x1E1E1E, 1.0f));
text->SetForeground(D2D1::ColorF(0xD4D4D4, 1.0f));
text->SetBorderBrush(D2D1::ColorF(0x007ACC, 1.0f));

// 边框和圆角
text->SetBorderThickness(2.0f);
text->SetCornerRadius(8.0f);

// 输入约束
text->SetMaxLength(50);
text->SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
text->SetInputFilter([](wchar_t ch) {
    return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z');
});

// 事件
text->TextChanged().Subscribe(owner, [](void*, TextEditBase&, std::wstring& txt) {
    // 文本变化处理
});)");

    return std::move(page);
}

// ============================================================================
// 增强的 TextArea 页面 - 展示所有样式和高级功能
// ============================================================================
std::unique_ptr<ScrollPanel> GalleryApp::CreateTextAreaPage() {
    auto [page, content] = CreatePageShell(L"TextArea 完整演示");

    // 示例 1：字体样式
    auto* card1 = CreateExampleCard(content, L"字体样式");

    auto* label1a = card1->Add(std::make_unique<TextBlock>());
    label1a->SetText(L"字号 12pt：");
    label1a->SetFontSize(12.0f);
    label1a->SetMargin(Thickness{0, 0, 0, 4.0f});
    auto* area1a = card1->Add(std::make_unique<TextArea>());
    area1a->SetHeight(120.0f);
    area1a->SetFontSize(12.0f);
    area1a->SetText(L"字号 12pt 的多行文本编辑器。\n支持多行输入。\nCtrl+Z 撤销，Ctrl+Y 重做。");
    area1a->SetTooltip(L"12pt 字号");

    auto* label1b = card1->Add(std::make_unique<TextBlock>());
    label1b->SetText(L"字号 16pt + 粗体：");
    label1b->SetFontSize(12.0f);
    label1b->SetMargin(Thickness{0, 12.0f, 0, 4.0f});
    auto* area1b = card1->Add(std::make_unique<TextArea>());
    area1b->SetHeight(120.0f);
    area1b->SetFontSize(16.0f);
    area1b->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD);
    area1b->SetText(L"字号 16pt 粗体文本编辑器。\n可以设置字体粗细。");
    area1b->SetTooltip(L"16pt 粗体");

    // 示例 2：代码编辑器样式
    auto* card2 = CreateExampleCard(content, L"代码编辑器样式");

    auto* label2 = card2->Add(std::make_unique<TextBlock>());
    label2->SetText(L"深色主题代码编辑器（VS Code 风格）：");
    label2->SetFontSize(12.0f);
    label2->SetMargin(Thickness{0, 0, 0, 4.0f});

    auto* code = card2->Add(std::make_unique<TextArea>());
    code->SetHeight(200.0f);
    code->SetWrapMode(TextWrapMode::NoWrap);
    code->SetFontSize(13.0f);
    code->SetBackground(D2D1::ColorF(0x1E1E1E, 1.0f));
    code->SetForeground(D2D1::ColorF(0xD4D4D4, 1.0f));
    code->SetBorderBrush(D2D1::ColorF(0x007ACC, 1.0f));
    code->SetBorderThickness(1.0f);
    code->SetText(
        L"void Measure(float availW, float availH) override {\n"
        L"    // 测量子元素\n"
        L"    if (content_) {\n"
        L"        content_->MeasureCached(availW, availH);\n"
        L"        desired_ = content_->Desired();\n"
        L"    }\n"
        L"}\n"
    );
    code->SetTooltip(L"深色代码编辑器");

    // 示例 3：浅色主题
    auto* label3 = card2->Add(std::make_unique<TextBlock>());
    label3->SetText(L"浅色主题编辑器：");
    label3->SetFontSize(12.0f);
    label3->SetMargin(Thickness{0, 12.0f, 0, 4.0f});

    auto* light = card2->Add(std::make_unique<TextArea>());
    light->SetHeight(150.0f);
    light->SetWrapMode(TextWrapMode::Wrap);
    light->SetFontSize(14.0f);
    light->SetBackground(D2D1::ColorF(0xFFFFF0, 1.0f));  // Ivory
    light->SetForeground(D2D1::ColorF(0x000000, 1.0f));
    light->SetBorderBrush(D2D1::ColorF(0xD3D3D3, 1.0f));
    light->SetBorderThickness(1.0f);
    light->SetText(L"浅色主题的多行编辑器。\n象牙白背景，黑色文字。\n适合长时间阅读。");
    light->SetTooltip(L"浅色编辑器");

    // 示例 4：特殊样式
    auto* card3 = CreateExampleCard(content, L"特殊样式演示");

    auto* terminal = card3->Add(std::make_unique<TextArea>());
    terminal->SetHeight(180.0f);
    terminal->SetWrapMode(TextWrapMode::NoWrap);
    terminal->SetFontSize(13.0f);
    terminal->SetBackground(D2D1::ColorF(0x0C0C0C, 1.0f));  // 接近黑色
    terminal->SetForeground(D2D1::ColorF(0x00FF00, 1.0f));  // 绿色
    terminal->SetBorderBrush(D2D1::ColorF(0x00FF00, 0.5f));
    terminal->SetBorderThickness(2.0f);
    terminal->SetCornerRadius(4.0f);
    terminal->SetReadOnly(true);
    terminal->SetText(
        L"$ ls -la\n"
        L"total 128\n"
        L"drwxr-xr-x  15 user  staff   480 Aug 14 10:30 .\n"
        L"drwxr-xr-x   8 user  staff   256 Aug 13 15:20 ..\n"
        L"-rw-r--r--   1 user  staff  2048 Aug 14 10:28 README.md\n"
        L"drwxr-xr-x  10 user  staff   320 Aug 14 09:15 src\n"
    );
    terminal->SetTooltip(L"终端风格（只读）");

    auto* paper = card3->Add(std::make_unique<TextArea>());
    paper->SetHeight(180.0f);
    paper->SetWrapMode(TextWrapMode::Wrap);
    paper->SetFontSize(15.0f);
    paper->SetBackground(D2D1::ColorF(0xFFFAF0, 1.0f));  // FloralWhite
    paper->SetForeground(D2D1::ColorF(0x2F4F4F, 1.0f));  // DarkSlateGray
    paper->SetBorderBrush(D2D1::ColorF(0xDEB887, 1.0f));  // BurlyWood
    paper->SetBorderThickness(3.0f);
    paper->SetCornerRadius(0.0f);
    paper->SetText(L"纸张风格的编辑器。\n\n米白色背景，深灰色文字，木色边框。\n\n适合书写长篇文档。");
    paper->SetTooltip(L"纸张风格");

    CreateCodeExample(content, LR"(// TextArea 完整样式定制
auto* area = panel->Add(std::make_unique<TextArea>());
area->SetHeight(200.0f);

// 字体
area->SetFontSize(13.0f);
area->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL);

// 颜色（代码编辑器风格）
area->SetBackground(D2D1::ColorF(0x1E1E1E, 1.0f));
area->SetForeground(D2D1::ColorF(0xD4D4D4, 1.0f));
area->SetBorderBrush(D2D1::ColorF(0x007ACC, 1.0f));

// 边框和圆角
area->SetBorderThickness(1.0f);
area->SetCornerRadius(4.0f);

// 换行模式
area->SetWrapMode(TextWrapMode::NoWrap);  // 或 Wrap

// 输入约束
area->SetReadOnly(false);
area->SetMaxLength(10000);

// Log 模式
area->SetAutoScrollToTail(true);
area->AppendText(L"新的日志行\n");

// 事件
area->TextChanged().Subscribe(owner, [](void*, TextEditBase&, std::wstring& txt) {
    // 文本变化处理
});)");

    return std::move(page);
}

} // namespace fluent
