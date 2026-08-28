// PasswordBoxPage.cpp — PasswordBox 控件演示页
#include "../GalleryMain.h"
#include "../../FluentUI/controls/PasswordBox.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/ScrollPanel.h"

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreatePasswordBoxPage() {
    auto [page, content] = CreatePageShell(L"PasswordBox");

    // 示例 1: 基础用法
    auto* card1 = CreateExampleCard(content, L"Basic PasswordBox");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"PasswordBox 是专门用于密码输入的控件，默认以掩码字符显示输入内容。");
    desc1->SetWrap(true);
    desc1->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* pwd1 = card1->Add(std::make_unique<PasswordBox>());
    pwd1->SetPlaceholder(L"请输入密码");
    pwd1->SetWidth(300.0f);

    // 示例 2: 带最大长度限制
    auto* card2 = CreateExampleCard(content, L"With MaxLength");
    auto* desc2 = card2->Add(std::make_unique<TextBlock>());
    desc2->SetText(L"限制密码最大长度为 16 个字符。");
    desc2->SetWrap(true);
    desc2->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* pwd2 = card2->Add(std::make_unique<PasswordBox>());
    pwd2->SetPlaceholder(L"最多 16 个字符");
    pwd2->SetWidth(300.0f);
    pwd2->SetMaxLength(16);

    // 示例 3: 禁用状态
    auto* card3 = CreateExampleCard(content, L"Disabled State");
    auto* pwd3 = card3->Add(std::make_unique<PasswordBox>());
    pwd3->SetPlaceholder(L"已禁用");
    pwd3->SetWidth(300.0f);
    pwd3->SetEnabled(false);

    // 示例 4: 只读状态
    auto* card4 = CreateExampleCard(content, L"Read-Only State");
    auto* pwd4 = card4->Add(std::make_unique<PasswordBox>());
    pwd4->SetText(L"readonly123");
    pwd4->SetWidth(300.0f);
    pwd4->SetReadOnly(true);

    // 示例 5: 密码强度指示
    auto* card5 = CreateExampleCard(content, L"Password Strength Indicator");
    auto* desc5 = card5->Add(std::make_unique<TextBlock>());
    desc5->SetText(L"输入密码时动态显示强度评估（演示功能）。");
    desc5->SetWrap(true);
    desc5->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* pwd5 = card5->Add(std::make_unique<PasswordBox>());
    pwd5->SetPlaceholder(L"输入密码查看强度");
    pwd5->SetWidth(300.0f);

    auto* strengthLabel = card5->Add(std::make_unique<TextBlock>());
    strengthLabel->SetText(L"强度：未输入");
    strengthLabel->SetMargin(Thickness{0, 8.0f, 0, 0});

    // 密码强度逻辑（简化演示）
    pageSubs_ += pwd5->TextChanged().Subscribe(strengthLabel, [](void* owner, TextEditBase& box, std::wstring&) {
        auto* label = static_cast<TextBlock*>(owner);
        const auto& text = box.Text();
        if (text.empty()) {
            label->SetText(L"强度：未输入");
        } else if (text.size() < 6) {
            label->SetText(L"强度：弱 (少于 6 个字符)");
        } else if (text.size() < 10) {
            label->SetText(L"强度：中 (6-9 个字符)");
        } else {
            label->SetText(L"强度：强 (10+ 个字符)");
        }
    });

    // 示例 6: 表单场景
    auto* card6 = CreateExampleCard(content, L"Form Example");
    auto* formStack = card6->Add(std::make_unique<StackPanel>());
    formStack->SetSpacing(12.0f);

    auto* label1 = formStack->Add(std::make_unique<TextBlock>());
    label1->SetText(L"当前密码：");

    auto* currentPwd = formStack->Add(std::make_unique<PasswordBox>());
    currentPwd->SetPlaceholder(L"输入当前密码");
    currentPwd->SetWidth(300.0f);

    auto* label2 = formStack->Add(std::make_unique<TextBlock>());
    label2->SetText(L"新密码：");
    label2->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* newPwd = formStack->Add(std::make_unique<PasswordBox>());
    newPwd->SetPlaceholder(L"输入新密码");
    newPwd->SetWidth(300.0f);
    newPwd->SetMaxLength(32);

    auto* label3 = formStack->Add(std::make_unique<TextBlock>());
    label3->SetText(L"确认密码：");
    label3->SetMargin(Thickness{0, 8.0f, 0, 0});

    auto* confirmPwd = formStack->Add(std::make_unique<PasswordBox>());
    confirmPwd->SetPlaceholder(L"再次输入新密码");
    confirmPwd->SetWidth(300.0f);
    confirmPwd->SetMaxLength(32);

    auto* submitBtn = formStack->Add(std::make_unique<Button>());
    submitBtn->SetText(L"更改密码");
    submitBtn->SetKind(Button::Kind::Accent);
    submitBtn->SetMargin(Thickness{0, 12.0f, 0, 0});

    CreateCodeExample(content, LR"(auto passwordBox = std::make_unique<PasswordBox>();
passwordBox->SetPlaceholder(L"请输入密码");
passwordBox->SetWidth(300.0f);
passwordBox->SetMaxLength(32);

// 监听密码变化
passwordBox->TextChanged().Subscribe(
    this, [](void* owner, TextEditBase& box, const std::wstring& text) {
        // 处理密码输入
    });)");

    return std::move(page);
}

} // namespace fluent
