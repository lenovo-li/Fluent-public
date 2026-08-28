// WindowLifecycleTests.cpp — headless checks for the Window/DialogWindow
// lifecycle seam introduced when DialogWindow became a Window subclass.
//
// These cannot create a real HWND, so they exercise the Raise* virtuals directly.
// That is deliberately the interesting part: Window fixes the ORDER of
// Closing -> Unloaded -> Closed and DialogWindow varies only the args, so a
// regression shows up as wrong order, a missed veto, or a result committed on a
// close that was cancelled. None of that needs a window.
//
// What is NOT covered here: anything gated on IsOpen() (Close(), Show(),
// ShowDialog(), the modal pump) and owner enable/restore, which need a live HWND.
#include "../framework/Test.h"
#include "../../FluentUI/window/DialogWindow.h"
#include "LayoutTestHelpers.h"
#include <string>
#include <vector>

using namespace fluent;

namespace {

// Exposes the protected lifecycle seam and records the order it fires in.
class WindowProbe : public Window {
public:
    std::vector<std::string> log;
    bool vetoClose = false;

    bool CallRaiseClosing() { return RaiseClosing(); }
    void CallRaiseUnloaded() { RaiseUnloaded(); }
    void CallRaiseClosed() { RaiseClosed(); }
    // Headless: no Application exists, so pass nullptr. PrepareContent accepts it
    // and leaves application_ null, which is the same state these tests had before
    // the parameter was added.
    HRESULT CallPrepareContent() { return PrepareContent(nullptr); }
    void CallSetRoot(std::unique_ptr<UIElement> r) { SetRoot(std::move(r)); }
    UIElement* CallRoot() const { return Root(); }

protected:
    void OnClosing(WindowClosingArgs& a) override {
        log.push_back("closing");
        if (vetoClose) a.cancel = true;
    }
    void OnUnloaded(WindowEventArgs&) override { log.push_back("unloaded"); }
    void OnClosed(WindowEventArgs&) override { log.push_back("closed"); }
};

// A Window whose OnInitialize supplies a root, so PrepareContent succeeds.
class RootedWindowProbe : public WindowProbe {
protected:
    void OnInitialize() override {
        log.push_back("initialize");
        CallSetRoot(std::make_unique<TestLeaf>());
    }
};

class DialogProbe : public DialogWindow {
public:
    std::vector<std::string> log;
    bool vetoWindowChannel = false;
    bool vetoDialogChannel = false;

    bool CallRaiseClosing() { return RaiseClosing(); }
    void CallRaiseUnloaded() { RaiseUnloaded(); }
    void CallRaiseClosed() { RaiseClosed(); }

protected:
    void OnClosing(WindowClosingArgs& a) override {
        log.push_back("window-closing");
        if (vetoWindowChannel) a.cancel = true;
    }
    void OnDialogClosing(DialogClosingArgs& a) override {
        log.push_back("dialog-closing");
        if (vetoDialogChannel) a.cancel = true;
    }
    void OnUnloaded(WindowEventArgs&) override { log.push_back("unloaded"); }
    void OnClosed(WindowEventArgs&) override { log.push_back("window-closed"); }
    void OnDialogClosed(DialogClosedArgs&) override {
        log.push_back("dialog-closed");
    }
};

} // namespace

TEST(WindowLifecycle, RaiseOrderIsClosingThenUnloadedThenClosed) {
    WindowProbe w;
    EXPECT_TRUE(w.CallRaiseClosing());
    w.CallRaiseUnloaded();
    w.CallRaiseClosed();

    EXPECT_EQ(w.log.size(), static_cast<size_t>(3));
    EXPECT_EQ(w.log[0], std::string("closing"));
    EXPECT_EQ(w.log[1], std::string("unloaded"));
    EXPECT_EQ(w.log[2], std::string("closed"));
}

TEST(WindowLifecycle, ClosingVetoIsReportedToCaller) {
    WindowProbe w;
    w.vetoClose = true;
    // The veto must surface as a false return — that is what Window::Close reads
    // to decide whether to destroy the HWND.
    EXPECT_FALSE(w.CallRaiseClosing());
}

TEST(WindowLifecycle, UnloadedAndClosedAreRaisedOnlyOnce) {
    WindowProbe w;
    w.CallRaiseUnloaded();
    w.CallRaiseUnloaded();
    w.CallRaiseClosed();
    w.CallRaiseClosed();
    // Both are reachable by more than one route (explicit Close, WM_CLOSE, owner
    // teardown), so idempotence is a contract, not an accident.
    EXPECT_EQ(w.log.size(), static_cast<size_t>(2));
    EXPECT_EQ(w.log[0], std::string("unloaded"));
    EXPECT_EQ(w.log[1], std::string("closed"));
}

TEST(WindowLifecycle, PrepareContentRunsInitializeOnceAndRequiresRoot) {
    RootedWindowProbe good;
    EXPECT_EQ(good.CallPrepareContent(), S_OK);
    EXPECT_TRUE(good.IsInitialized());
    EXPECT_TRUE(good.CallRoot() != nullptr);
    // A second call must refuse: NativeWindowHost instances are not reusable, so a
    // second init would build a second tree onto the same object.
    EXPECT_EQ(good.CallPrepareContent(), E_INVALIDARG);
    EXPECT_EQ(good.log.size(), static_cast<size_t>(1));

    // No root supplied -> loud failure rather than a window that paints nothing.
    WindowProbe rootless;
    EXPECT_EQ(rootless.CallPrepareContent(), E_UNEXPECTED);
}

TEST(WindowLifecycle, WindowDefaultsAreMainWindowFlavoured) {
    WindowProbe w;
    EXPECT_EQ(w.Title(), std::wstring(L"FluentUI"));
    EXPECT_NEAR(w.ClientWidth(), 800.0f, 0.001f);
    EXPECT_NEAR(w.ClientHeight(), 600.0f, 0.001f);
    EXPECT_FALSE(w.IsInitialized());

    w.SetTitle(L"App");
    w.SetClientSize(1024.0f, 768.0f);
    EXPECT_EQ(w.Title(), std::wstring(L"App"));
    EXPECT_NEAR(w.ClientWidth(), 1024.0f, 0.001f);
    EXPECT_NEAR(w.ClientHeight(), 768.0f, 0.001f);
}

TEST(WindowLifecycle, StandardTitleBarIsExplicitAndConfigurable) {
    WindowProbe w;
    EXPECT_FALSE(w.StandardTitleBar());
    EXPECT_NEAR(w.TitleBarHeight(), 48.0f, 0.001f);
    w.SetStandardTitleBar(true);
    w.SetTitleBarHeight(52.0f);
    EXPECT_TRUE(w.StandardTitleBar());
    EXPECT_NEAR(w.TitleBarHeight(), 52.0f, 0.001f);
}

TEST(DialogLifecycle, BothChannelsFireWindowFirst) {
    DialogProbe d;
    EXPECT_TRUE(d.CallRaiseClosing());
    d.CallRaiseUnloaded();
    d.CallRaiseClosed();

    EXPECT_EQ(d.log.size(), static_cast<size_t>(5));
    EXPECT_EQ(d.log[0], std::string("window-closing"));
    EXPECT_EQ(d.log[1], std::string("dialog-closing"));
    EXPECT_EQ(d.log[2], std::string("unloaded"));
    EXPECT_EQ(d.log[3], std::string("window-closed"));
    EXPECT_EQ(d.log[4], std::string("dialog-closed"));
}

TEST(DialogLifecycle, VetoFromWindowChannelSkipsDialogChannel) {
    DialogProbe d;
    d.vetoWindowChannel = true;
    EXPECT_FALSE(d.CallRaiseClosing());
    // Short-circuit: once the window channel vetoes there is no close to describe,
    // so the dialog channel must not run at all.
    EXPECT_EQ(d.log.size(), static_cast<size_t>(1));
    EXPECT_EQ(d.log[0], std::string("window-closing"));
}

TEST(DialogLifecycle, VetoFromDialogChannelLeavesResultUntouched) {
    DialogProbe d;
    d.vetoDialogChannel = true;
    EXPECT_FALSE(d.CallRaiseClosing());
    // A cancelled close must not commit a result — Result() staying None is what
    // tells the caller nothing was decided.
    EXPECT_EQ(d.Result(), DialogResult::None);
}

TEST(DialogLifecycle, ClosedWithoutResultReadsAsCancel) {
    DialogProbe d;
    // Dismissed by the system / owner teardown, never through Close(result).
    d.CallRaiseClosed();
    EXPECT_EQ(d.Result(), DialogResult::Cancel);
}

TEST(DialogLifecycle, DialogDefaultsDifferFromWindowDefaults) {
    DialogProbe d;
    EXPECT_EQ(d.Title(), std::wstring(L"Fluent Dialog"));
    EXPECT_NEAR(d.ClientWidth(), 420.0f, 0.001f);
    EXPECT_NEAR(d.ClientHeight(), 260.0f, 0.001f);
    EXPECT_FALSE(d.IsDialogOpen());
    EXPECT_TRUE(d.Owner() == nullptr);
}
