#include "ContentDialog.h"

namespace fluent {

void ContentDialog::AddButton(std::wstring text, DialogResult result) {
    buttons_.push_back({std::move(text), result});
}

void ContentDialog::OnInitialize() {
    auto root = std::make_unique<StackPanel>();
    root->SetOrientation(StackPanel::Orientation::Vertical);
    root->SetSpacing(12.0f);

    auto title = std::make_unique<TextBlock>();
    title->SetText(Title());
    title->SetTypographyRole(TypographyRole::Subtitle);
    title->SetHeight(30.0f);
    root->Add(std::move(title));

    if (content_)
        root->Add(std::move(content_));

    auto actions = std::make_unique<StackPanel>();
    actions->SetOrientation(StackPanel::Orientation::Horizontal);
    actions->SetSpacing(8.0f);
    actions->SetHeight(36.0f);
    actions->SetHAlign(HAlign::Right);
    for (const ButtonSpec& spec : buttons_) {
        auto button = std::make_unique<Button>();
        button->SetText(spec.text);
        button->SetWidth(96.0f);
        button->SetHeight(32.0f);
        button->SetKind(spec.result == DialogResult::Primary
                            ? Button::Kind::Accent : Button::Kind::Standard);
        Button* raw = button.get();
        buttonControls_.push_back(raw);
        buttonSubs_.Keep(raw->Click().Subscribe<&ContentDialog::OnButtonClick>(this));
        if (!defaultButton_ && spec.result == defaultResult_)
            defaultButton_ = raw;
        actions->Add(std::move(button));
    }
    root->Add(std::move(actions));
    SetRoot(std::move(root));
}

void ContentDialog::OnLoaded(WindowEventArgs&) {
    if (defaultButton_)
        SetFocusElement(defaultButton_);
}

void ContentDialog::OnButtonClick(Button& sender, RoutedEventArgs&) {
    // Members are reachable directly now — no `self` indirection.
    for (size_t i = 0; i < buttonControls_.size(); ++i) {
        if (buttonControls_[i] == &sender && i < buttons_.size()) {
            Close(buttons_[i].result);
            return;
        }
    }
}

} // namespace fluent
