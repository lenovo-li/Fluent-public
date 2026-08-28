// GroupBoxPage.cpp — GroupBox 布局容器演示页
#include "../GalleryMain.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/RadioButton.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/base/Event.h"  // SubscriptionBag

namespace fluent {

// Page-level state to keep subscriptions alive.
struct GroupBoxPageState {
    SubscriptionBag subs;
};

std::unique_ptr<ScrollPanel> GalleryApp::CreateGroupBoxPage() {
    auto [page, content] = CreatePageShell(L"GroupBox");

    // State lives as long as the page (owned by the lambda captures).
    auto* state = new GroupBoxPageState();

    // 示例 1: 基础用法
    auto* card1 = CreateExampleCard(content, L"Basic GroupBox with Header");
    auto* desc1 = card1->Add(std::make_unique<TextBlock>());
    desc1->SetText(L"GroupBox 为一组相关控件提供带标题的边框容器。");
    desc1->SetWrap(true);
    desc1->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* groupBox1 = card1->Add(std::make_unique<GroupBox>());
    groupBox1->SetHeader(L"个人信息");
    groupBox1->SetWidth(400.0f);

    auto childContent1 = std::make_unique<StackPanel>();
    childContent1->SetSpacing(8.0f);
    childContent1->SetMargin(Thickness{12.0f});

    auto* name = childContent1->Add(std::make_unique<TextBlock>());
    name->SetText(L"姓名: 张三");

    auto* age = childContent1->Add(std::make_unique<TextBlock>());
    age->SetText(L"年龄: 25");

    auto* city = childContent1->Add(std::make_unique<TextBlock>());
    city->SetText(L"城市: 北京");

    groupBox1->SetChild(std::move(childContent1));

    // 示例 2: CheckBox 组
    auto* card2 = CreateExampleCard(content, L"GroupBox with CheckBoxes");
    auto* groupBox2 = card2->Add(std::make_unique<GroupBox>());
    groupBox2->SetHeader(L"功能选项");
    groupBox2->SetWidth(400.0f);

    auto childContent2 = std::make_unique<StackPanel>();
    childContent2->SetSpacing(8.0f);
    childContent2->SetMargin(Thickness{12.0f});

    auto* cb1 = childContent2->Add(std::make_unique<CheckBox>());
    cb1->SetText(L"启用自动保存");
    cb1->SetChecked(true);

    auto* cb2 = childContent2->Add(std::make_unique<CheckBox>());
    cb2->SetText(L"显示行号");

    auto* cb3 = childContent2->Add(std::make_unique<CheckBox>());
    cb3->SetText(L"自动换行");
    cb3->SetChecked(true);

    groupBox2->SetChild(std::move(childContent2));

    // 示例 3: RadioButton 组
    auto* card3 = CreateExampleCard(content, L"GroupBox with RadioButtons");
    auto* groupBox3 = card3->Add(std::make_unique<GroupBox>());
    groupBox3->SetHeader(L"主题选择");
    groupBox3->SetWidth(400.0f);

    auto childContent3 = std::make_unique<StackPanel>();
    childContent3->SetSpacing(8.0f);
    childContent3->SetMargin(Thickness{12.0f});

    // RadioButton 组需要共享的 int* 和各自的值
    static int themeGroup = 1;  // 0=浅色, 1=深色, 2=跟随系统

    auto* rb1 = childContent3->Add(std::make_unique<RadioButton>());
    rb1->SetText(L"浅色主题");
    rb1->SetGroup(&themeGroup, 0);

    auto* rb2 = childContent3->Add(std::make_unique<RadioButton>());
    rb2->SetText(L"深色主题");
    rb2->SetGroup(&themeGroup, 1);

    auto* rb3 = childContent3->Add(std::make_unique<RadioButton>());
    rb3->SetText(L"跟随系统");
    rb3->SetGroup(&themeGroup, 2);

    groupBox3->SetChild(std::move(childContent3));

    // 示例 4: 嵌套 GroupBox
    auto* card4 = CreateExampleCard(content, L"Nested GroupBox");
    auto* outerBox = card4->Add(std::make_unique<GroupBox>());
    outerBox->SetHeader(L"账户设置");
    outerBox->SetWidth(450.0f);

    auto outerContent = std::make_unique<StackPanel>();
    outerContent->SetSpacing(12.0f);
    outerContent->SetMargin(Thickness{12.0f});

    auto* innerBox1 = outerContent->Add(std::make_unique<GroupBox>());
    innerBox1->SetHeader(L"通知设置");
    auto innerContent1 = std::make_unique<StackPanel>();
    innerContent1->SetSpacing(6.0f);
    innerContent1->SetMargin(Thickness{8.0f});
    auto* notif1 = innerContent1->Add(std::make_unique<CheckBox>());
    notif1->SetText(L"邮件通知");
    notif1->SetChecked(true);
    auto* notif2 = innerContent1->Add(std::make_unique<CheckBox>());
    notif2->SetText(L"短信通知");
    innerBox1->SetChild(std::move(innerContent1));

    auto* innerBox2 = outerContent->Add(std::make_unique<GroupBox>());
    innerBox2->SetHeader(L"隐私设置");
    auto innerContent2 = std::make_unique<StackPanel>();
    innerContent2->SetSpacing(6.0f);
    innerContent2->SetMargin(Thickness{8.0f});
    auto* priv1 = innerContent2->Add(std::make_unique<CheckBox>());
    priv1->SetText(L"公开个人资料");
    auto* priv2 = innerContent2->Add(std::make_unique<CheckBox>());
    priv2->SetText(L"显示在线状态");
    priv2->SetChecked(true);
    innerBox2->SetChild(std::move(innerContent2));

    outerBox->SetChild(std::move(outerContent));

    // 示例 5: 可见性控制
    auto* card5 = CreateExampleCard(content, L"Visibility Control");
    auto* desc5 = card5->Add(std::make_unique<TextBlock>());
    desc5->SetText(L"点击按钮可切换 GroupBox 的可见性。");
    desc5->SetWrap(true);
    desc5->SetMargin(Thickness{0, 0, 0, 12.0f});

    auto* groupBox5 = card5->Add(std::make_unique<GroupBox>());
    groupBox5->SetHeader(L"高级选项");
    groupBox5->SetWidth(400.0f);

    auto childContent5 = std::make_unique<StackPanel>();
    childContent5->SetSpacing(8.0f);
    childContent5->SetMargin(Thickness{12.0f});

    auto* opt1 = childContent5->Add(std::make_unique<CheckBox>());
    opt1->SetText(L"启用调试模式");

    auto* opt2 = childContent5->Add(std::make_unique<CheckBox>());
    opt2->SetText(L"显示性能指标");

    auto* opt3 = childContent5->Add(std::make_unique<CheckBox>());
    opt3->SetText(L"记录详细日志");

    groupBox5->SetChild(std::move(childContent5));

    auto* toggleBtn = card5->Add(std::make_unique<Button>());
    toggleBtn->SetText(L"切换可见性");
    toggleBtn->SetMargin(Thickness{0, 8.0f, 0, 0});
    state->subs.Keep(toggleBtn->Click().Subscribe(groupBox5, [](void* owner, Button&, RoutedEventArgs&) {
        auto* gb = static_cast<GroupBox*>(owner);
        gb->SetVisible(!gb->IsVisible());
    }));

    CreateCodeExample(content, LR"(auto groupBox = std::make_unique<GroupBox>();
groupBox->SetHeader(L"个人信息");

auto content = std::make_unique<StackPanel>();
content->SetSpacing(8.0f);
content->SetMargin(Thickness{12.0f});

// ... add children to content
groupBox->SetChild(std::move(content));

// Control visibility
groupBox->SetVisible(false);)");

    return std::move(page);
}

}  // namespace fluent
