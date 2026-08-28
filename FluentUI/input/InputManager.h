// InputManager.h — hit-testing, event routing, and pointer capture (roadmap §9).
//
// The window's job shrinks to translating raw Win32 messages into DIP points +
// modifier bits and feeding them here (PointerMoved / PointerPressed / ...); this
// class decides everything else, so the host contains no control-type knowledge.
//
// Routing (§9.2): a pointer event hit-tests to the deepest element, builds the
// target..root chain, tunnels Preview root->target, then bubbles the main phase
// target->root; a handler setting args.handled stops the remaining route. Key
// events route the same way over the focused element's chain.
//
// Pointer capture (§9.3): a control (e.g. Button on press, Slider on drag) grabs
// the pointer so subsequent moves/releases go only to it regardless of position;
// capture is released on up, explicit release, window-deactivate, or the captured
// element detaching — every path also delivers a canceling release so the control
// leaves its pressed state cleanly.
//
// LIFETIME CONTRACT (read before wiring a new host):
//   The InputManager, the root element list it walks, and the invalidate target
//   MUST outlive any single event it dispatches. A PointerXxx/KeyDown call routes
//   into arbitrary control handlers and then touches its own members again (e.g.
//   the trailing RequestInvalidate — needed because some input changes visible
//   state without the element self-invalidating, such as a scrollbar rail's hover
//   fade). So a handler MUST NOT synchronously destroy the InputManager (nor its
//   host, nor the invalidate target) from inside a dispatched event.
//
//   The one place this bites: a popup whose own content closes it (a menu item's
//   onInvoke). MenuFlyout must NOT free a PopupHost (which owns an InputManager)
//   while that popup's WndProc is on the stack — it defers destruction instead
//   (MenuFlyout::retired_). A host that can be torn down by its own content must
//   defer that teardown the same way, or hide-not-destroy like ComboBox.
#pragma once

#include "InputTypes.h"
#include "RoutedEvent.h"
#include <vector>

// HWND without dragging <windows.h> into every translation unit that routes input.
// This is the same opaque-handle forward declaration the Windows SDK itself uses
// (HWND is a pointer to an undefined struct), so it stays compatible if a caller
// includes <windows.h> first.
struct HWND__;
using HWND = HWND__*;

namespace fluent {

class UIElement;
class FocusManager;

class InputManager {
public:
    // Borrow the host's root list (same vector it renders / the FocusManager uses)
    // and the focus authority. Neither owned; both must outlive the manager.
    void SetRoots(const std::vector<UIElement*>* roots) { roots_ = roots; }
    void SetFocusManager(FocusManager* fm) { focus_ = fm; }

    // The host window, needed to take OS-level mouse capture. Element capture alone
    // is not enough: once the cursor leaves the window, Win32 delivers WM_LBUTTONUP
    // to whatever window is under it, so without ::SetCapture our WndProc never sees
    // the release and the pressed element stays stuck in its Pressed state. Optional
    // — headless tests leave it null and element capture still routes normally.
    void SetHwnd(HWND hwnd) { hwnd_ = hwnd; }

    // Request-a-repaint hook, invoked after routing an event (input usually
    // changes visual state). Optional; null = no repaint request.
    void SetInvalidate(void (*cb)(void*), void* ctx) { invalidateCb_ = cb; invalidateCtx_ = ctx; }

    // --- Translated events fed by the window (positions in window DIPs) ----
    void PointerMoved(Point p, ModifierKeys mods);
    // `clickCount` is 1/2/3 (see ClickCounter.h). Defaulted so existing callers and
    // tests that only care about a plain press need no change.
    void PointerPressed(Point p, PointerButton button, ModifierKeys mods,
                        int clickCount = 1);
    void PointerReleased(Point p, PointerButton button, ModifierKeys mods);
    void PointerWheel(Point p, int delta, ModifierKeys mods);
    // The pointer left the window entirely: clear hover (like a WM_MOUSELEAVE).
    void PointerLeftWindow();
    // Route a key/char to the focused element's chain. KeyDown returns true if the
    // route (or built-in Tab navigation) consumed it, so the host can fall through
    // to DefWindowProc otherwise.
    bool KeyDown(unsigned vk, ModifierKeys mods);
    void TextInput(wchar_t ch);

    // --- Pointer capture ---------------------------------------------------
    void CapturePointer(UIElement* e);
    void ReleaseCapture(UIElement* e);   // no-op unless `e` currently holds capture
    UIElement* Captured() const { return captured_; }

    // The window lost activation: release capture (with a canceling release) and
    // clear hover, so a control never stays pressed/hovered after focus leaves.
    void OnWindowDeactivated();

    // WM_CAPTURECHANGED: the OS handed mouse capture to someone else (another
    // window, or our own caption-button path calling ::SetCapture). Deliver the
    // same canceling release as deactivation so the element leaves Pressed, but
    // do NOT call ::ReleaseCapture — we no longer hold it, and releasing would
    // yank it out from under whoever does.
    void OnCaptureStolen();
    // An element left the tree: clear it from hot/captured and notify focus.
    void OnElementDetached(UIElement* e);

    // Deepest interactive element (or null) at a window-DIP point. Topmost-first
    // across roots, then HitTestDeep within. (The old NativeWindowHost::ElementAt.)
    UIElement* HitTest(Point p) const;

    // Right-click target resolution (WP-03 follow-up): hit-test the deepest
    // element at `p`, then walk the Parent() chain and return the first element
    // that owns a context menu (so a child with none inherits its container's).
    // Null if nothing under the point has a menu anywhere up its chain.
    UIElement* ContextMenuOwnerAt(Point p) const;

    UIElement* Hot() const { return hot_; }

private:
    // Build the route from `target` up through Parent() to the root (index 0 =
    // target, last = outermost). Empty if target is null.
    void BuildRoute(UIElement* target, std::vector<UIElement*>& out) const;

    // Tunnel Preview (root->target) then bubble main (target->root), invoking the
    // matching member function on each element; stops when args.handled is set.
    // The two function pointers are the preview and main routed virtuals.
    void RoutePointer(const std::vector<UIElement*>& route, PointerEventArgs& args,
                      void (UIElement::*preview)(PointerEventArgs&),
                      void (UIElement::*main)(PointerEventArgs&));

    void RequestInvalidate() { if (invalidateCb_) invalidateCb_(invalidateCtx_); }

    // Update hover (hot_) for a move to point `p` and fire enter/leave.
    void UpdateHot(Point p, ModifierKeys mods);

    const std::vector<UIElement*>* roots_ = nullptr;
    FocusManager* focus_ = nullptr;

    UIElement* hot_ = nullptr;       // element under the pointer (hover)
    UIElement* captured_ = nullptr;  // element receiving all pointer input

    HWND hwnd_ = nullptr;            // host window for OS-level mouse capture

    void (*invalidateCb_)(void*) = nullptr;
    void* invalidateCtx_ = nullptr;
};

}  // namespace fluent
