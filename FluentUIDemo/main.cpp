// main.cpp — FluentUI Gallery entry point (Stage F: Gallery rewrite)
// Replaced the old acceptance demo (1400+ lines) with a minimal wWinMain that
// creates a Window, builds GalleryApp content, and runs the message loop.
//
// The old demo is preserved in main_old.cpp for reference.

#include "../FluentUI/window/Window.h"
#include "../FluentUI/Application.h"
#include "../FluentSettings/ConfigStore.h"
#include "GalleryMain.h"

using namespace fluent;

// === WINDOW WRAPPER ================================================================
// Gallery 不需要自定义窗口行为，所以直接用 Window + OnInitialize 装内容。

class GalleryWindow final : public Window {
public:
    explicit GalleryWindow(ConfigStore* cfg) : cfg_(cfg) {
        SetClientSize(1100.0f, 800.0f);
        SetCornerRadius(12.0f);
        SetShadowEnabled(true);
    }

    // Override to match the visual title bar height (48 DIP) so the entire
    // visible title bar area is draggable, not just the top 32 DIP.
    float TitleBarHeightDip() const override { return 48.0f; }

protected:
    void OnInitialize() override {
        // GalleryApp 必须是成员（不能是局部变量），否则它持有的 navSub_ 和
        // flyouts_ 在函数返回时析构 → 导航失效 + flyout 指针悬空崩溃。
        gallery_ = std::make_unique<GalleryApp>();
        // 主题切换由窗口执行：GalleryApp 拿不到 NativeWindowHost::SetDarkMode。
        // OwningApplication() 现在在 OnInitialize 里可用（PrepareContent 在调用
        // OnInitialize 之前设置 application_），所以钩子可以在这里注入。
        gallery_->SetThemeHooks(
            [this](bool wantDark) {
                SetDarkMode(wantDark);
                return IsDarkMode();
            },
            [this] { return IsDarkMode(); });
        // 日志演示的后台生产者要把数据交回 UI 线程，走 Application::Post。
        if (Application* app = OwningApplication()) {
            gallery_->SetUiPost([app](std::function<void()> fn) {
                app->Post(std::move(fn));
            });
        }
        // 对话框演示需要 NativeWindowHost& 引用（DialogWindow::ShowDialog/Show）。
        gallery_->SetOwnerWindowProvider([this] { return this; });

        SetRoot(gallery_->BuildContent());
    }

    void OnContentCreated() override {
        // (empty — 钩子已在 OnInitialize 注入)
    }

    // Window::OnDestroying is final (it owns the Unloaded ordering), so the save
    // hook is OnUnloaded — which fires from inside it, while the HWND is still
    // valid. That validity is what CaptureWindowState needs.
    void OnUnloaded(WindowEventArgs&) override {
        if (!cfg_) return;
        WindowState s = CaptureWindowState();
        if (s.valid) {
            cfg_->SetInt("window.x", s.x);
            cfg_->SetInt("window.y", s.y);
            cfg_->SetInt("window.width", s.width);
            cfg_->SetInt("window.height", s.height);
            cfg_->SetBool("window.maximized", s.maximized);
            cfg_->SetInt("window.dpi", s.dpi);
        }
        cfg_->SetBool("theme.dark", IsDarkMode());
        cfg_->Save();
    }

private:
    ConfigStore* cfg_ = nullptr;
    std::unique_ptr<GalleryApp> gallery_;  // 持有 GalleryApp，保证订阅/flyout 生命期
};

// === ENTRY POINT ==================================================================
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // Application owns this thread's COM apartment (WIC for Image, IFileDialog for
    // FileDialog), so it is declared FIRST and therefore destroyed LAST — after
    // every window and element tree below has released its COM interfaces. Moving
    // any declaration above this one reintroduces 0xC0000005 on exit; see the
    // ordering contract in Application.h.
    Application app(hInst);
    if (!app.ComReady()) return 1;

    // App-owned settings store (FluentSettings). Loaded before the window so the
    // saved placement can be handed to Show() as plain data — the window itself
    // does no file I/O.
    ConfigStore cfg;
    cfg.Initialize(L"FluentUIGallery");

    bool wantDark = cfg.GetBool("theme.dark", SystemUsesDarkMode());

    WindowState restore;
    if (cfg.Has("window.width") && cfg.Has("window.height")) {
        restore.x = static_cast<int>(cfg.GetInt("window.x", 0));
        restore.y = static_cast<int>(cfg.GetInt("window.y", 0));
        restore.width = static_cast<int>(cfg.GetInt("window.width", 0));
        restore.height = static_cast<int>(cfg.GetInt("window.height", 0));
        restore.maximized = cfg.GetBool("window.maximized", false);
        restore.dpi = static_cast<UINT>(cfg.GetInt("window.dpi", 96));
        restore.valid = restore.width > 0 && restore.height > 0;
    }

    GalleryWindow win(&cfg);
    if (FAILED(win.Show(app, restore.valid ? &restore : nullptr))) {
        return 1;
    }
    win.SetMinClientSizeDip(100.0f, 100.0f); 
    win.SetDarkMode(wantDark);

    int exitCode = app.Run();
    return exitCode;
}
