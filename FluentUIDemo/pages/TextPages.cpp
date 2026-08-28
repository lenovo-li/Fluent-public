// TextPages.cpp — 文本控件页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/PasswordBox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/Button.h"
#include <thread>
#include <chrono>

namespace fluent {


// CreatePasswordBoxPage 的实现在 pages/PasswordBoxPage.cpp。

std::unique_ptr<ScrollPanel> GalleryApp::CreateTextBlockPage() {
    auto [page, content] = CreatePageShell(L"TextBlock");

    // --- 1. 语义字号（推荐） -----------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"排版角色：跟随主题的字号阶梯");
        AddNote(card, L"优先用 SetTypographyRole 而不是 SetFontSize —— 角色从主题的 "
                      L"TypographyTokens 解析出实际字号，所以换主题/换 DPI 时整套文字"
                      L"一起协调变化。写死 pt 值等于把自己钉在一套字号上。");

        struct RoleDemo { const wchar_t* label; TypographyRole role; };
        const RoleDemo roles[] = {
            {L"Title —— 页面标题", TypographyRole::Title},
            {L"Subtitle —— 区块标题", TypographyRole::Subtitle},
            {L"Body —— 正文（默认）", TypographyRole::Body},
            {L"Caption —— 辅助说明", TypographyRole::Caption},
        };
        for (const RoleDemo& r : roles) {
            auto* t = card->Add(std::make_unique<TextBlock>());
            t->SetText(r.label);
            t->SetTypographyRole(r.role);
        }

        auto* custom = card->Add(std::make_unique<TextBlock>());
        custom->SetText(L"Custom —— SetFontSize(26) 会把角色切成 Custom");
        custom->SetFontSize(26.0f);
        AddNote(card, L"注意 SetFontSize 的副作用：它把角色置为 Custom，此后不再跟随主题。"
                      L"ClearFontSize() 可以退回 Body 并同时清掉 Control 层的显式值 —— "
                      L"只清一处会让通用代码读到「跟随主题」而实际仍按旧尺寸绘制。");
    }

    // --- 2. 换行与布局 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"换行：宽度决定高度");
        AddNote(card, L"SetWrap(true) 之后，控件的 desired 高度取决于可用宽度 —— "
                      L"同一段文字在窄容器里更高。这就是为什么放文字的行必须用 "
                      L"GridLength::Auto()，写死行高会把末行切掉。");

        const wchar_t* para =
            L"本框架是从零开始的 Win32 + Direct2D/DirectWrite/DirectComposition "
            L"保留模式 UI 框架。这段文字用来演示换行：把容器调窄，它会占更多行。";
        for (float w : {620.0f, 400.0f, 240.0f}) {
            auto* box = AddBoundedBox(card, w,
                std::wstring(L"容器宽 ") + std::to_wstring(static_cast<int>(w)) + L" DIP");
            auto* t = box->Add(std::make_unique<TextBlock>());
            t->SetText(para);
            t->SetWrap(true);
        }

        auto* sec = AddSubSection(card, L"不换行时超出容器会被裁");
        AddNote(sec, L"SetWrap(false)（默认）时文字是单行的，容器不够宽就被裁掉 —— "
                     L"不会溢出、不会加省略号，这是框架的 Arrange 契约。");
        auto* box = AddBoundedBox(sec, 240.0f, L"容器 240 DIP，未开换行");
        auto* clipped = box->Add(std::make_unique<TextBlock>());
        clipped->SetText(para);
    }

    // --- 3. 对齐与样式 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"对齐、字重、行距");

        auto* sec1 = AddSubSection(card, L"段落内对齐（DirectWrite 枚举）");
        AddNote(sec1, L"SetAlignment 收的是 DWRITE_TEXT_ALIGNMENT，不是框架自己的枚举。"
                      L"它是 Render 级 —— 对齐不改变 DWrite 报告的 metrics。");
        struct AlignDemo { const wchar_t* label; DWRITE_TEXT_ALIGNMENT a; };
        const AlignDemo aligns[] = {
            {L"LEADING（左）", DWRITE_TEXT_ALIGNMENT_LEADING},
            {L"CENTER（居中）", DWRITE_TEXT_ALIGNMENT_CENTER},
            {L"TRAILING（右）", DWRITE_TEXT_ALIGNMENT_TRAILING},
        };
        for (const AlignDemo& d : aligns) {
            auto* b = AddBoundedBox(sec1, 420.0f, d.label);
            auto* t = b->Add(std::make_unique<TextBlock>());
            t->SetText(L"这一行文字演示段落内的水平对齐。");
            t->SetAlignment(d.a);
        }

        auto* sec2 = AddSubSection(card, L"字重");
        AddNote(sec2, L"字重会改变文字宽度，所以是 Measure 级。SemiBold 比 Normal 宽 —— "
                      L"曾经有过一个 bug：测量时写死 Normal 而绘制用 SemiBold，导致标题"
                      L"溢出自己申请的框。");
        for (auto [label, w] : {
                 std::pair<const wchar_t*, DWRITE_FONT_WEIGHT>{L"Light", DWRITE_FONT_WEIGHT_LIGHT},
                 {L"Normal（默认）", DWRITE_FONT_WEIGHT_NORMAL},
                 {L"SemiBold", DWRITE_FONT_WEIGHT_SEMI_BOLD},
                 {L"Bold", DWRITE_FONT_WEIGHT_BOLD}}) {
            auto* t = sec2->Add(std::make_unique<TextBlock>());
            t->SetText(label);
            t->SetWeight(w);
        }

        auto* sec3 = AddSubSection(card, L"Dimmed：次要文字");
        AddNote(sec3, L"Dimmed 走主题的 textSecondary，而不是给 Foreground 硬编码一个灰值 —— "
                      L"深色主题下硬编码的灰会看不见。");
        auto* normal = sec3->Add(std::make_unique<TextBlock>());
        normal->SetText(L"普通文字（textPrimary）");
        auto* dim = sec3->Add(std::make_unique<TextBlock>());
        dim->SetText(L"次要文字（textSecondary）");
        dim->SetDimmed(true);

        auto* sec4 = AddSubSection(card, L"行距倍数");
        for (float f : {1.0f, 1.4f, 1.8f}) {
            auto* b = AddBoundedBox(sec4, 420.0f,
                std::wstring(L"LineSpacing ") + (f == 1.0f ? L"1.0" : (f == 1.4f ? L"1.4" : L"1.8")));
            auto* t = b->Add(std::make_unique<TextBlock>());
            t->SetText(L"多行文本的行距演示。行距是 Measure 级，因为它改变整段的高度。"
                       L"这一段刻意写长一点以便换行。");
            t->SetWrap(true);
            t->SetLineSpacing(f);
        }
    }

    // --- 4. 可选中 -------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"可选中的只读文本");
        AddNote(card, L"TextBlock 默认是纯静态标签：不可聚焦、无 I 形光标、不能选中。"
                      L"SetSelectable(true) 之后它会接受焦点、支持鼠标选中与滚轮滚动 —— "
                      L"适合设备详情、日志片段这类「只读但需要复制」的内容。");
        auto* sel = card->Add(std::make_unique<TextBlock>());
        sel->SetText(L"这段文字可以用鼠标选中并复制（Ctrl+C）。"
                     L"SetSelectable(true) 同时把控件变成可聚焦的。");
        sel->SetWrap(true);
        sel->SetSelectable(true);
    }

    CreateCodeExample(content, LR"(auto* text = panel->Add(std::make_unique<TextBlock>());
text->SetText(L"区块标题");

// 优先用语义角色，让字号跟随主题：
text->SetTypographyRole(TypographyRole::Subtitle);
// 而不是 text->SetFontSize(20.0f)（会把角色切成 Custom，不再跟随主题）

text->SetWrap(true);              // 开启换行：高度将取决于可用宽度
text->SetDimmed(true);            // 次要文字，走 textSecondary
text->SetWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);
text->SetAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);   // DirectWrite 枚举
text->SetSelectable(true);        // 允许选中复制（同时变为可聚焦）)");
    return std::move(page);
}

} // namespace fluent
