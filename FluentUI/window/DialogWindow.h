// DialogWindow.h — a Window that additionally has an owner, a result, and modality.
//
// A dialog is a real window class, not a temporary content factory. Derived
// classes configure their title/size in the constructor and build their retained
// control tree in OnInitialize. Calling code then mirrors WPF/Win32 usage:
//
//     SettingsDialog dialog;
//     DialogResult result = dialog.ShowDialog(owner);
//
// Lifecycle is deterministic and inherited unchanged from Window:
//   OnInitialize (once) -> Loaded -> Closing (cancelable) -> Unloaded -> Closed.
// Unloaded runs while the tree is still attached; Closed runs after the HWND and
// attached context are gone.
//
// WHY THIS DERIVES FROM Window. Everything above — the authoring model and the
// lifecycle order — is identical to a plain application window. Only three things
// are genuinely dialog-specific: an owner HWND (to disable/restore and to stay on
// top of), a DialogResult, and a modal pump. So those three are all this class
// adds. Before this refactor Window and DialogWindow were siblings that each
// carried their own copy of root/title/size/initialized/closing state, and they
// had already drifted: two different Close() semantics, and four host overrides
// locked with `final` that made the dialog base unusable as a main window (which
// is why the demo's main window still inherited the raw host class). One authoring
// base with a thin dialog specialization is what prevents a repeat.
//
// TWO EVENT CHANNELS, ON PURPOSE. Loaded/Unloaded are inherited as-is: a dialog's
// "it opened" and "it is going away" carry no extra information. Closing/Closed
// exist twice — the inherited Window channel (WindowClosingArgs, so code holding a
// Window& still works polymorphically) AND a dialog channel (DialogClosing/
// DialogClosed, carrying the DialogResult). RaiseClosing fires both and honours a
// veto from either. The asymmetric naming is honest: the result-carrying channel is
// genuinely additional, not a replacement, and hiding a base event behind the same
// name with a different args type would silently break Window& subscribers.
#pragma once

#include "Window.h"
#include "../base/Event.h"
#include <memory>
#include <string>

namespace fluent {

enum class DialogResult { None, Primary, Secondary, Cancel, Custom };

struct DialogClosingArgs {
    DialogResult result = DialogResult::Cancel;
    bool cancel = false;
};

struct DialogClosedArgs {
    DialogResult result = DialogResult::None;
};

class FLUENTUI_API DialogWindow : public Window {
public:
    DialogWindow() {
        // Dialogs keep the owner's tokens but use the stronger tabbed-window
        // material so a same-theme child remains visually distinct from its
        // owner. On systems without MicaAlt this follows the normal solid fallback.
        SetBackdropKind(BackdropKind::MicaAlt);
        SetCornerRadius(12.0f);
        SetShadowEnabled(true);
        SetTitle(L"Fluent Dialog");
        SetClientSize(420.0f, 260.0f);
    }
    ~DialogWindow() override = default;

    // Modeless Show and modal ShowDialog follow the familiar WPF naming. Window
    // title, size and content belong to the dialog object, not to each call site.
    HRESULT Show(NativeWindowHost& owner);
    DialogResult ShowDialog(NativeWindowHost& owner);

    // Returns false when a Closing handler vetoes the close. Hides Window::Close()
    // deliberately — a dialog close always carries a result.
    bool Close(DialogResult result = DialogResult::Cancel);
    bool IsDialogOpen() const { return IsOpen(); }
    DialogResult Result() const { return result_; }
    HWND Owner() const { return owner_; }

    // The result-carrying channel. See "TWO EVENT CHANNELS" above.
    Event<DialogWindow, DialogClosingArgs>& DialogClosing() { return dialogClosing_; }
    Event<DialogWindow, DialogClosedArgs>& DialogClosed() { return dialogClosed_; }

protected:
    // Result-carrying counterparts to Window's OnClosing/OnClosed. Both fire: the
    // Window hook first, then these.
    virtual void OnDialogClosing(DialogClosingArgs&) {}
    virtual void OnDialogClosed(DialogClosedArgs&) {}

    // Dialog flavours of the base lifecycle seam: same order, plus the result
    // channel and owner re-enabling on the way out.
    bool RaiseClosing() override;
    void RaiseUnloaded() override;
    void RaiseClosed() override;

    void OnLayout() override;
    bool OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

    // Host-layer overrides that make this a secondary window rather than the
    // application's main one. Deliberately NOT final: a subclass may legitimately
    // need a different show command or pump strategy, and locking these is exactly
    // what made the previous DialogWindow unusable as a general base.
    bool ShouldPostQuitOnDestroy() const override { return false; }
    bool UsesExternalFramePump() const override { return false; }
    // Dialogs are secondary windows: they do not participate in the
    // ShutdownMode::OnMainWindowClose count. Only Window (and raw
    // NativeWindowHost) subclasses are counted as primary.
    bool IsMainWindow() const override { return false; }
    HWND OwnerHwnd() const override { return owner_; }
    int InitialShowCommand() const override { return SW_HIDE; }

    // Force dialogs to appear in the taskbar even though they have an owner.
    // Without WS_EX_APPWINDOW, owned windows have no taskbar button and minimize
    // to the screen corner (Win32 default). Modern desktop apps (VSCode, Chrome,
    // Office) give dialogs their own taskbar buttons so users can find and restore
    // them independently.
    DWORD ExtraExStyle() const override { return WS_EX_APPWINDOW; }

    NativeWindowHost* ownerWindow_ = nullptr;

private:
    HRESULT Start(NativeWindowHost& owner);
    void RestoreOwner();

    HWND owner_ = nullptr;
    bool ownerWasEnabled_ = false;
    DialogResult result_ = DialogResult::None;
    // Staged by Close(DialogResult) so RaiseClosing can put it into the args. Kept
    // separate from result_ because a vetoed close must not mutate the result.
    DialogResult pendingResult_ = DialogResult::Cancel;
    Event<DialogWindow, DialogClosingArgs> dialogClosing_;
    Event<DialogWindow, DialogClosedArgs> dialogClosed_;
    Subscription ownerThemeSub_;
};

} // namespace fluent
