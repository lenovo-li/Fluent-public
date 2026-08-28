#include "Window.h"
#include "../diagnostics/LayoutCostProbe.h"

namespace fluent {
namespace {

// Gate for the per-phase layout trace, read once from the environment.
//
// Off by default and env-gated rather than compiled in, for the reason stated in
// project documentation's frame-pipeline notes: a trace on the layout path is itself a cost
// (OutputDebugStringA serializes on a system-wide lock and a debugger makes it far
// worse), so a diagnostic that is always on distorts exactly the numbers it reports.
//
// Set FLUENTUI_TRACE_LAYOUT=1 to enable. In Visual Studio that goes in the project's
// Debugging → Environment (FluentUIDemo.vcxproj.user already carries it); a `set` in
// a separate shell does not reach an F5 launch.
bool LayoutPhaseTraceEnabled() {
    static const bool enabled = [] {
        char buf[8]{};
        return GetEnvironmentVariableA("FLUENTUI_TRACE_LAYOUT", buf, sizeof(buf)) > 0 &&
               buf[0] == '1';
    }();
    return enabled;
}

}  // namespace

void Window::SetTitle(std::wstring title) {
    title_ = std::move(title);
    if (IsOpen()) UpdateNativeTitle(title_);
}

void Window::SetStandardTitleBar(bool enabled) {
    if (standardTitleBar_ == enabled) return;
    standardTitleBar_ = enabled;
    if (IsOpen()) {
        OnLayout();
        Render();
    }
}

void Window::SetTitleBarHeight(float heightDip) {
    const float value = heightDip > 0.0f ? heightDip : 0.0f;
    if (titleBarHeightDip_ == value) return;
    titleBarHeightDip_ = value;
    if (IsOpen()) {
        OnLayout();
        Render();
    }
}

HRESULT Window::PrepareContent(Application* app) {
    // Initialization is once per object: NativeWindowHost instances are not reusable
    // after their HWND is destroyed, so a second Show would build a second tree
    // onto a dead window.
    if (initialized_ || IsOpen()) return E_INVALIDARG;
    SetOwningApplication(app);  // Set BEFORE OnInitialize, so OwningApplication() works
    OnInitialize();
    initialized_ = true;
    // A window with no root would create successfully and then paint nothing,
    // which is a confusing failure to debug. Fail loudly at the source instead.
    return root_ ? S_OK : E_UNEXPECTED;
}

HRESULT Window::Show(Application& application, const WindowState* restore) {
    HRESULT hr = PrepareContent(&application);
    if (FAILED(hr)) return hr;
    hr = Create(application, title_.c_str(), widthDip_, heightDip_, restore);
    if (FAILED(hr)) return hr;
    RaiseLoaded();
    return S_OK;
}

bool Window::Close() {
    if (!IsOpen() || closing_) return false;
    if (!RaiseClosing()) return false;
    closing_ = true;
    NativeWindowHost::Close();
    return true;
}

void Window::RaiseLoaded() {
    if (loadedRaised_ || !IsOpen()) return;
    loadedRaised_ = true;
    WindowEventArgs args;
    OnLoaded(args);
    loaded_.Raise(*this, args);
}

bool Window::RaiseClosing() {
    WindowClosingArgs args;
    OnClosing(args);
    closingEvent_.Raise(*this, args);
    return !args.cancel;
}

void Window::RaiseUnloaded() {
    if (unloadedRaised_) return;
    unloadedRaised_ = true;
    WindowEventArgs args;
    OnUnloaded(args);
    unloaded_.Raise(*this, args);
}

void Window::RaiseClosed() {
    if (closedRaised_) return;
    closedRaised_ = true;
    WindowEventArgs args;
    OnClosed(args);
    closed_.Raise(*this, args);
}

void Window::OnCreated() {
    // Register the root first so the service context reaches it, then let the
    // subclass add anything that lives outside the root tree. Order matters: a
    // subclass may want to reference already-attached root children.
    AddElement(root_.get());
    OnContentCreated();
}

void Window::OnLayout() {
    if (!root_) return;
    // Per-pass, not per-session: the probe accumulates, so without this every
    // reported total is the sum over every resize frame since launch.
    LayoutCostProbe::Reset();
    const float top = standardTitleBar_ ? titleBarHeightDip_ : 0.0f;
    const float height = std::max(0.0f, ClientHeightDip() - top);
    const float width = ClientWidthDip();

    // Synchronous, and deliberately so. This ran on a background worker for several
    // commits (AsyncLayoutWorker, now deleted); measurement on the Gallery is what
    // ended that. Per-phase traces of an Off-mode resize drag showed:
    //
    //   spawn=0.8ms   creating the single-shot worker thread, every WM_SIZE
    //   wait=7-17ms   the UI thread BLOCKED on the worker for the whole measure
    //   publish=0.1ms walking the tree to copy async fields into the real ones
    //   arrange=4-12ms UI thread, which async measure cannot touch by construction
    //
    // The measure cost never went anywhere — it was accounted to a different thread
    // while the UI thread waited for exactly as long, plus the spawn. And with
    // arrange at 4-12ms, even a perfect non-blocking measure could not have made the
    // drag smooth. See docs/design/async-layout-postmortem.md.
    //
    // The real fix for a slow resize: skip the work instead of moving it.
    // UIContext::inModalResize (set by NativeWindowHost on WM_ENTERSIZEMOVE) defers
    // DComp surface rasterization until WM_EXITSIZEMOVE, which took resize layout
    // from 7-13ms to 2-3ms — geometry updates every frame, pixels converge once.
    const int64_t tMeasureStart = QpcNow();
    root_->Measure(width, height);
    const int64_t tMeasured = QpcNow();
    root_->Arrange({0.0f, top, width, height});
    const int64_t tArranged = QpcNow();

    // Publish the split unconditionally. It is two QPC reads and two stores on a
    // path that already costs milliseconds, and the numbers are what let the resize
    // trace (NativeWindowHost::ApplyPendingResize) name the expensive half without a
    // second trace line to correlate by hand. Only the TRACE below is gated — the
    // measurement is not, because a diagnostic that requires an environment variable
    // to be set correctly is a diagnostic that is missing exactly when it is needed.
    FrameStats& stats = MutableFrameStats();
    stats.measureMs = QpcToMs(tMeasured - tMeasureStart);
    stats.arrangeMs = QpcToMs(tArranged - tMeasured);

    if (LayoutPhaseTraceEnabled()) {
        FL_TRACEF("Layout", "phases: measure=%.3f arrange=%.3f total=%.3fms",
                  stats.measureMs, stats.arrangeMs,
                  QpcToMs(tArranged - tMeasureStart));
        LayoutCostProbe::Report("  ");
    }
}

void Window::OnDestroying() {
    // Runs while the tree is still attached and the HWND is still valid, so a
    // handler can still read control state (that is the whole point of Unloaded
    // being distinct from Closed).
    RaiseUnloaded();
}

void Window::OnDestroyed() {
    // Runs after roots have detached and hwnd_ has been cleared.
    RaiseClosed();
}

bool Window::OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    UNREFERENCED_PARAMETER(wp);
    UNREFERENCED_PARAMETER(lp);
    if (msg == WM_CLOSE) {
        // Route the system close button through the cancelable path rather than
        // letting DefWindowProc destroy the window behind the app's back.
        Close();
        result = 0;
        return true;
    }
    return false;
}

} // namespace fluent
