// ContentDialog.h — the canned "title + content + button row" dialog.
//
// SCOPE, READ THIS FIRST. This is NOT the general way to write a dialog. Its
// layout is fixed — a title block, one content slot, a right-aligned button row —
// and that shape is the entire value it adds: it saves you the ~50 lines of title
// block, button row, result mapping and initial focus that a message box needs.
// The content slot accepts any FrameworkElement, so it is not a cage, but as soon
// as your dialog wants a layout this template does not describe, you are fighting
// it for no benefit.
//
// For anything beyond a message box, derive from DialogWindow directly and build
// the tree in OnInitialize — that is the supported, Win32/WPF-shaped path and it
// has no ceiling. MessageDialog (MessageDialog.h) wraps this class for the
// one-line case, which is where it earns its keep.
//
// It deliberately composes existing TextBlock, Panel, StackPanel and Button
// controls instead of introducing a template or markup runtime.
#pragma once

#include "../window/DialogWindow.h"
#include "../layout/StackPanel.h"
#include "../controls/TextBlock.h"
#include "Button.h"
#include <memory>
#include <string>
#include <vector>

namespace fluent {

class ContentDialog : public DialogWindow {
public:
    ContentDialog() { SetTitle(L"Dialog"); }

    // Ownership transfers to the dialog. The content is a FrameworkElement so it
    // can be any existing leaf or panel and is attached like normal tree content.
    void SetContent(std::unique_ptr<FrameworkElement> content) {
        content_ = std::move(content);
    }

    void AddButton(std::wstring text, DialogResult result);
    size_t ButtonCount() const { return buttons_.size(); }
    void SetDefaultResult(DialogResult result) { defaultResult_ = result; }

protected:
    void OnInitialize() override;
    void OnLoaded(WindowEventArgs&) override;

private:
    struct ButtonSpec {
        std::wstring text;
        DialogResult result = DialogResult::Custom;
    };

    // An ordinary member function, not a static thunk taking void* — see the
    // Subscribe<Method>(owner) overload in base/Event.h. Still zero-allocation:
    // the captureless lambda that adapts it decays to a plain function pointer.
    void OnButtonClick(Button& sender, RoutedEventArgs& args);

    std::unique_ptr<FrameworkElement> content_;
    std::vector<ButtonSpec> buttons_;
    std::vector<Button*> buttonControls_;
    // One member for a dynamic number of subscriptions (one per button), instead
    // of a hand-maintained std::vector<Subscription>.
    SubscriptionBag buttonSubs_;
    Button* defaultButton_ = nullptr;
    DialogResult defaultResult_ = DialogResult::Primary;
};

} // namespace fluent
