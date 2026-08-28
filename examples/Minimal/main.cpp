#include <FluentUI/FluentUI.h>

using namespace fluent;

class MainWindow final : public Window {
public:
    MainWindow() {
        SetTitle(L"FluentUI Minimal");
        SetClientSize(640.0f, 360.0f);
    }

protected:
    void OnInitialize() override {
        auto root = std::make_unique<StackPanel>();
        root->SetSpacing(12.0f);
        root->SetMargin(Thickness(24.0f));

        auto* heading = root->Emplace<TextBlock>();
        heading->SetText(L"Hello from FluentUI");
        heading->SetTypographyRole(TypographyRole::Title);
        heading->SetHeight(48.0f);

        auto* button = root->Emplace<Button>();
        button->SetText(L"Close");
        button->SetWidth(120.0f);
        button->SetHAlign(HAlign::Left);
        closeSub_ = button->Click().Subscribe<&MainWindow::OnClose>(this);
        SetRoot(std::move(root));
    }

private:
    void OnClose(Button&, RoutedEventArgs&) { Close(); }
    Subscription closeSub_;
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Application app(instance);
    MainWindow window;
    if (FAILED(window.Show(app))) return 1;
    return app.Run();
}
