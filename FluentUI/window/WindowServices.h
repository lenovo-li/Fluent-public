// WindowServices.h — narrow interface the controls depend on instead of the
// concrete NativeWindowHost.
//
// Phase 3 of the refactor decouples controls from the full window class. A
// control that needs a floating layer (ComboBox dropdown, MenuBar / MenuFlyout)
// or the shared device stack asks for these capabilities through this interface,
// not by including NativeWindowHost.h. NativeWindowHost implements it; a test double
// or a future alternative host can implement the same small surface.
//
// This is not a cross-platform abstraction — it only narrows the compile-time
// and lifetime coupling between a control and its host window (roadmap §5.4).
#pragma once

#include "../fl_common.h"
#include "../core/Subscription.h"
#include <functional>
#include <string>

namespace fluent {

class D2DContext;
class DWriteContext;
class ICompositionBackend;

// Why the host asked an active popup to dismiss. Free (namespace-level) so a
// control's callback signature does not have to name a concrete window class.
// The window raises Move/Resize/Deactivate itself; Click carries the screen-
// pixel point of the mouse-down so a popup can test click-outside.
enum class PopupDismissReason { Click, Deactivate, Move, Resize };

// The capabilities a control needs from its host window. Kept intentionally
// small: device access for a popup's own composition target, the compositor backend
// (so even an embedded member like a TreeView's scroll model can animate off the UI
// thread), the owner HWND / HINSTANCE / DPI for positioning, and the two active-popup
// registries the window routes deactivation and keyboard input through.
class WindowServices {
public:
    virtual HINSTANCE Instance() const = 0;
    virtual HWND Hwnd() const = 0;
    virtual float DpiScale() const = 0;
    virtual D2DContext& D2D() = 0;
    virtual DWriteContext& DWrite() = 0;

    // Composition layer (Plan B): a control that runs an animation on the
    // DirectComposition compositor thread (e.g. an indeterminate ProgressBar's
    // sweeping segment, a scroll container's overscan translation) reaches the
    // compositor through this backend — create visuals, parent them above the
    // window content, request a commit — without naming any DComp type itself.
    // Returns null on a host without composition or mid device-loss; a control
    // MUST null-check and fall back to its UI-thread path. The backend is owned by
    // the host and outlives the attached tree; a control holds it only while
    // attached (via Window()->Composition()), never past its own lifetime.
    virtual ICompositionBackend* Composition() = 0;

    // Record a diagnostic event into the host's session log, if one is active
    // (diagnostics/SessionLog.h). Default no-op so a control can trace geometry /
    // state changes into the on-close report without every host or test double
    // having to implement logging. `category` is a short tag; `detail` is free text.
    virtual void LogSessionEvent(const char* category, const std::string& detail) {
        UNREFERENCED_PARAMETER(category);
        UNREFERENCED_PARAMETER(detail);
    }

    // Install the callback and return a Subscription that clears *this specific*
    // registration when destroyed. The host stamps each slot with a generation,
    // so a stale Subscription (whose slot was later overwritten by another popup)
    // is a no-op on destruction — it never clears the newer owner's callback. A
    // control stores the Subscription as a member, so its destructor unregisters
    // automatically (no dangling `[this]`).
    virtual Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND otherHwnd, int screenX,
                           int screenY)> cb) = 0;
    virtual Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)> cb) = 0;

protected:
    // Non-virtual protected dtor: the host owns its own lifetime; controls hold
    // a non-owning WindowServices* and never delete through it.
    ~WindowServices() = default;
};

} // namespace fluent
