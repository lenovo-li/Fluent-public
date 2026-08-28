// SelectionPages.cpp — 选择控件页面
#include "../GalleryMain.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/ListBox.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/Calendar.h"
#include "../../FluentUI/controls/DatePicker.h"
#include "../../FluentUI/controls/NumericUpDown.h"
#include "../../FluentUI/controls/Rating.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/layout/Grid.h"
#include "../../FluentUI/layout/GroupBox.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/layout/ScrollPanel.h"
// 「混合使用」卡片要把控件放进真实构图里，需要按钮和复选框这些搭档控件。
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include <cwchar>

namespace fluent {

std::unique_ptr<ScrollPanel> GalleryApp::CreateSliderPage() {
    auto [page, content] = CreatePageShell(L"Slider");

    // --- 1. 范围与状态 ---------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"范围与状态");
        AddNote(card, L"Event 的 handler 是无捕获函数指针（Event.h 刻意不支持捕获 lambda，"
                      L"那会要求堆分配闭包），所以显示用的 label 指针只能通过 owner 传进去。");

        auto* normal = card->Add(std::make_unique<Slider>());
        normal->SetWidth(360.0f);
        normal->SetMinimum(0.0); normal->SetMaximum(100.0); normal->SetValue(50.0);
        normal->SetHAlign(HAlign::Left);
        auto* normalLabel = card->Add(std::make_unique<TextBlock>());
        normalLabel->SetText(L"0..100，当前值: 50.0");
        pageSubs_ += normal->ValueChanged().Subscribe(normalLabel,
            [](void* owner, Slider&, float& value) {
                wchar_t buf[64];
                swprintf_s(buf, L"0..100，当前值: %.1f", value);
                static_cast<TextBlock*>(owner)->SetText(buf);
            });

        auto* fine = card->Add(std::make_unique<Slider>());
        fine->SetWidth(360.0f);
        fine->SetMinimum(-1.0); fine->SetMaximum(1.0); fine->SetValue(0.25);
        fine->SetHAlign(HAlign::Left);
        auto* fineLabel = card->Add(std::make_unique<TextBlock>());
        fineLabel->SetText(L"-1..1（负范围），当前值: 0.25");
        pageSubs_ += fine->ValueChanged().Subscribe(fineLabel,
            [](void* owner, Slider&, float& value) {
                wchar_t buf[64];
                swprintf_s(buf, L"-1..1（负范围），当前值: %.2f", value);
                static_cast<TextBlock*>(owner)->SetText(buf);
            });

        auto* disabled = card->Add(std::make_unique<Slider>());
        disabled->SetWidth(360.0f);
        disabled->SetValue(70.0);
        disabled->SetEnabled(false);
        disabled->SetHAlign(HAlign::Left);
        AddNote(card, L"↑ 禁用态");
    }

    // --- 2. 布局行为 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"在布局里的行为");
        AddNote(card, L"Slider 没有内容驱动的长度 —— 轨道要多长完全由布局决定，"
                      L"所以它默认 Stretch 占满可用宽度。这和 Metric 那种「有自然尺寸」"
                      L"的控件相反：给 Slider 写死宽度是正当用法。");

        auto* sec1 = AddSubSection(card, L"Stretch vs 固定宽度");
        {
            auto* box = AddBoundedBox(sec1, 480.0f, L"Stretch（默认）：跟着容器变");
            auto* sl = box->Add(std::make_unique<Slider>());
            sl->SetMaximum(100.0); sl->SetValue(60.0);
        }
        {
            auto* box = AddBoundedBox(sec1, 480.0f, L"固定 200 DIP + 左对齐");
            auto* sl = box->Add(std::make_unique<Slider>());
            sl->SetMaximum(100.0); sl->SetValue(60.0);
            sl->SetWidth(200.0f);
            sl->SetHAlign(HAlign::Left);
        }

        auto* sec2 = AddSubSection(card, L"MinWidth / MaxWidth 夹逼");
        AddNote(sec2, L"MaxWidth 让滑块在宽屏上不会拉成一条难以精确操作的长线；"
                      L"MinWidth 保证窄容器里仍然可用。Min 优先于 Max（和 WPF 一致）。");
        for (float w : {600.0f, 380.0f, 160.0f}) {
            auto* box = AddBoundedBox(sec2, w,
                std::wstring(L"容器 ") + std::to_wstring(static_cast<int>(w)) +
                L" DIP，滑块 MaxWidth=320 MinWidth=180");
            auto* sl = box->Add(std::make_unique<Slider>());
            sl->SetMaximum(100.0); sl->SetValue(45.0);
            sl->SetMaxWidth(320.0f);
            sl->SetMinWidth(180.0f);
            sl->SetHAlign(HAlign::Left);
        }

        auto* sec3 = AddSubSection(card, L"垂直方向");
        AddNote(sec3, L"Vertical 时长度由 Height 决定，需要显式给高度 —— "
                      L"纵向没有「自然长度」可言。");
        auto* vrow = AddRow(sec3, 28.0f);
        for (float v : {20.0f, 55.0f, 90.0f}) {
            auto* sl = vrow->Add(std::make_unique<Slider>());
            sl->SetOrientation(Slider::Orientation::Vertical);
            sl->SetHeight(120.0f);
            sl->SetMaximum(100.0);
            sl->SetValue(v);
        }

        auto* sec4 = AddSubSection(card, L"标签 + 滑块 + 数值：三列 Grid");
        AddNote(sec4, L"实用形状：标签 Auto 列、滑块 Star 列、数值 Auto 列（定宽右对齐，"
                      L"这样数字变化时滑块不会左右跳动）。");
        auto* g = sec4->Add(std::make_unique<Grid>());
        g->AddColumn(GridLength::Pixels(80.0f));
        g->AddColumn(GridLength::Star(1.0f));
        g->AddColumn(GridLength::Pixels(64.0f));
        const wchar_t* names[] = {L"音量", L"亮度", L"对比度"};
        const float vals[] = {70.0f, 45.0f, 55.0f};
        for (int i = 0; i < 3; ++i) {
            g->AddRow(GridLength::Auto());
            auto* lab = g->Add(std::make_unique<TextBlock>());
            g->SetCell(lab, i, 0);
            lab->SetText(names[i]);
            lab->SetVAlign(VAlign::Center);

            auto* sl = g->Add(std::make_unique<Slider>());
            g->SetCell(sl, i, 1);
            sl->SetMaximum(100.0);
            sl->SetValue(vals[i]);
            sl->SetVAlign(VAlign::Center);
            sl->SetMargin(Thickness{8.0f, 4.0f, 8.0f, 4.0f});

            auto* num = g->Add(std::make_unique<TextBlock>());
            g->SetCell(num, i, 2);
            wchar_t buf[16];
            swprintf_s(buf, L"%.0f", vals[i]);
            num->SetText(buf);
            num->SetVAlign(VAlign::Center);
            num->SetAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            pageSubs_ += sl->ValueChanged().Subscribe(num,
                [](void* owner, Slider&, float& value) {
                    wchar_t b[16];
                    swprintf_s(b, L"%.0f", value);
                    static_cast<TextBlock*>(owner)->SetText(b);
                });
        }
    }

    // --- 3. 样式定制 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");
        auto* sec1 = AddSubSection(card, L"AccentColor：已填充轨道的颜色");
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        for (int i = 0; i < n; ++i) {
            auto* sl = sec1->Add(std::make_unique<Slider>());
            sl->SetMaximum(100.0);
            sl->SetValue(20.0f + i * 15.0f);
            sl->SetWidth(320.0f);
            sl->SetHAlign(HAlign::Left);
            sl->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
        }

        auto* sec2 = AddSubSection(card, L"步进：Step 让取值离散化");
        AddNote(sec2, L"Step 之后拖动会吸附到步长上，键盘方向键也按步长走。"
                      L"注意：Step 没有「连续」档 —— SetStep 会把值夹到 ≥0.0001，"
                      L"CoerceValue 始终按网格取整，所以传 0 得到的是 0.0001 的细网格"
                      L"（视觉上等同连续），而不是关闭吸附。");
        for (float step : {0.0001f, 10.0f, 25.0f}) {
            auto* sl = sec2->Add(std::make_unique<Slider>());
            sl->SetMaximum(100.0);
            sl->SetValue(50.0);
            sl->SetStep(step);
            sl->SetWidth(320.0f);
            sl->SetHAlign(HAlign::Left);
            auto* cap = sec2->Add(std::make_unique<TextBlock>());
            wchar_t buf[48];
            swprintf_s(buf, L"Step = %g%s", step,
                       step < 0.001f ? L"（下限，视觉上连续）" : L"");
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
        }
    }

    // --- 4. 混合使用 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：滑块驱动其他控件");
        AddNote(card, L"滑块的值同时驱动一个 ProgressBar 和一段文字 —— "
                      L"演示一个值源可以有多个消费者，且都在同一帧内更新。");

        auto* sl = card->Add(std::make_unique<Slider>());
        sl->SetMaximum(100.0);
        sl->SetValue(35.0);
        sl->SetWidth(420.0f);
        sl->SetHAlign(HAlign::Left);

        auto* bar = card->Add(std::make_unique<ProgressBar>());
        bar->SetValue(0.35f);
        bar->SetWidth(420.0f);
        bar->SetHAlign(HAlign::Left);

        sliderMirrorBar_ = bar;
        pageSubs_ += sl->ValueChanged().Subscribe(this,
            [](void* o, Slider&, float& v) {
                auto* self = static_cast<GalleryApp*>(o);
                if (self->sliderMirrorBar_) self->sliderMirrorBar_->SetValue(v / 100.0f);
            });
    }

    CreateCodeExample(content, LR"(auto* slider = panel->Add(std::make_unique<Slider>());
slider->SetMinimum(0.0);
slider->SetMaximum(100.0);
slider->SetValue(50.0);
slider->SetStep(5.0f);                  // 下限 0.0001，没有「关闭吸附」这一档

// Slider 没有自然长度，写死尺寸是正当用法；也可以只夹逼上下限：
slider->SetMaxWidth(320.0f);            // 宽屏上不要拉成难操作的长线
slider->SetMinWidth(180.0f);

slider->SetAccentColor(D2D1::ColorF(0x107C10, 1.0f));   // 已填充轨道色

// handler 是无捕获函数指针，状态通过 owner 传入
sub_ = slider->ValueChanged().Subscribe(label, [](void* o, Slider&, float& v) {
    wchar_t buf[32]; swprintf_s(buf, L"%.0f", v);
    static_cast<TextBlock*>(o)->SetText(buf);
});)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateComboBoxPage() {
    auto [page, content] = CreatePageShell(L"ComboBox");

    // --- 1. 状态 ---------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"状态");
        AddNote(card, L"ComboBox 的「值」在两种模式下来源不同：只读模式取选中项，"
                      L"可编辑模式取键入的文字。Text() 两种模式都能读，调用方不必分支。");

        auto* sec1 = AddSubSection(card, L"只读模式（默认）");
        auto* combo = sec1->Add(std::make_unique<ComboBox>());
        combo->SetWidth(300.0f);
        combo->SetHAlign(HAlign::Left);
        combo->SetItems({L"平衡", L"高性能", L"节能"});
        combo->SetSelectedIndex(0);
        combo->SetTooltip(L"选择电源模式");
        auto* comboLabel = sec1->Add(std::make_unique<TextBlock>());
        comboLabel->SetText(L"当前选择：平衡");
        // owner 用 ComboBox 自己而不是新建一个配对结构体：SelectionChanged 的
        // sender 就是这个 ComboBox，索引又能直接查 Items()，配对结构体是多余的
        // 堆分配（旧写法 new 出来的那块从来不回收）。
        pageSubs_ += combo->SelectionChanged().Subscribe(comboLabel,
            [](void* owner, ComboBox& sender, int& index) {
                if (index < 0 || index >= static_cast<int>(sender.Items().size())) return;
                static_cast<TextBlock*>(owner)->SetText(L"当前选择：" + sender.Items()[index]);
            });

        auto* sec2 = AddSubSection(card, L"可编辑模式：列表是建议，不是限制");
        AddNote(sec2, L"可编辑模式刻意不做过滤 —— 前缀还是子串、区不区分大小写、"
                      L"要不要自动提交，都是应用的策略，所以 TextChanged 只把文字交出去。");
        auto* editable = sec2->Add(std::make_unique<ComboBox>());
        editable->SetWidth(300.0f);
        editable->SetHAlign(HAlign::Left);
        editable->SetItems({L"深圳", L"上海", L"北京"});
        editable->SetEditable(true);
        editable->SetText(L"深圳");
        editable->SetTooltip(L"可编辑：也能填列表里没有的城市");
        auto* editLabel = sec2->Add(std::make_unique<TextBlock>());
        editLabel->SetText(L"键入内容：深圳");
        editLabel->SetFontSize(12.0f);
        editLabel->SetDimmed(true);
        pageSubs_ += editable->TextChanged().Subscribe(editLabel,
            [](void* owner, ComboBox&, std::wstring& text) {
                static_cast<TextBlock*>(owner)->SetText(L"键入内容：" + text);
            });

        auto* sec3 = AddSubSection(card, L"空选择与禁用");
        AddNote(sec3, L"SelectedIndex = -1 表示「什么都没选」，头部因此是空的 —— "
                      L"这和「选中了一个空字符串」不是一回事。");
        auto* empty = sec3->Add(std::make_unique<ComboBox>());
        empty->SetWidth(300.0f);
        empty->SetHAlign(HAlign::Left);
        empty->SetItems({L"选项 A", L"选项 B", L"选项 C"});
        empty->SetSelectedIndex(-1);
        empty->SetTooltip(L"未选择任何项");

        auto* disabled = sec3->Add(std::make_unique<ComboBox>());
        disabled->SetWidth(300.0f);
        disabled->SetHAlign(HAlign::Left);
        disabled->SetItems({L"已锁定的值"});
        disabled->SetSelectedIndex(0);
        disabled->SetEnabled(false);
        AddNote(sec3, L"↑ 禁用态：不接受点击也不进 Tab 序，但仍显示当前值。");

        auto* sec4 = AddSubSection(card, L"长列表：弹出面板自己滚动");
        AddNote(sec4, L"下拉面板是独立的顶层窗口，高度有上限并自带滚动，"
                      L"所以项数多少都不会改变头部的尺寸。");
        auto* many = sec4->Add(std::make_unique<ComboBox>());
        many->SetWidth(300.0f);
        many->SetHAlign(HAlign::Left);
        std::vector<std::wstring> months;
        for (int m = 1; m <= 12; ++m) months.push_back(std::to_wstring(m) + L" 月");
        many->SetItems(std::move(months));
        many->SetSelectedIndex(7);
    }

    // --- 2. 布局行为 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"在布局里的行为");
        AddNote(card, L"ComboBox 的宽度不由最长选项决定：Measure 里未写死宽度时取"
                      L"min(可用宽度, 200)，所以它在窄容器里跟着容器缩，在宽容器里"
                      L"停在 200 —— 想要占满就得显式 Stretch 或给宽度。");

        auto* sec1 = AddSubSection(card, L"Stretch vs 固定宽度");
        {
            auto* box = AddBoundedBox(sec1, 480.0f, L"HAlign::Stretch：铺满容器");
            auto* c = box->Add(std::make_unique<ComboBox>());
            c->SetItems({L"跟随容器宽度"});
            c->SetSelectedIndex(0);
            c->SetHAlign(HAlign::Stretch);
        }
        {
            auto* box = AddBoundedBox(sec1, 480.0f, L"固定 180 DIP + 左对齐");
            auto* c = box->Add(std::make_unique<ComboBox>());
            c->SetItems({L"固定 180"});
            c->SetSelectedIndex(0);
            c->SetWidth(180.0f);
            c->SetHAlign(HAlign::Left);
        }
        {
            auto* box = AddBoundedBox(sec1, 480.0f, L"不写宽度也不 Stretch：停在默认 200");
            auto* c = box->Add(std::make_unique<ComboBox>());
            c->SetItems({L"默认 200"});
            c->SetSelectedIndex(0);
            c->SetHAlign(HAlign::Left);
        }

        auto* sec2 = AddSubSection(card, L"MinWidth / MaxWidth 夹逼");
        AddNote(sec2, L"表单里常见的需求：宽屏上别拉成横贯整行的长条，窄屏上又要留住"
                      L"足够显示文字的宽度。Min 与 Max 冲突时 Min 胜（和 WPF 一致），"
                      L"所以 160 DIP 的容器里它会溢出而不是缩到读不清。");
        for (float w : {600.0f, 380.0f, 160.0f}) {
            auto* box = AddBoundedBox(sec2, w,
                std::wstring(L"容器 ") + std::to_wstring(static_cast<int>(w)) +
                L" DIP，下拉框 MinWidth=200 MaxWidth=320");
            auto* c = box->Add(std::make_unique<ComboBox>());
            c->SetItems({L"Min 200 / Max 320"});
            c->SetSelectedIndex(0);
            c->SetHAlign(HAlign::Stretch);
            c->SetMinWidth(200.0f);
            c->SetMaxWidth(320.0f);
        }

        auto* sec3 = AddSubSection(card, L"四种水平对齐");
        AddNote(sec3, L"Left / Center / Right 用 desired 尺寸摆位，Stretch 则改变尺寸本身 —— "
                      L"前三个宽度相同，只有位置不同，最后一个才变宽。");
        struct AlignCase { HAlign align; const wchar_t* caption; const wchar_t* item; };
        static constexpr AlignCase kAligns[] = {
            {HAlign::Left,    L"HAlign::Left",    L"靠左"},
            {HAlign::Center,  L"HAlign::Center",  L"居中"},
            {HAlign::Right,   L"HAlign::Right",   L"靠右"},
            {HAlign::Stretch, L"HAlign::Stretch", L"铺满"},
        };
        for (const AlignCase& ac : kAligns) {
            auto* box = AddBoundedBox(sec3, 460.0f, ac.caption);
            auto* c = box->Add(std::make_unique<ComboBox>());
            c->SetItems({ac.item});
            c->SetSelectedIndex(0);
            c->SetWidth(ac.align == HAlign::Stretch ? kAuto : 160.0f);
            c->SetHAlign(ac.align);
        }

        auto* sec4 = AddSubSection(card, L"标签 + 下拉框：两列表单 Grid");
        AddNote(sec4, L"标签列 Auto（跟着最长标签走，不必手量像素），控件列 Star（吃掉剩余宽度）。"
                      L"行高一律 Auto —— 写死行高会把文字的下伸部分切掉。");
        auto* form = sec4->Add(std::make_unique<Grid>());
        form->AddColumn(GridLength::Auto());
        form->AddColumn(GridLength::Star(1.0f));
        form->SetColumnSpacing(12.0f);
        form->SetRowSpacing(8.0f);
        static const wchar_t* const kFieldNames[] = {L"编码格式", L"分辨率", L"音频轨"};
        static const wchar_t* const kFieldItems[3][3] = {
            {L"H.264", L"H.265", L"AV1"},
            {L"1080p", L"1440p", L"2160p"},
            {L"AAC 立体声", L"AAC 5.1", L"无音频"},
        };
        for (int i = 0; i < 3; ++i) {
            form->AddRow(GridLength::Auto());
            auto* lab = form->Add(std::make_unique<TextBlock>());
            form->SetCell(lab, i, 0);
            lab->SetText(kFieldNames[i]);
            // 标签垂直居中对齐到 32 DIP 高的控件上，否则会贴在行顶。
            lab->SetVAlign(VAlign::Center);
            lab->SetHAlign(HAlign::Left);

            auto* c = form->Add(std::make_unique<ComboBox>());
            form->SetCell(c, i, 1);
            c->SetItems({kFieldItems[i][0], kFieldItems[i][1], kFieldItems[i][2]});
            c->SetSelectedIndex(0);
            c->SetHAlign(HAlign::Stretch);
            c->SetMaxWidth(280.0f);
            c->SetVAlign(VAlign::Center);
        }
    }

    // --- 3. 样式定制 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");

        auto* sec1 = AddSubSection(card, L"AccentColor：弹出列表里的选中/悬停底色");
        AddNote(sec1, L"头部本身不用强调色，改色要打开下拉才看得到 —— 高亮条和选中行的"
                      L"竖条走的是这个值。");
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        for (int i = 0; i < n; ++i) {
            auto* row = AddRow(sec1, 12.0f);
            auto* c = row->Add(std::make_unique<ComboBox>());
            c->SetItems({L"第一项", L"第二项", L"第三项"});
            c->SetSelectedIndex(i % 3);
            c->SetWidth(220.0f);
            c->SetHAlign(HAlign::Left);
            c->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(sw[i].label);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec2 = AddSubSection(card, L"CornerRadius：从直角到药丸形");
        AddNote(sec2, L"圆角只影响头部的填充与描边，是 Render 级属性，不改变尺寸；"
                      L"半径大于高度一半时会被几何自然截断成药丸形。");
        for (float r : {0.0f, 4.0f, 10.0f, 16.0f}) {
            auto* row = AddRow(sec2, 12.0f);
            auto* c = row->Add(std::make_unique<ComboBox>());
            c->SetItems({L"圆角演示"});
            c->SetSelectedIndex(0);
            c->SetWidth(200.0f);
            c->SetHAlign(HAlign::Left);
            c->SetCornerRadius(r);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[32];
            swprintf_s(buf, L"CornerRadius = %.0f", r);
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec3 = AddSubSection(card, L"BorderThickness / BorderBrush：边框强调");
        AddNote(sec3, L"加粗边框是「这个字段有问题」的常用提示，比只换文字颜色更容易被注意到。");
        {
            auto* row = AddRow(sec3, 12.0f);
            auto* thin = row->Add(std::make_unique<ComboBox>());
            thin->SetItems({L"默认 1 DIP"});
            thin->SetSelectedIndex(0);
            thin->SetWidth(200.0f);
            thin->SetHAlign(HAlign::Left);

            auto* thick = row->Add(std::make_unique<ComboBox>());
            thick->SetItems({L"2 DIP + 红色描边"});
            thick->SetSelectedIndex(0);
            thick->SetWidth(220.0f);
            thick->SetHAlign(HAlign::Left);
            thick->SetBorderThickness(2.0f);
            thick->SetBorderBrush(D2D1::ColorF(0xC42B1C, 1.0f));
        }

        auto* sec4 = AddSubSection(card, L"Background / Foreground：填充与文字色");
        AddNote(sec4, L"两者都是 Control 级覆盖：设了就一直用它，没设的控件继续跟着主题走 —— "
                      L"包括明暗主题切换。");
        {
            auto* row = AddRow(sec4, 12.0f);
            auto* tinted = row->Add(std::make_unique<ComboBox>());
            tinted->SetItems({L"浅蓝底"});
            tinted->SetSelectedIndex(0);
            tinted->SetWidth(200.0f);
            tinted->SetHAlign(HAlign::Left);
            tinted->SetBackground(D2D1::ColorF(0xEFF6FC, 1.0f));
            tinted->SetForeground(D2D1::ColorF(0x0F548C, 1.0f));

            auto* plain = row->Add(std::make_unique<ComboBox>());
            plain->SetItems({L"跟随主题"});
            plain->SetSelectedIndex(0);
            plain->SetWidth(200.0f);
            plain->SetHAlign(HAlign::Left);
        }

        auto* sec5 = AddSubSection(card, L"PopupOpacity：下拉面板的不透明度");
        AddNote(sec5, L"半透明面板能让下面的内容透出来，但会牺牲文字对比度，"
                      L"所以默认是完全不透明；这里放两个对照，打开下拉即可比较。");
        {
            auto* row = AddRow(sec5, 12.0f);
            auto* opaque = row->Add(std::make_unique<ComboBox>());
            opaque->SetItems({L"不透明 1.0", L"第二项", L"第三项"});
            opaque->SetSelectedIndex(0);
            opaque->SetWidth(200.0f);
            opaque->SetHAlign(HAlign::Left);

            auto* semi = row->Add(std::make_unique<ComboBox>());
            semi->SetItems({L"半透明 0.85", L"第二项", L"第三项"});
            semi->SetSelectedIndex(0);
            semi->SetWidth(200.0f);
            semi->SetHAlign(HAlign::Left);
            // SetPopupOpacity 只在 popup_ 已创建后有效（OnAttachedToTree 里建），
            // 而页面是先建树再附加的 —— 这里的调用因此可能落空，属于已知限制。
            semi->SetPopupOpacity(0.85f);
        }
    }

    // --- 4. 混合使用 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：导出设置面板");
        AddNote(card, L"两个下拉框 + 一个复选框共同决定一句摘要。三个控件的 handler 都指向"
                      L"同一个 owner（GalleryApp），因为摘要要读全部三个值 —— 无捕获函数指针"
                      L"没法各自携带其他控件的指针。");

        auto* form = card->Add(std::make_unique<Grid>());
        form->AddColumn(GridLength::Auto());
        form->AddColumn(GridLength::Star(1.0f));
        form->SetColumnSpacing(12.0f);
        form->SetRowSpacing(8.0f);

        form->AddRow(GridLength::Auto());
        auto* lab1 = form->Add(std::make_unique<TextBlock>());
        form->SetCell(lab1, 0, 0);
        lab1->SetText(L"导出格式");
        lab1->SetVAlign(VAlign::Center);
        lab1->SetHAlign(HAlign::Left);
        auto* fmt = form->Add(std::make_unique<ComboBox>());
        form->SetCell(fmt, 0, 1);
        fmt->SetItems({L"PNG", L"JPEG", L"WebP"});
        fmt->SetSelectedIndex(0);
        fmt->SetHAlign(HAlign::Stretch);
        fmt->SetMaxWidth(240.0f);
        fmt->SetVAlign(VAlign::Center);

        form->AddRow(GridLength::Auto());
        auto* lab2 = form->Add(std::make_unique<TextBlock>());
        form->SetCell(lab2, 1, 0);
        lab2->SetText(L"画质");
        lab2->SetVAlign(VAlign::Center);
        lab2->SetHAlign(HAlign::Left);
        auto* quality = form->Add(std::make_unique<ComboBox>());
        form->SetCell(quality, 1, 1);
        quality->SetItems({L"标准", L"高", L"最高"});
        quality->SetSelectedIndex(1);
        quality->SetHAlign(HAlign::Stretch);
        quality->SetMaxWidth(240.0f);
        quality->SetVAlign(VAlign::Center);

        auto* meta = card->Add(std::make_unique<CheckBox>());
        meta->SetText(L"保留拍摄元数据");
        meta->SetChecked(true);
        meta->SetHAlign(HAlign::Left);

        auto* summary = card->Add(std::make_unique<TextBlock>());
        summary->SetText(L"将导出 PNG（高画质），保留元数据。");
        summary->SetWrap(true);
        summary->SetMargin(Thickness{0, 8.0f, 0, 0});

        auto* go = card->Add(std::make_unique<Button>());
        go->SetText(L"开始导出");
        go->SetKind(Button::Kind::Accent);
        go->SetHAlign(HAlign::Left);

        comboMixFormat_ = fmt;
        comboMixQuality_ = quality;
        comboMixMeta_ = meta;
        comboMixSummary_ = summary;

        // 三个事件的载荷类型各不相同（int& / int& / bool&），所以没法共用一个
        // handler；但它们最终都调 GalleryApp 的同一份重算逻辑。这里用三个薄
        // 包装而不是一个「通用」handler，是因为 Event 的签名是编译期固定的。
        pageSubs_ += fmt->SelectionChanged().Subscribe(this,
            [](void* owner, ComboBox&, int&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->comboMixFormat_ || !self->comboMixQuality_ ||
                    !self->comboMixMeta_ || !self->comboMixSummary_) return;
                self->comboMixSummary_->SetText(
                    L"将导出 " + self->comboMixFormat_->Text() + L"（" +
                    self->comboMixQuality_->Text() + L"画质），" +
                    (self->comboMixMeta_->IsChecked() ? L"保留元数据。" : L"丢弃元数据。"));
            });
        pageSubs_ += quality->SelectionChanged().Subscribe(this,
            [](void* owner, ComboBox&, int&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->comboMixFormat_ || !self->comboMixQuality_ ||
                    !self->comboMixMeta_ || !self->comboMixSummary_) return;
                self->comboMixSummary_->SetText(
                    L"将导出 " + self->comboMixFormat_->Text() + L"（" +
                    self->comboMixQuality_->Text() + L"画质），" +
                    (self->comboMixMeta_->IsChecked() ? L"保留元数据。" : L"丢弃元数据。"));
            });
        pageSubs_ += meta->Checked().Subscribe(this,
            [](void* owner, CheckBox&, bool&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->comboMixFormat_ || !self->comboMixQuality_ ||
                    !self->comboMixMeta_ || !self->comboMixSummary_) return;
                self->comboMixSummary_->SetText(
                    L"将导出 " + self->comboMixFormat_->Text() + L"（" +
                    self->comboMixQuality_->Text() + L"画质），" +
                    (self->comboMixMeta_->IsChecked() ? L"保留元数据。" : L"丢弃元数据。"));
            });
    }

    CreateCodeExample(content, LR"(auto* combo = panel->Add(std::make_unique<ComboBox>());
combo->SetItems({L"平衡", L"高性能", L"节能"});
combo->SetSelectedIndex(0);          // -1 = 什么都没选（头部为空）
combo->SetEditable(true);            // 列表变成建议，可填列表外的值

// 布局：默认宽度是 min(可用宽度, 200)，要占满得显式声明
combo->SetHAlign(HAlign::Stretch);
combo->SetMinWidth(200.0f);          // Min 与 Max 冲突时 Min 胜
combo->SetMaxWidth(320.0f);

// 样式（都是 Control 级覆盖，没设的继续跟主题走）
combo->SetAccentColor(D2D1::ColorF(0x107C10, 1.0f));   // 下拉列表高亮
combo->SetCornerRadius(10.0f);
combo->SetBorderThickness(2.0f);

// handler 是无捕获函数指针，索引查 sender 自己的 Items()
sub_ = combo->SelectionChanged().Subscribe(label,
    [](void* o, ComboBox& s, int& i) {
        static_cast<TextBlock*>(o)->SetText(s.Items()[i]);
    });)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateListBoxPage() {
    auto [page, content] = CreatePageShell(L"ListBox");
    auto* card = CreateExampleCard(content, L"Single and multiple selection");

    auto* single = card->Add(std::make_unique<ListBox>());
    single->SetWidth(360.0f); single->SetHeight(150.0f);
    single->SetItems({L"Alpha", L"Beta", L"Gamma", L"Delta", L"Epsilon"});
    single->SetTooltip(L"单选列表，点击选中一项");

    auto* multi = card->Add(std::make_unique<ListBox>());
    multi->SetWidth(360.0f); multi->SetHeight(170.0f);
    multi->SetSelectionMode(SelectionMode::Multiple);
    multi->SetItems({L"Ctrl+Click toggles", L"Shift+Click selects a range", L"Space toggles focused item", L"Keyboard navigation", L"Virtualized rows"});
    multi->SetTooltip(L"多选列表：Ctrl+Click 切换，Shift+Click 范围选择");

    CreateCodeExample(content, LR"(list->SetSelectionMode(SelectionMode::Multiple);
list->SetItems(items);
// Ctrl/Shift/Space are handled by Selector<ListBox>.)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateCalendarPage() {
    auto [page, content] = CreatePageShell(L"Calendar");
    auto* card = CreateExampleCard(content, L"Date selection and constraints");
    auto* calendar = card->Add(std::make_unique<Calendar>());
    calendar->SetWidth(360.0f);
    calendar->SetMinDate(2026, 1, 1);
    calendar->SetMaxDate(2026, 12, 31);
    calendar->SetBlackoutDates({{2026, 10, 1}, {2026, 10, 2}, {2026, 10, 3}});
    calendar->SetSelectedDate(2026, 8, 13);
    auto* hint = card->Add(std::make_unique<TextBlock>());
    hint->SetText(L"Keyboard: arrows move, PageUp/PageDown changes month, Enter selects.");
    hint->SetWrap(true);
    CreateCodeExample(content, LR"(auto calendar = std::make_unique<Calendar>();
calendar->SetMinDate(2026, 1, 1);
calendar->SetMaxDate(2026, 12, 31);
calendar->SetBlackoutDates({{2026, 10, 1}});
calendar->SetSelectedDate(2026, 8, 13);)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateDatePickerPage() {
    auto [page, content] = CreatePageShell(L"DatePicker");
    auto* card = CreateExampleCard(content, L"Popup calendar");
    auto* picker = card->Add(std::make_unique<DatePicker>());
    picker->SetWidth(260.0f);
    picker->SetSelectedDate(2026, 8, 13);
    auto* hint = card->Add(std::make_unique<TextBlock>());
    hint->SetText(L"Click to open the calendar. Arrow keys adjust the selected date.");
    hint->SetWrap(true);
    CreateCodeExample(content, LR"(auto picker = std::make_unique<DatePicker>();
picker->SetSelectedDate(2026, 8, 13);
picker->DateChanged().Subscribe(owner, OnDateChanged);)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateNumericUpDownPage() {
    auto [page, content] = CreatePageShell(L"NumericUpDown");

    // --- 1. 状态 ---------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"状态");
        AddNote(card, L"注意命名：NumericUpDown 用的是 SetMin / SetMax（RangeBase 的 "
                      L"SetMinimum / SetMaximum 的别名，和 Slider 一致），不是 Slider "
                      L"那套写法的独有名字 —— 两者其实是同一个基类。");

        auto* sec1 = AddSubSection(card, L"整数：0..100，步长 1");
        auto* basic = sec1->Add(std::make_unique<NumericUpDown>());
        basic->SetWidth(200.0f);
        basic->SetHAlign(HAlign::Left);
        basic->SetMin(0.0f);
        basic->SetMax(100.0f);
        basic->SetStep(1.0f);
        basic->SetValue(50.0f);
        basic->SetTooltip(L"↑↓ 键或点箭头调整，也可直接键入后回车提交");
        auto* basicLabel = sec1->Add(std::make_unique<TextBlock>());
        basicLabel->SetText(L"当前值：50");
        basicLabel->SetFontSize(12.0f);
        basicLabel->SetDimmed(true);
        pageSubs_ += basic->ValueChanged().Subscribe(basicLabel,
            [](void* owner, NumericUpDown&, float& value) {
                wchar_t buf[48];
                swprintf_s(buf, L"当前值：%.0f", value);
                static_cast<TextBlock*>(owner)->SetText(buf);
            });

        auto* sec2 = AddSubSection(card, L"小数：DecimalPlaces 决定显示位数");
        AddNote(sec2, L"DecimalPlaces 只管显示格式，Step 才决定实际能取到哪些值 —— "
                      L"两者不匹配（比如 2 位小数配步长 0.1）会让用户看到永远出现不了的第二位。");
        for (int places : {0, 1, 2}) {
            auto* row = AddRow(sec2, 12.0f);
            auto* n = row->Add(std::make_unique<NumericUpDown>());
            n->SetWidth(180.0f);
            n->SetHAlign(HAlign::Left);
            n->SetMin(0.0f);
            n->SetMax(10.0f);
            // 步长跟着位数走：位数 0 → 1，位数 1 → 0.1，位数 2 → 0.01。
            n->SetStep(places == 0 ? 1.0f : (places == 1 ? 0.1f : 0.01f));
            n->SetDecimalPlaces(places);
            n->SetValue(3.14159f);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[64];
            swprintf_s(buf, L"DecimalPlaces = %d", places);
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec3 = AddSubSection(card, L"步长与吸附：Value 会被强制落到网格上");
        AddNote(sec3, L"CoerceValue 的网格是从 Min 起算的，不是从 0 起算：范围 [0.5, 10] "
                      L"步长 1 给出的合法值是 0.5 / 1.5 / 2.5，因为下限本身一定是合法值。");
        {
            auto* row = AddRow(sec3, 12.0f);
            auto* offGrid = row->Add(std::make_unique<NumericUpDown>());
            offGrid->SetWidth(180.0f);
            offGrid->SetHAlign(HAlign::Left);
            offGrid->SetMin(0.5f);
            offGrid->SetMax(10.0f);
            offGrid->SetStep(1.0f);
            offGrid->SetDecimalPlaces(1);
            // 故意写一个不在网格上的值，用来展示吸附：会被拉到 4.5。
            offGrid->SetValue(4.2f);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(L"Min=0.5 Step=1，写入 4.2 → 吸附成 4.5");
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec4 = AddSubSection(card, L"边界与禁用");
        AddNote(sec4, L"到顶/到底时对应方向的箭头会变灰，不用点一下才发现走不动了。");
        {
            auto* row = AddRow(sec4, 12.0f);
            auto* atMax = row->Add(std::make_unique<NumericUpDown>());
            atMax->SetWidth(160.0f);
            atMax->SetHAlign(HAlign::Left);
            atMax->SetMin(0.0f);
            atMax->SetMax(10.0f);
            atMax->SetValue(10.0f);
            auto* capMax = row->Add(std::make_unique<TextBlock>());
            capMax->SetText(L"已到上限：↑ 变灰");
            capMax->SetFontSize(12.0f);
            capMax->SetDimmed(true);
            capMax->SetVAlign(VAlign::Center);
        }
        {
            auto* row = AddRow(sec4, 12.0f);
            auto* atMin = row->Add(std::make_unique<NumericUpDown>());
            atMin->SetWidth(160.0f);
            atMin->SetHAlign(HAlign::Left);
            atMin->SetMin(0.0f);
            atMin->SetMax(10.0f);
            atMin->SetValue(0.0f);
            auto* capMin = row->Add(std::make_unique<TextBlock>());
            capMin->SetText(L"已到下限：↓ 变灰");
            capMin->SetFontSize(12.0f);
            capMin->SetDimmed(true);
            capMin->SetVAlign(VAlign::Center);
        }
        {
            auto* row = AddRow(sec4, 12.0f);
            auto* disabled = row->Add(std::make_unique<NumericUpDown>());
            disabled->SetWidth(160.0f);
            disabled->SetHAlign(HAlign::Left);
            disabled->SetMax(100.0f);
            disabled->SetValue(42.0f);
            disabled->SetEnabled(false);
            auto* capDis = row->Add(std::make_unique<TextBlock>());
            capDis->SetText(L"禁用态：文字与箭头一起变灰");
            capDis->SetFontSize(12.0f);
            capDis->SetDimmed(true);
            capDis->SetVAlign(VAlign::Center);
        }

        auto* sec5 = AddSubSection(card, L"负范围");
        AddNote(sec5, L"温度补偿一类的设置需要负值；范围横跨 0 时步长网格照样从 Min 起算。");
        auto* negative = sec5->Add(std::make_unique<NumericUpDown>());
        negative->SetWidth(180.0f);
        negative->SetHAlign(HAlign::Left);
        negative->SetMin(-20.0f);
        negative->SetMax(20.0f);
        negative->SetStep(0.5f);
        negative->SetDecimalPlaces(1);
        negative->SetValue(-3.5f);
    }

    // --- 2. 布局行为 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"在布局里的行为");
        AddNote(card, L"右侧两个箭头占固定 20 DIP，文字区拿走剩下的宽度 —— 所以控件越窄，"
                      L"能显示的位数越少，而箭头永远不会被挤掉。默认宽度是 min(可用宽度, 120)，"
                      L"高度取主题的 controlHeightNormal（与 TextBox 对齐，同一行能排整齐）。");

        auto* sec1 = AddSubSection(card, L"Stretch vs 固定宽度");
        {
            auto* box = AddBoundedBox(sec1, 460.0f, L"HAlign::Stretch：文字区跟着容器变宽");
            auto* n = box->Add(std::make_unique<NumericUpDown>());
            n->SetMax(999999.0f);
            n->SetValue(123456.0f);
            n->SetHAlign(HAlign::Stretch);
        }
        {
            auto* box = AddBoundedBox(sec1, 460.0f, L"固定 120 DIP + 左对齐");
            auto* n = box->Add(std::make_unique<NumericUpDown>());
            n->SetMax(999999.0f);
            n->SetValue(123456.0f);
            n->SetWidth(120.0f);
            n->SetHAlign(HAlign::Left);
        }
        {
            auto* box = AddBoundedBox(sec1, 460.0f, L"固定 70 DIP：文字区只剩 50，长数字会被裁剪");
            auto* n = box->Add(std::make_unique<NumericUpDown>());
            n->SetMax(999999.0f);
            n->SetValue(123456.0f);
            n->SetWidth(70.0f);
            n->SetHAlign(HAlign::Left);
        }

        auto* sec2 = AddSubSection(card, L"MinWidth / MaxWidth 夹逼");
        AddNote(sec2, L"MinWidth 是这个控件真正需要的东西：低于「箭头 20 + 几位数字」就没法读了。"
                      L"MaxWidth 则避免一个只需要三位数的字段在宽屏上拉成半屏长。");
        for (float w : {520.0f, 300.0f, 110.0f}) {
            auto* box = AddBoundedBox(sec2, w,
                std::wstring(L"容器 ") + std::to_wstring(static_cast<int>(w)) +
                L" DIP，控件 MinWidth=96 MaxWidth=200");
            auto* n = box->Add(std::make_unique<NumericUpDown>());
            n->SetMax(9999.0f);
            n->SetValue(1234.0f);
            n->SetHAlign(HAlign::Stretch);
            n->SetMinWidth(96.0f);
            n->SetMaxWidth(200.0f);
        }

        auto* sec3 = AddSubSection(card, L"四种水平对齐");
        AddNote(sec3, L"注意 Stretch 那一格：它没有写宽度，所以宽度由容器给出；"
                      L"另外三格宽度都是 140，区别只在位置。");
        struct NumAlignCase { HAlign align; const wchar_t* caption; };
        static constexpr NumAlignCase kNumAligns[] = {
            {HAlign::Left,    L"HAlign::Left"},
            {HAlign::Center,  L"HAlign::Center"},
            {HAlign::Right,   L"HAlign::Right"},
            {HAlign::Stretch, L"HAlign::Stretch"},
        };
        for (const NumAlignCase& ac : kNumAligns) {
            auto* box = AddBoundedBox(sec3, 440.0f, ac.caption);
            auto* n = box->Add(std::make_unique<NumericUpDown>());
            n->SetMax(100.0f);
            n->SetValue(25.0f);
            if (ac.align != HAlign::Stretch) n->SetWidth(140.0f);
            n->SetHAlign(ac.align);
        }

        auto* sec4 = AddSubSection(card, L"标签 + 数字框 + 单位：三列 Grid 表单");
        AddNote(sec4, L"数字框用 Pixels 定宽而不是 Star：一排数字框宽度一致才好对比，"
                      L"而单位列用 Auto 贴在框右边。行高全部 Auto。");
        auto* form = sec4->Add(std::make_unique<Grid>());
        form->AddColumn(GridLength::Auto());
        form->AddColumn(GridLength::Pixels(140.0f));
        form->AddColumn(GridLength::Auto());
        form->SetColumnSpacing(12.0f);
        form->SetRowSpacing(8.0f);
        struct MarginField {
            const wchar_t* name; const wchar_t* unit;
            float min; float max; float step; int places; float value;
        };
        static constexpr MarginField kFields[] = {
            {L"页边距",   L"mm", 0.0f,  50.0f, 0.5f, 1, 12.5f},
            {L"缩放",     L"%",  10.0f, 400.0f, 5.0f, 0, 100.0f},
            {L"每页份数", L"份", 1.0f,  99.0f, 1.0f, 0, 1.0f},
        };
        for (int i = 0; i < 3; ++i) {
            const MarginField& f = kFields[i];
            form->AddRow(GridLength::Auto());

            auto* lab = form->Add(std::make_unique<TextBlock>());
            form->SetCell(lab, i, 0);
            lab->SetText(f.name);
            lab->SetHAlign(HAlign::Left);
            lab->SetVAlign(VAlign::Center);

            auto* n = form->Add(std::make_unique<NumericUpDown>());
            form->SetCell(n, i, 1);
            n->SetMin(f.min);
            n->SetMax(f.max);
            n->SetStep(f.step);
            n->SetDecimalPlaces(f.places);
            n->SetValue(f.value);
            n->SetHAlign(HAlign::Stretch);
            n->SetVAlign(VAlign::Center);

            auto* unit = form->Add(std::make_unique<TextBlock>());
            form->SetCell(unit, i, 2);
            unit->SetText(f.unit);
            unit->SetHAlign(HAlign::Left);
            unit->SetVAlign(VAlign::Center);
            unit->SetDimmed(true);
        }
    }

    // --- 3. 样式定制 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");

        auto* sec1 = AddSubSection(card, L"AccentColor：获得焦点时的边框色");
        AddNote(sec1, L"这个控件的强调色只在聚焦时出现（边框换成强调色），"
                      L"所以要用 Tab 或点击把焦点移进去才看得到差别。");
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        for (int i = 0; i < n; ++i) {
            auto* row = AddRow(sec1, 12.0f);
            auto* num = row->Add(std::make_unique<NumericUpDown>());
            num->SetWidth(140.0f);
            num->SetHAlign(HAlign::Left);
            num->SetMax(100.0f);
            num->SetValue(10.0f + i * 15.0f);
            num->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(std::wstring(sw[i].label) + L"（聚焦后可见）");
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec2 = AddSubSection(card, L"FontSize：字号会改变控件高度");
        AddNote(sec2, L"FontSize 是 Measure 级属性，但这个控件的高度取自主题 token 而不是字号，"
                      L"所以放大字号只会让数字在固定高度里变挤 —— 想要更高的输入框要同时给 Height。");
        for (float fs : {12.0f, 16.0f, 20.0f}) {
            auto* row = AddRow(sec2, 12.0f);
            auto* num = row->Add(std::make_unique<NumericUpDown>());
            num->SetWidth(160.0f);
            num->SetHAlign(HAlign::Left);
            num->SetMax(100.0f);
            num->SetValue(88.0f);
            num->SetFontSize(fs);
            // 字号大于默认时补高度，否则数字会贴着边框上下缘。
            if (fs > 14.0f) num->SetHeight(fs + 18.0f);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[64];
            swprintf_s(buf, L"FontSize = %.0f%s", fs, fs > 14.0f ? L"（配套加高）" : L"");
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec3 = AddSubSection(card, L"BorderThickness：0 表示完全不描边");
        AddNote(sec3, L"Render 里 stroke <= 0 就整段跳过描边，所以 0 是「无边框输入框」"
                      L"（嵌在表格单元格里常用），不是「极细边框」。");
        for (float t : {0.0f, 1.0f, 2.0f, 3.0f}) {
            auto* row = AddRow(sec3, 12.0f);
            auto* num = row->Add(std::make_unique<NumericUpDown>());
            num->SetWidth(160.0f);
            num->SetHAlign(HAlign::Left);
            num->SetMax(100.0f);
            num->SetValue(64.0f);
            num->SetBorderThickness(t);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[48];
            swprintf_s(buf, L"BorderThickness = %.0f", t);
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec4 = AddSubSection(card, L"Background / Foreground / FontWeight");
        AddNote(sec4, L"只读式的「结果字段」常用浅底 + 粗体，和可输入字段区分开；"
                      L"这里仍然是可输入的，只是演示配色。");
        {
            auto* row = AddRow(sec4, 12.0f);
            auto* tinted = row->Add(std::make_unique<NumericUpDown>());
            tinted->SetWidth(160.0f);
            tinted->SetHAlign(HAlign::Left);
            tinted->SetMax(100.0f);
            tinted->SetValue(72.0f);
            tinted->SetBackground(D2D1::ColorF(0xF3F9F1, 1.0f));
            tinted->SetForeground(D2D1::ColorF(0x0B6A0B, 1.0f));
            tinted->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD);

            auto* plain = row->Add(std::make_unique<NumericUpDown>());
            plain->SetWidth(160.0f);
            plain->SetHAlign(HAlign::Left);
            plain->SetMax(100.0f);
            plain->SetValue(72.0f);

            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(L"左：自定义配色 + 半粗；右：跟随主题");
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }
    }

    // --- 4. 混合使用 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：下单小票");
        AddNote(card, L"数量 × 单价（可选加 13% 税）→ 合计。数量、单价、税三个输入的载荷类型"
                      L"分别是 float& / float& / bool&，签名不同所以是三个 handler，"
                      L"但都通过 owner 拿到同一批成员指针后算同一个结果。");

        auto* form = card->Add(std::make_unique<Grid>());
        form->AddColumn(GridLength::Auto());
        form->AddColumn(GridLength::Pixels(150.0f));
        form->SetColumnSpacing(12.0f);
        form->SetRowSpacing(8.0f);

        form->AddRow(GridLength::Auto());
        auto* qtyLab = form->Add(std::make_unique<TextBlock>());
        form->SetCell(qtyLab, 0, 0);
        qtyLab->SetText(L"数量");
        qtyLab->SetHAlign(HAlign::Left);
        qtyLab->SetVAlign(VAlign::Center);
        auto* qty = form->Add(std::make_unique<NumericUpDown>());
        form->SetCell(qty, 0, 1);
        qty->SetMin(1.0f);
        qty->SetMax(999.0f);
        qty->SetStep(1.0f);
        qty->SetValue(3.0f);
        qty->SetHAlign(HAlign::Stretch);
        qty->SetVAlign(VAlign::Center);

        form->AddRow(GridLength::Auto());
        auto* priceLab = form->Add(std::make_unique<TextBlock>());
        form->SetCell(priceLab, 1, 0);
        priceLab->SetText(L"单价（元）");
        priceLab->SetHAlign(HAlign::Left);
        priceLab->SetVAlign(VAlign::Center);
        auto* price = form->Add(std::make_unique<NumericUpDown>());
        form->SetCell(price, 1, 1);
        price->SetMin(0.0f);
        price->SetMax(9999.0f);
        price->SetStep(0.5f);
        price->SetDecimalPlaces(2);
        price->SetValue(19.5f);
        price->SetHAlign(HAlign::Stretch);
        price->SetVAlign(VAlign::Center);

        auto* tax = card->Add(std::make_unique<CheckBox>());
        tax->SetText(L"含 13% 增值税");
        tax->SetChecked(false);
        tax->SetHAlign(HAlign::Left);

        auto* total = card->Add(std::make_unique<TextBlock>());
        total->SetText(L"合计：58.50 元");
        total->SetFontSize(18.0f);
        total->SetMargin(Thickness{0, 8.0f, 0, 0});

        auto* submit = card->Add(std::make_unique<Button>());
        submit->SetText(L"提交订单");
        submit->SetKind(Button::Kind::Accent);
        submit->SetHAlign(HAlign::Left);

        numMixQty_ = qty;
        numMixPrice_ = price;
        numMixTax_ = tax;
        numMixTotal_ = total;

        pageSubs_ += qty->ValueChanged().Subscribe(this,
            [](void* owner, NumericUpDown&, float&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->numMixQty_ || !self->numMixPrice_ ||
                    !self->numMixTax_ || !self->numMixTotal_) return;
                float sum = self->numMixQty_->Value() * self->numMixPrice_->Value();
                if (self->numMixTax_->IsChecked()) sum *= 1.13f;
                wchar_t buf[64];
                swprintf_s(buf, L"合计：%.2f 元", sum);
                self->numMixTotal_->SetText(buf);
            });
        pageSubs_ += price->ValueChanged().Subscribe(this,
            [](void* owner, NumericUpDown&, float&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->numMixQty_ || !self->numMixPrice_ ||
                    !self->numMixTax_ || !self->numMixTotal_) return;
                float sum = self->numMixQty_->Value() * self->numMixPrice_->Value();
                if (self->numMixTax_->IsChecked()) sum *= 1.13f;
                wchar_t buf[64];
                swprintf_s(buf, L"合计：%.2f 元", sum);
                self->numMixTotal_->SetText(buf);
            });
        pageSubs_ += tax->Checked().Subscribe(this,
            [](void* owner, CheckBox&, bool&) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->numMixQty_ || !self->numMixPrice_ ||
                    !self->numMixTax_ || !self->numMixTotal_) return;
                float sum = self->numMixQty_->Value() * self->numMixPrice_->Value();
                if (self->numMixTax_->IsChecked()) sum *= 1.13f;
                wchar_t buf[64];
                swprintf_s(buf, L"合计：%.2f 元", sum);
                self->numMixTotal_->SetText(buf);
            });
    }

    CreateCodeExample(content, LR"(auto* num = panel->Add(std::make_unique<NumericUpDown>());
num->SetMin(0.0f);                 // 注意：是 SetMin/SetMax
num->SetMax(100.0f);               // （RangeBase SetMinimum/SetMaximum 的别名）
num->SetStep(0.5f);                // 步长网格从 Min 起算，不是从 0
num->SetDecimalPlaces(2);          // 只管显示位数，不改变可取值
num->SetValue(19.5f);

// 布局：右侧箭头固定 20 DIP，文字区拿剩下的宽度
num->SetHAlign(HAlign::Stretch);
num->SetMinWidth(96.0f);           // 再窄就读不出数字了
num->SetMaxWidth(200.0f);

// 样式
num->SetAccentColor(D2D1::ColorF(0x107C10, 1.0f));  // 聚焦时的边框色
num->SetBorderThickness(0.0f);                       // 0 = 完全不描边

// handler 是无捕获函数指针，参数是引用
sub_ = num->ValueChanged().Subscribe(label,
    [](void* o, NumericUpDown&, float& v) {
        wchar_t buf[32]; swprintf_s(buf, L"%.2f", v);
        static_cast<TextBlock*>(o)->SetText(buf);
    });)");
    return std::move(page);
}

std::unique_ptr<ScrollPanel> GalleryApp::CreateRatingPage() {
    auto [page, content] = CreatePageShell(L"Rating");

    // --- 1. 状态 ---------------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"状态");
        AddNote(card, L"小数值只用于「显示」服务端算出来的平均分：小数部分 ≥ 0.5 时把边界那颗"
                      L"星点亮，所以 3.4 显示 3 颗、3.6 显示 4 颗。用户点击永远只能提交整数星，"
                      L"半星输入是另一个特性，不由小数显示顺带提供。");

        auto* sec1 = AddSubSection(card, L"可交互（默认）");
        auto* basic = sec1->Add(std::make_unique<Rating>());
        basic->SetValue(3.0f);
        basic->SetHAlign(HAlign::Left);
        basic->SetTooltip(L"点击星星提交；← → 调整，Home/End 到端点");
        auto* basicLabel = sec1->Add(std::make_unique<TextBlock>());
        basicLabel->SetText(L"当前评分：3 / 5");
        basicLabel->SetFontSize(12.0f);
        basicLabel->SetDimmed(true);
        pageSubs_ += basic->ValueChanged().Subscribe(basicLabel,
            [](void* owner, Rating& sender, float& value) {
                wchar_t buf[48];
                swprintf_s(buf, L"当前评分：%.0f / %d", value, sender.MaxValue());
                static_cast<TextBlock*>(owner)->SetText(buf);
            });
        AddNote(sec1, L"再点一次当前那颗星会清零 —— 这是「取消评分」唯一的鼠标手势。");

        auto* sec2 = AddSubSection(card, L"小数值：显示平均分");
        for (float v : {0.0f, 2.4f, 2.6f, 4.9f}) {
            auto* row = AddRow(sec2, 12.0f);
            auto* r = row->Add(std::make_unique<Rating>());
            r->SetValue(v);
            r->SetReadOnly(true);
            r->SetHAlign(HAlign::Left);
            r->SetVAlign(VAlign::Center);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[64];
            swprintf_s(buf, L"Value = %.1f → 亮 %d 颗", v,
                       static_cast<int>(v) + ((v - static_cast<int>(v)) >= 0.5f ? 1 : 0));
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec3 = AddSubSection(card, L"只读与禁用：两种「不能改」不一样");
        AddNote(sec3, L"只读是纯展示：退出 Tab 序、不预览、但颜色照常（它就是要好看）。"
                      L"禁用则连颜色都收掉 —— 形状还在，可操作性没了。两者语义不同，不要互相替代。");
        {
            auto* row = AddRow(sec3, 12.0f);
            auto* ro = row->Add(std::make_unique<Rating>());
            ro->SetValue(4.0f);
            ro->SetReadOnly(true);
            ro->SetHAlign(HAlign::Left);
            ro->SetVAlign(VAlign::Center);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(L"ReadOnly：他人的评价，只看不改");
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }
        {
            auto* row = AddRow(sec3, 12.0f);
            auto* dis = row->Add(std::make_unique<Rating>());
            dis->SetValue(4.0f);
            dis->SetEnabled(false);
            dis->SetHAlign(HAlign::Left);
            dis->SetVAlign(VAlign::Center);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(L"Disabled：暂时不可评（比如还没下单）");
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec4 = AddSubSection(card, L"MaxValue：星数就是上限");
        AddNote(sec4, L"MaxValue 同时是「画几颗星」和「最大值」，所以它必须让 Measure 变脏 —— "
                      L"宽度是它的函数。每帧 DrawText 次数是 O(MaxValue)，5 颗合适，"
                      L"列表里几百行评分应该改画缓存好的文本而不是几百个这个控件。");
        for (int m : {3, 5, 10}) {
            auto* row = AddRow(sec4, 12.0f);
            auto* r = row->Add(std::make_unique<Rating>());
            r->SetMaxValue(m);
            r->SetValue(static_cast<float>(m) * 0.6f);
            r->SetHAlign(HAlign::Left);
            r->SetVAlign(VAlign::Center);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[48];
            swprintf_s(buf, L"MaxValue = %d", m);
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }
    }

    // --- 2. 布局行为 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"在布局里的行为");
        AddNote(card, L"和 Slider 相反：Rating 有确定的自然宽度（MaxValue × IconSize + 间隙），"
                      L"Measure 根本不看可用宽度。所以给它 Stretch 并不会把星星摊开 —— "
                      L"bounds_ 变宽了，星星仍然从左边缘开始画，右边只是留白。");

        auto* sec1 = AddSubSection(card, L"Stretch 不改变星星，只改变留白");
        {
            auto* box = AddBoundedBox(sec1, 460.0f, L"HAlign::Stretch：右侧全是空白");
            auto* r = box->Add(std::make_unique<Rating>());
            r->SetValue(3.0f);
            r->SetHAlign(HAlign::Stretch);
        }
        {
            auto* box = AddBoundedBox(sec1, 460.0f, L"HAlign::Left：宽度就是内容宽度");
            auto* r = box->Add(std::make_unique<Rating>());
            r->SetValue(3.0f);
            r->SetHAlign(HAlign::Left);
        }
        AddNote(sec1, L"要让评分在一行里「靠右」，正确做法是用 HAlign::Right 或 Grid 列，"
                      L"而不是给它 Stretch —— Stretch 只是把空白吃进控件的 bounds 里。");

        auto* sec2 = AddSubSection(card, L"写死宽度小于自然宽度：会溢出，不会缩");
        AddNote(sec2, L"Render 无条件画 MaxValue 颗星、也不裁剪，所以宽度不够时星星画到框外去了。"
                      L"这不是布局 bug，而是「显式尺寸是输入」的直接后果：想变小应该改 IconSize，"
                      L"不是改 Width。");
        {
            auto* box = AddBoundedBox(sec2, 460.0f, L"Width = 60（自然宽度约 116）：溢出");
            auto* r = box->Add(std::make_unique<Rating>());
            r->SetValue(4.0f);
            r->SetWidth(60.0f);
            r->SetHAlign(HAlign::Left);
        }
        {
            auto* box = AddBoundedBox(sec2, 460.0f, L"改 IconSize = 12：真正变小");
            auto* r = box->Add(std::make_unique<Rating>());
            r->SetValue(4.0f);
            r->SetIconSize(12.0f);
            r->SetHAlign(HAlign::Left);
        }

        auto* sec3 = AddSubSection(card, L"MinWidth / MaxWidth 夹逼");
        AddNote(sec3, L"因为自然宽度固定，Min/Max 对这个控件的作用是「把 bounds 撑大/压小」而不是"
                      L"重排内容 —— MaxWidth 小于自然宽度时和写死 Width 一样会溢出。");
        for (float w : {480.0f, 200.0f}) {
            auto* box = AddBoundedBox(sec3, w,
                std::wstring(L"容器 ") + std::to_wstring(static_cast<int>(w)) +
                L" DIP，评分 MinWidth=160（比自然宽度大）");
            auto* r = box->Add(std::make_unique<Rating>());
            r->SetValue(3.0f);
            r->SetHAlign(HAlign::Left);
            r->SetMinWidth(160.0f);
        }

        auto* sec4 = AddSubSection(card, L"四种水平对齐");
        struct RateAlignCase { HAlign align; const wchar_t* caption; };
        static constexpr RateAlignCase kRateAligns[] = {
            {HAlign::Left,    L"HAlign::Left"},
            {HAlign::Center,  L"HAlign::Center"},
            {HAlign::Right,   L"HAlign::Right"},
            {HAlign::Stretch, L"HAlign::Stretch（等同于 Left + 留白）"},
        };
        for (const RateAlignCase& ac : kRateAligns) {
            auto* box = AddBoundedBox(sec4, 440.0f, ac.caption);
            auto* r = box->Add(std::make_unique<Rating>());
            r->SetValue(4.0f);
            r->SetHAlign(ac.align);
        }

        auto* sec5 = AddSubSection(card, L"评价明细：标签 + 星星 + 计数的三列 Grid");
        AddNote(sec5, L"星星列用 Auto —— 它有自然宽度，Star 只会给它多余的空白。"
                      L"计数列右对齐（DirectWrite 的 TRAILING），数字变化时不会左右跳。");
        auto* g = sec5->Add(std::make_unique<Grid>());
        g->AddColumn(GridLength::Pixels(72.0f));
        g->AddColumn(GridLength::Auto());
        g->AddColumn(GridLength::Star(1.0f));
        g->SetColumnSpacing(12.0f);
        g->SetRowSpacing(6.0f);
        struct RatingRow { const wchar_t* aspect; float score; const wchar_t* count; };
        static constexpr RatingRow kRows[] = {
            {L"物流",   4.0f, L"1,204 人评"},
            {L"包装",   5.0f, L"988 人评"},
            {L"性价比", 3.0f, L"1,431 人评"},
            {L"客服",   4.0f, L"612 人评"},
        };
        for (int i = 0; i < 4; ++i) {
            g->AddRow(GridLength::Auto());

            auto* lab = g->Add(std::make_unique<TextBlock>());
            g->SetCell(lab, i, 0);
            lab->SetText(kRows[i].aspect);
            lab->SetHAlign(HAlign::Left);
            lab->SetVAlign(VAlign::Center);

            auto* r = g->Add(std::make_unique<Rating>());
            g->SetCell(r, i, 1);
            r->SetValue(kRows[i].score);
            r->SetReadOnly(true);
            r->SetIconSize(16.0f);
            r->SetHAlign(HAlign::Left);
            r->SetVAlign(VAlign::Center);

            auto* cnt = g->Add(std::make_unique<TextBlock>());
            g->SetCell(cnt, i, 2);
            cnt->SetText(kRows[i].count);
            cnt->SetFontSize(12.0f);
            cnt->SetDimmed(true);
            cnt->SetVAlign(VAlign::Center);
            // TextBlock 的对齐参数是 DirectWrite 的枚举，不是框架自己的 TextAlignment。
            cnt->SetAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }

    // --- 3. 样式定制 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"样式定制");
        AddNote(card, L"Rating 的 Render 只读三个可覆盖属性：AccentColor（点亮的星）、"
                      L"FontWeight（字形粗细）、以及它自己的 IconSize。"
                      L"CornerRadius / BorderThickness / Background 在这个控件上没有几何可以作用，"
                      L"设了不会报错但也不会有任何变化 —— 所以这里不演示它们。");

        auto* sec1 = AddSubSection(card, L"AccentColor：点亮的星星用什么颜色");
        AddNote(sec1, L"悬停预览走的是主题的 accentHover，不受这个覆盖影响，"
                      L"所以「已提交」和「将要提交」始终能区分开。");
        int n = 0;
        const AccentSwatch* sw = AccentSwatches(n);
        for (int i = 0; i < n; ++i) {
            auto* row = AddRow(sec1, 12.0f);
            auto* r = row->Add(std::make_unique<Rating>());
            r->SetValue(4.0f);
            r->SetHAlign(HAlign::Left);
            r->SetVAlign(VAlign::Center);
            r->SetAccentColor(D2D1::ColorF(sw[i].color, 1.0f));
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(sw[i].label);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec2 = AddSubSection(card, L"IconSize：这才是 Rating 的「字号」");
        AddNote(sec2, L"星星是文本字形，IconSize 就是它的字号，被夹在 [12, 48] 之间，"
                      L"并且会让 Measure 变脏（宽度 = MaxValue × IconSize + 间隙）。"
                      L"注意 SetFontSize 对它无效 —— Render 用的是 iconSize_。");
        for (float s : {12.0f, 20.0f, 28.0f, 40.0f}) {
            auto* row = AddRow(sec2, 12.0f);
            auto* r = row->Add(std::make_unique<Rating>());
            r->SetValue(4.0f);
            r->SetIconSize(s);
            r->SetHAlign(HAlign::Left);
            r->SetVAlign(VAlign::Center);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            wchar_t buf[64];
            swprintf_s(buf, L"IconSize = %.0f（宽度约 %.0f）", s, 5 * s + 4 * 4.0f);
            cap->SetText(buf);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec3 = AddSubSection(card, L"FontWeight：字形本身的粗细");
        AddNote(sec3, L"星形字形在不同字重下的差别很小（很多字体的符号区并没有多个字重），"
                      L"所以这里放三档对照说明「设得上但别指望明显」，而不是当作主要手段。");
        struct WeightCase { DWRITE_FONT_WEIGHT weight; const wchar_t* label; };
        static const WeightCase kWeights[] = {
            {DWRITE_FONT_WEIGHT_LIGHT,     L"Light"},
            {DWRITE_FONT_WEIGHT_NORMAL,    L"Normal（默认）"},
            {DWRITE_FONT_WEIGHT_SEMI_BOLD, L"SemiBold"},
        };
        for (const WeightCase& wc : kWeights) {
            auto* row = AddRow(sec3, 12.0f);
            auto* r = row->Add(std::make_unique<Rating>());
            r->SetValue(3.0f);
            r->SetIconSize(24.0f);
            r->SetFontWeight(wc.weight);
            r->SetHAlign(HAlign::Left);
            r->SetVAlign(VAlign::Center);
            auto* cap = row->Add(std::make_unique<TextBlock>());
            cap->SetText(wc.label);
            cap->SetFontSize(12.0f);
            cap->SetDimmed(true);
            cap->SetVAlign(VAlign::Center);
        }

        auto* sec4 = AddSubSection(card, L"组合：颜色 + 尺寸对应不同用途");
        AddNote(sec4, L"实际项目里这三种一般同时出现：主评分要大而醒目，明细行小而低调，"
                      L"警示项换成红色。");
        {
            auto* row = AddRow(sec4, 24.0f);
            auto* hero = row->Add(std::make_unique<Rating>());
            hero->SetValue(5.0f);
            hero->SetIconSize(32.0f);
            hero->SetAccentColor(D2D1::ColorF(0xF7630C, 1.0f));
            hero->SetHAlign(HAlign::Left);
            hero->SetVAlign(VAlign::Center);

            auto* detail = row->Add(std::make_unique<Rating>());
            detail->SetValue(4.0f);
            detail->SetIconSize(14.0f);
            detail->SetReadOnly(true);
            detail->SetHAlign(HAlign::Left);
            detail->SetVAlign(VAlign::Center);

            auto* warn = row->Add(std::make_unique<Rating>());
            warn->SetValue(1.0f);
            warn->SetIconSize(20.0f);
            warn->SetAccentColor(D2D1::ColorF(0xC42B1C, 1.0f));
            warn->SetReadOnly(true);
            warn->SetHAlign(HAlign::Left);
            warn->SetVAlign(VAlign::Center);
        }
    }

    // --- 4. 混合使用 -----------------------------------------------------
    {
        auto* card = CreateExampleCard(content, L"混合使用：写一条评价");
        AddNote(card, L"一个评分值同时驱动结论文字和满意度进度条 —— 一个值源多个消费者，"
                      L"且都在同一帧内更新。所有消费者都挂在 GalleryApp 上，因为无捕获"
                      L"函数指针只能通过 owner 找到它们。");

        auto* head = AddRow(card, 12.0f);
        auto* headLab = head->Add(std::make_unique<TextBlock>());
        headLab->SetText(L"总体评分");
        headLab->SetVAlign(VAlign::Center);
        auto* stars = head->Add(std::make_unique<Rating>());
        stars->SetValue(4.0f);
        stars->SetIconSize(28.0f);
        stars->SetHAlign(HAlign::Left);
        stars->SetVAlign(VAlign::Center);

        auto* verdict = card->Add(std::make_unique<TextBlock>());
        verdict->SetText(L"结论：满意");
        verdict->SetFontSize(16.0f);

        auto* bar = card->Add(std::make_unique<ProgressBar>());
        bar->SetValue(0.8f);
        bar->SetWidth(320.0f);
        bar->SetHAlign(HAlign::Left);

        auto* anonymous = card->Add(std::make_unique<CheckBox>());
        anonymous->SetText(L"匿名发布");
        anonymous->SetChecked(true);
        anonymous->SetHAlign(HAlign::Left);

        auto* post = card->Add(std::make_unique<Button>());
        post->SetText(L"发布评价");
        post->SetKind(Button::Kind::Accent);
        post->SetHAlign(HAlign::Left);

        ratingMixStars_ = stars;
        ratingMixText_ = verdict;
        ratingMixBar_ = bar;

        pageSubs_ += stars->ValueChanged().Subscribe(this,
            [](void* owner, Rating& sender, float& value) {
                auto* self = static_cast<GalleryApp*>(owner);
                if (!self->ratingMixText_ || !self->ratingMixBar_) return;
                // 文案按整数星查表，比 if-else 链更容易看出边界；索引来自已被
                // 控件夹紧的 value，所以不会越界。
                static const wchar_t* const kVerdicts[] = {
                    L"结论：未评分", L"结论：很不满意", L"结论：不满意",
                    L"结论：一般",   L"结论：满意",     L"结论：非常满意",
                };
                const int idx = static_cast<int>(value);
                self->ratingMixText_->SetText(kVerdicts[idx]);
                const int maxV = sender.MaxValue() > 0 ? sender.MaxValue() : 1;
                self->ratingMixBar_->SetValue(value / static_cast<float>(maxV));
            });
    }

    CreateCodeExample(content, LR"(auto* rating = panel->Add(std::make_unique<Rating>());
rating->SetMaxValue(5);            // 星数 = 上限，改它会让 Measure 变脏
rating->SetValue(3.6f);            // 小数仅用于显示平均分（≥0.5 点亮边界星）
rating->SetReadOnly(true);         // 纯展示：退出 Tab 序、不预览

// 尺寸：Rating 有自然宽度（MaxValue × IconSize + 间隙），Stretch 只增加留白。
// 想变小要改 IconSize，写死 Width 只会让星星画到框外。
rating->SetIconSize(16.0f);        // 夹在 [12, 48]
rating->SetHAlign(HAlign::Left);

// 样式：Render 只读 AccentColor / FontWeight / IconSize，
// CornerRadius、Background 之类在这个控件上没有作用。
rating->SetAccentColor(D2D1::ColorF(0xF7630C, 1.0f));

// handler 是无捕获函数指针，参数是引用
sub_ = rating->ValueChanged().Subscribe(bar,
    [](void* o, Rating& s, float& v) {
        static_cast<ProgressBar*>(o)->SetValue(v / s.MaxValue());
    });)");
    return std::move(page);
}

} // namespace fluent
