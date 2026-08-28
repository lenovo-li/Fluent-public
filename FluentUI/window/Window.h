// Window.h — the primary code-first top-level window, and the ONE authoring base.
//
// This is what applications derive from. It owns the authoring model that used to
// exist only on DialogWindow: SetTitle / SetClientSize / SetRoot / OnInitialize
// plus the Loaded -> Closing -> Unloaded -> Closed lifecycle. DialogWindow now
// derives from *this* class and adds only the three things a dialog genuinely
// needs on top of a window: an owner, a DialogResult, and modality.
//
// WHY THE HIERARCHY IS SHAPED THIS WAY. NativeWindowHost is the native host layer:
// HWND and WndProc, per-monitor DPI, the custom title bar, popup and tooltip
// hosting, plus five composed subsystem hosts — GraphicsHost (D2D/DWrite/DComp
// devices + resource cache), InputHost (routing, focus, click counting),
// FrameHost (scheduler + animation registry), ThemeHost (tokens + native
// appearance), DiagnosticsHost (frame rings + session log). It answers "how does
// a window exist on Windows", not "how does an application describe a window".
//
// **Window's inheritance of NativeWindowHost is an implementation detail, not
// part of the supported API.** Applications derive from Window (or DialogWindow)
// and use only the authoring surface declared in this header. Do not reach past
// it into host internals, and do not dynamic_cast to NativeWindowHost: the
// relationship is free to become composition later. Window itself only consumes
// a handful of host entry points (Create / IsOpen / AddElement / Render /
// ClientWidthDip / ClientHeightDip / UpdateNativeTitle / SetOwningApplication),
// which is what keeps that option open.
//
// Composition was evaluated and deliberately not adopted: with Window and
// DialogWindow as the only two subclasses in the entire codebase and fewer than
// ten host calls between them, hand-writing 30+ forwarding methods would add
// surface area without changing the layering. See
// internal documentation ("Phase 6 取消理由").
//
// LIFECYCLE HOOKS AND WHY SOME ARE final. The three framework-driven overrides
// (OnCreated / OnDestroying / OnDestroyed) are final on purpose: each one carries
// a step that the window cannot skip — registering the root element, and raising
// Unloaded/Closed in the right order relative to element detach. A subclass that
// overrode one and forgot to call the base would get a blank window or a lost
// event, and that failure is silent. Subclasses get purpose-built virtuals
// instead:
//
//   OnInitialize()       build the control tree, before the HWND exists
//   OnContentCreated()   register extra non-tree elements, after graphics are up
//   OnLoaded/OnClosing/OnUnloaded/OnClosed    the app-facing lifecycle
//
// The Raise* virtuals below are the framework-internal seam that lets DialogWindow
// reuse this exact ordering while raising dialog-flavoured event args. Keeping the
// ORDER in one place — and letting subclasses vary only the args — is what stops
// the two window kinds from drifting apart the way they had before this refactor.
#pragma once

#include "NativeWindowHost.h"
#include "../Application.h"
#include <memory>
#include <string>

namespace fluent {

struct WindowEventArgs {};
struct WindowClosingArgs { bool cancel = false; };

class FLUENTUI_API Window : public NativeWindowHost {
public:
    // Create and show the window, attaching it to `application` so it takes part
    // in the app's frame loop and shutdown accounting. `restore` is optional saved
    // placement; the window does not retain the pointer.
    HRESULT Show(Application& application, const WindowState* restore = nullptr);

    // Request a close. Runs the cancelable Closing lifecycle first, so a handler
    // can veto. Returns false if it was vetoed or the window is already closing.
    // Prefer this over NativeWindowHost::Close(), which destroys unconditionally.
    bool Close();

    void SetTitle(std::wstring title);
    const std::wstring& Title() const { return title_; }
    // Standard mode draws SetTitle()/SetIconResource() as window chrome and lays
    // application content below it. Off by default for source compatibility with
    // applications that already author a completely custom title bar.
    void SetStandardTitleBar(bool enabled);
    bool StandardTitleBar() const { return standardTitleBar_; }
    void SetTitleBarHeight(float heightDip);
    float TitleBarHeight() const { return titleBarHeightDip_; }
    void SetClientSize(float widthDip, float heightDip) {
        widthDip_ = widthDip;
        heightDip_ = heightDip;
    }
    float ClientWidth() const { return widthDip_; }
    float ClientHeight() const { return heightDip_; }
    bool IsInitialized() const { return initialized_; }

    Event<Window, WindowEventArgs>& Loaded() { return loaded_; }
    Event<Window, WindowClosingArgs>& Closing() { return closingEvent_; }
    Event<Window, WindowEventArgs>& Unloaded() { return unloaded_; }
    Event<Window, WindowEventArgs>& Closed() { return closed_; }

protected:
    // Called once, immediately before native creation — the counterpart to
    // InitializeComponent. Build the control tree here and hand the root to
    // SetRoot. It runs before graphics exist, so do NOT call AddElement from here.
    virtual void OnInitialize() {}

    // Called right after the root has been registered and the service context is
    // live. This is the place for elements that are not part of the root tree (a
    // MenuBar strip, a standalone MenuFlyout) — AddElement requires graphics to be
    // ready, which is true here and not yet true in OnInitialize.
    virtual void OnContentCreated() {}

    virtual void OnLoaded(WindowEventArgs&) {}
    virtual void OnClosing(WindowClosingArgs&) {}
    virtual void OnUnloaded(WindowEventArgs&) {}
    virtual void OnClosed(WindowEventArgs&) {}

    void SetRoot(std::unique_ptr<UIElement> root) { root_ = std::move(root); }
    UIElement* Root() const { return root_.get(); }

    // Framework-internal lifecycle seam. Window raises WindowEventArgs flavours;
    // DialogWindow overrides these to raise its result-carrying args and to
    // re-enable its owner. The ORDER in which they fire is fixed by this class and
    // is deliberately not overridable.
    //
    // RaiseClosing returns false when a handler vetoed the close.
    virtual bool RaiseClosing();
    virtual void RaiseUnloaded();
    virtual void RaiseClosed();

    // Raise Loaded exactly once, after the window is shown. Idempotent, because
    // both Window::Show and DialogWindow::ShowDialog reach this point by different
    // routes and must not double-raise.
    void RaiseLoaded();

    // Run OnInitialize once and verify a root was supplied. Both Window::Show and
    // DialogWindow::Start funnel through this so the "initialize exactly once,
    // before native creation" rule has a single implementation.
    //
    // `app` is set as the owning Application BEFORE OnInitialize runs, so
    // OwningApplication() is valid during OnInitialize — eliminating the lifecycle
    // trap where hooks that need Application::Post had to be deferred to
    // OnContentCreated. Pass nullptr for the non-Application creation path.
    HRESULT PrepareContent(Application* app);

    // Mark the window as closing without running the Closing lifecycle. Used by
    // DialogWindow, whose Close(DialogResult) overload runs its own veto pass
    // before delegating down.
    void MarkClosing() { closing_ = true; }
    bool IsClosing() const { return closing_; }

    void OnCreated() final;
    void OnLayout() override;
    void OnDestroying() final;
    void OnDestroyed() final;
    bool OnAppMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;
    float TitleBarHeightDip() const override { return titleBarHeightDip_; }
    const wchar_t* TitleText() const override { return title_.c_str(); }
    bool UsesStandardTitleBar() const override { return standardTitleBar_; }

private:
    std::unique_ptr<UIElement> root_;
    std::wstring title_ = L"FluentUI";
    float widthDip_ = 800.0f;
    float heightDip_ = 600.0f;
    bool initialized_ = false;
    bool closing_ = false;
    bool loadedRaised_ = false;
    bool unloadedRaised_ = false;
    bool closedRaised_ = false;
    bool standardTitleBar_ = false;
    float titleBarHeightDip_ = 48.0f;
    Event<Window, WindowEventArgs> loaded_;
    Event<Window, WindowClosingArgs> closingEvent_;
    Event<Window, WindowEventArgs> unloaded_;
    Event<Window, WindowEventArgs> closed_;
};

} // namespace fluent
