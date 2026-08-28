#include "DialogWindow.h"
#include "../Application.h"
#include <algorithm>

namespace fluent {

namespace {
// Dialog content inset. These are window-level chrome padding, not control
// spacing, which is why they are plain constants rather than theme tokens: the
// theme's spacing tokens describe gaps *between controls*, and borrowing one here
// would couple dialog chrome to a token that exists for a different purpose. If
// dialog chrome ever becomes themeable these move into ThemeTokens together.
constexpr float kDialogPadDip = 16.0f;      // left/right inset
constexpr float kDialogTopGapDip = 8.0f;    // gap below the title bar
constexpr float kDialogBottomPadDip = 24.0f;
} // namespace

HRESULT DialogWindow::Start(NativeWindowHost& owner) {
    if (IsOpen() || IsInitialized() || !owner.IsOpen()) return E_INVALIDARG;
    ownerWindow_ = &owner;
    owner_ = owner.Hwnd();
    result_ = DialogResult::None;

    // Dialogs inherit the owner's complete theme inputs, not merely the current
    // dark bit. This preserves custom accent and high-contrast policy too.
    SetThemeConfiguration(owner.ThemeConfiguration());
    ownerThemeSub_ = owner.ThemeChanged().Subscribe(
        this, [](void* target, NativeWindowHost& source, ThemeChangedArgs&) {
            static_cast<DialogWindow*>(target)->SetThemeConfiguration(
                source.ThemeConfiguration());
        });

    // Initialization belongs to the dialog class and happens before native
    // creation, matching InitializeComponent/WM_INITDIALOG semantics. PrepareContent
    // enforces the once-per-object rule and the "must have a root" contract.
    //
    // Pass the owner's Application so OwningApplication() works in OnInitialize.
    // Fallback to nullptr for owners that never adopted Application (PrepareContent
    // accepts nullptr and leaves application_ null in that case).
    Application* app = owner.OwningApplication();
    HRESULT hr = PrepareContent(app);
    if (FAILED(hr)) return hr;

    // Inherit the owner's Application so the dialog joins the same frame loop and
    // its modal pump can keep the owner painting. Falls back to the raw HINSTANCE
    // path for hosts that never adopted Application.
    if (app)
        return Create(*app, Title().c_str(), ClientWidth(), ClientHeight());
    return Create(owner.Instance(), Title().c_str(), ClientWidth(), ClientHeight());
}

HRESULT DialogWindow::Show(NativeWindowHost& owner) {
    HRESULT hr = Start(owner);
    if (FAILED(hr)) return hr;
    ShowWindow(Hwnd(), SW_SHOWNORMAL);
    SetForegroundWindow(Hwnd());
    RaiseLoaded();
    return S_OK;
}

DialogResult DialogWindow::ShowDialog(NativeWindowHost& owner) {
    HRESULT hr = Start(owner);
    if (FAILED(hr)) return DialogResult::None;

    ownerWasEnabled_ = IsWindowEnabled(owner.Hwnd()) != FALSE;
    EnableWindow(owner.Hwnd(), FALSE);
    ShowWindow(Hwnd(), SW_SHOWNORMAL);
    SetForegroundWindow(Hwnd());
    RaiseLoaded();

    if (Application* app = OwningApplication()) {
        // Application-driven: its pump services every attached window, so the owner
        // keeps repainting behind the modal dialog.
        app->RunModalUntilClosed(*this);
    } else {
        // Legacy path for hosts without an Application. Each window drives itself
        // via the self-frame timer (UsesExternalFramePump() == false).
        MSG msg;
        while (IsOpen()) {
            const int state = GetMessageW(&msg, nullptr, 0, 0);
            if (state <= 0) {
                if (state == 0) PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    RestoreOwner();
    return result_;
}

void DialogWindow::RestoreOwner() {
    if (ownerWindow_ && ownerWasEnabled_ && ownerWindow_->Hwnd()) {
        EnableWindow(ownerWindow_->Hwnd(), TRUE);
        SetForegroundWindow(ownerWindow_->Hwnd());
    }
    ownerWasEnabled_ = false;
}

bool DialogWindow::Close(DialogResult result) {
    if (!IsOpen() || IsClosing()) return false;
    // Stage the result so RaiseClosing (which runs inside Window::Close) can put it
    // into the dialog args without threading a parameter through the base class.
    pendingResult_ = result;
    return Window::Close();
}

bool DialogWindow::RaiseClosing() {
    // Window channel first, so a handler holding a Window& sees the close and can
    // veto it exactly as it would for any window.
    if (!Window::RaiseClosing()) return false;

    DialogClosingArgs args{pendingResult_, false};
    OnDialogClosing(args);
    dialogClosing_.Raise(*this, args);
    if (args.cancel) return false;

    // Only commit the result once the close is definitely going through — a vetoed
    // close must leave Result() untouched.
    result_ = args.result;
    return true;
}

void DialogWindow::RaiseUnloaded() {
    // Re-enable the owner before the tree detaches. Doing it here rather than after
    // the modal loop returns means a dialog destroyed by any route (owner closing,
    // WM_CLOSE, app shutdown) still hands focus back.
    RestoreOwner();
    Window::RaiseUnloaded();
}

void DialogWindow::RaiseClosed() {
    Window::RaiseClosed();
    // A dialog dismissed without an explicit result (owner destroyed it, or the
    // system closed it) reads as Cancel rather than the ambiguous None.
    if (result_ == DialogResult::None) result_ = DialogResult::Cancel;
    DialogClosedArgs args{result_};
    OnDialogClosed(args);
    dialogClosed_.Raise(*this, args);
}

// Deliberately synchronous, unlike Window::OnLayout which measures on a worker.
// A dialog's tree is small (a message, a few buttons) and its layout cost is far
// below the ~7ms that motivated async layout, so handing it to a worker would add
// thread-handoff latency and a timeout path to guard, for no measurable win. It
// also never participates in a modal resize drag — the case async layout exists
// for. If a dialog ever grows a large scrolling document, revisit this.
void DialogWindow::OnLayout() {
    UIElement* root = Root();
    if (!root) return;
    const float w = ClientWidthDip();
    const float h = ClientHeightDip();
    const float contentW = std::max(0.0f, w - kDialogPadDip * 2.0f);
    const float titleBar = TitleBarHeightDip();
    root->Measure(contentW, std::max(0.0f, h - titleBar - kDialogPadDip * 2.0f));
    root->Arrange({kDialogPadDip, titleBar + kDialogTopGapDip, contentW,
                   std::max(0.0f, h - titleBar - kDialogBottomPadDip)});
}

bool DialogWindow::OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    UNREFERENCED_PARAMETER(lp);
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        Close(DialogResult::Cancel);
        result = 0;
        return true;
    }
    if (msg == WM_CLOSE) {
        // Deliberately not delegating to Window::OnAppMessage: its WM_CLOSE path
        // calls Window::Close(), which would bypass the result staging.
        Close(DialogResult::Cancel);
        result = 0;
        return true;
    }
    return false;
}

} // namespace fluent
