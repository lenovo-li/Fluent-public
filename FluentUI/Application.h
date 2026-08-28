// Application.h — UI-thread coordinator and frame-aware message loop.
//
// Application owns the message pump, the set of top-level windows, and two
// shared services that need to reach any pump from any call site:
//
//   * Shutdown(exitCode)  — ask the app to stop. Posts to a message-only HWND
//     so the message reaches our WndProc via DispatchMessage regardless of
//     which pump is currently running (system modal move/resize loop, legacy
//     GetMessage-based dialog pump, etc.).
//
//   * Post(fn)  — enqueue work from a background thread. The work runs on the
//     UI thread inside the next PumpOneTurn turn. Same mechanism: the message-
//     only window's WndProc drains the queue when it receives WM_APP_DISPATCH.
//
// ShutdownMode controls when a silent pump stop is triggered by window closure:
//
//   OnMainWindowClose (default)
//       The pump stops as soon as the last *main* window (Window) closes,
//       even if modeless dialogs are still open. This mirrors WPF's default
//       and matches the "close the app, dismiss the tool windows" expectation.
//
//   OnLastWindowClose
//       Every attached window — including modeless dialogs — must close before
//       the pump stops.
//
//   OnExplicitShutdown
//       The pump never stops due to window count; only an explicit Shutdown()
//       or WM_QUIT terminates it. Useful for long-lived tray apps.
//
// The distinction between main and dialog windows is made at Create() time
// via NativeWindowHost::IsMainWindow(). Window returns true; DialogWindow returns
// false. Neither class changes AttachWindow's signature — the virtual call
// happens inside AttachWindow.
//
// DECLARE Application FIRST IN wWinMain. It owns this thread's COM apartment
// (see com_ below), so it must outlive every object that holds a COM interface —
// which is every window, every element tree, and anything holding an
// ID2D1Bitmap / IDWriteTextLayout / IFileDialog. Because locals are destroyed in
// reverse declaration order, "declared first" is exactly "destroyed last":
//
//     int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
//         Application app(hInst);        // COM up
//         if (!app.ComReady()) return 1;
//         ConfigStore cfg;               // ...everything else after
//         MainWindow win(&cfg);
//         return app.Run();
//     }                                  // win, cfg, then app -> COM down last
//
// Moving any of those declarations above `app` reintroduces a 0xC0000005 on exit:
// the window's element tree would Release() its COM interfaces into an apartment
// that had already been torn down.
#pragma once

#include "Api.h"
#include "WindowRegistry.h"
#include "fl_common.h"
#include "window/FrameWaiter.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace fluent {

class NativeWindowHost;
class DialogWindow;

// ShutdownMode is declared in WindowRegistry.h, alongside the logic that acts on
// it — the stop decision and the enum belong together, and putting the enum
// there is what lets that decision be unit-tested without an Application.

class FLUENTUI_API Application final {
public:
    explicit Application(HINSTANCE instance);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    HINSTANCE Instance() const { return instance_; }
    DWORD ThreadId() const { return threadId_; }
    bool IsUiThread() const { return GetCurrentThreadId() == threadId_; }
    bool IsRunning() const { return running_; }
    size_t WindowCount() const { return windows_.Count(); }
    // Number of registered PRIMARY windows (dialogs excluded). This is what
    // ShutdownMode::OnMainWindowClose counts down to zero.
    size_t MainWindowCount() const { return windows_.MainCount(); }

    // Run the frame-aware message loop until the shutdown condition is reached
    // (see ShutdownMode) or Shutdown() is called.
    //
    // TryRun / ExitCode split the two things a combined `int Run()` would
    // conflate: how the loop TERMINATED and what the PROCESS should report.
    //
    // TryRun returns:
    //   S_OK                     the loop ran and terminated normally
    //   E_ILLEGAL_METHOD_CALL    called off the UI thread, or re-entrantly
    // In both failure cases the loop does not run and ExitCode() is unchanged.
    HRESULT TryRun();

    // The exit code to hand back to the OS: whatever was supplied to Shutdown()
    // or carried by WM_QUIT, and 0 if the loop stopped because the shutdown
    // condition was met by window closures. Valid after TryRun/Run returns.
    int ExitCode() const { return exitCode_; }

    // Convenience wrapper for `return app.Run();` in wWinMain. Equivalent to
    // TryRun() followed by ExitCode(), returning 0 if TryRun failed. Prefer
    // TryRun when you want to detect the misuse.
    int Run();

    // Ask the pump to stop. Safe from any thread. Posts to the message-only
    // window so the request reaches the WndProc even inside a system modal loop
    // (drag/resize) or the legacy GetMessage-based dialog pump.
    void Shutdown(int exitCode = 0);

    // Post work to the UI thread. Safe from any thread. fn() is invoked inside
    // the next PumpOneTurn turn, on the UI thread, in the order posts arrived.
    // Use this to marshal results from background threads without raw Win32
    // synchronization in the application layer.
    void Post(std::function<void()> fn);

    ShutdownMode GetShutdownMode() const { return shutdownMode_; }
    void SetShutdownMode(ShutdownMode mode) { shutdownMode_ = mode; }

    // True if COM initialization succeeded. The application must check this after
    // constructing Application and exit early if false. WIC (Image control) and
    // IFileDialog require a COM apartment; the framework cannot function without it.
    bool ComReady() const;

private:
    friend class NativeWindowHost;
    friend class DialogWindow;

    using Entry = WindowRegistry::Entry;

    bool AttachWindow(NativeWindowHost& window);
    void DetachWindow(NativeWindowHost& window);
    bool Contains(const Entry& entry) const { return windows_.Contains(entry); }
    bool WindowCountStop() const;
    void RunModalUntilClosed(DialogWindow& dialog);
    bool PumpOneTurn(bool stopWhenNoWindows);

    static LRESULT CALLBACK AppMsgWndProc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp);

    // COM apartment for this UI thread. MUST be the first declared member so it
    // is destroyed LAST — after every window, element tree, and COM interface
    // acquired through the framework has been released.
    //
    // This lives in Application, not the app's wWinMain, for the same reason the
    // message-only window does: it is thread-level infrastructure the framework
    // requires (WIC for Image, IFileDialog for FileDialog), not an application
    // choice to make. An app that forgot CoInitializeEx got a silently blank
    // Image; an app that called CoUninitialize in the wrong order got 0xC0000005
    // on exit. Neither is the application's problem to solve.
    //
    // The apartment is COINIT_APARTMENTTHREADED (STA) to match the UI-thread
    // model and avoid cross-thread marshaling overhead.
    struct ComApartment {
        HRESULT hr;
        ComApartment();
        ~ComApartment();
        ComApartment(const ComApartment&) = delete;
        ComApartment& operator=(const ComApartment&) = delete;
    };
    ComApartment com_;   // must be the first declared member

    HINSTANCE instance_ = nullptr;
    DWORD threadId_ = 0;
    // message-only window for Shutdown() and Post() (B1 / B5).
    HWND msgHwnd_ = nullptr;

    WindowRegistry windows_;
    // The loop's single wait primitive. Owned here rather than per-window because
    // one pump serves every window and paces on the soonest deadline among them.
    FrameWaiter waiter_;
    bool running_ = false;
    bool shutdownRequested_ = false;
    int exitCode_ = 0;
    ShutdownMode shutdownMode_ = ShutdownMode::OnMainWindowClose;

    // Work queue for Post(). Protected by workMutex_; drained on the UI thread
    // when the message-only window receives WM_APP_DISPATCH.
    std::mutex workMutex_;
    std::vector<std::function<void()>> workQueue_;
};

} // namespace fluent
