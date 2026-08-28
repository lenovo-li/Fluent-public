// InputManager.cpp — see InputManager.h.

#include "InputManager.h"
#include "FocusManager.h"
#include "../core/UIElement.h"

namespace fluent {

UIElement* InputManager::HitTest(Point p) const {
    if (!roots_) return nullptr;
    // Topmost-first: later-registered roots render on top, so test in reverse.
    for (auto it = roots_->rbegin(); it != roots_->rend(); ++it)
        if (*it) {
            if (UIElement* hit = (*it)->HitTestDeep(p.x, p.y)) return hit;
        }
    return nullptr;
}

UIElement* InputManager::ContextMenuOwnerAt(Point p) const {
    // Walk from the deepest hit element up the visual-tree parent chain, returning
    // the first that owns a context menu (a child without one inherits its
    // container's). Same parent walk BuildRoute uses for event bubbling.
    for (UIElement* e = HitTest(p); e; ) {
        if (e->HasContextMenu()) return e;
        Visual* parent = e->Parent();
        e = parent ? static_cast<UIElement*>(parent) : nullptr;
    }
    return nullptr;
}

void InputManager::BuildRoute(UIElement* target, std::vector<UIElement*>& out) const {
    out.clear();
    for (UIElement* e = target; e; ) {
        out.push_back(e);
        // The visual-tree parent of any element in this system is always another
        // UIElement (Panel / Border); Visual has no other concrete subclass.
        Visual* parent = e->Parent();
        e = parent ? static_cast<UIElement*>(parent) : nullptr;
    }
}

void InputManager::RoutePointer(const std::vector<UIElement*>& route,
                                PointerEventArgs& args,
                                void (UIElement::*preview)(PointerEventArgs&),
                                void (UIElement::*main)(PointerEventArgs&)) {
    // Tunnel: root -> target (route is stored target..root, so iterate in reverse).
    if (preview) {
        for (auto it = route.rbegin(); it != route.rend(); ++it) {
            args.source = *it;
            ((*it)->*preview)(args);
            if (args.handled) return;
        }
    }
    // Bubble: target -> root.
    if (main) {
        for (UIElement* e : route) {
            args.source = e;
            (e->*main)(args);
            if (args.handled) return;
        }
    }
}

void InputManager::UpdateHot(Point p, ModifierKeys mods) {
    UNREFERENCED_PARAMETER(mods);
    UIElement* e = HitTest(p);
    if (e != hot_) {
        if (hot_) hot_->OnPointerLeave();
        hot_ = e;
        if (hot_) hot_->OnPointerEnter();
    }
}

void InputManager::PointerMoved(Point p, ModifierKeys mods) {
    PointerEventArgs args;
    args.position = p;
    args.modifiers = mods;

    if (captured_) {
        // Captured: all moves go to the capturing element regardless of position.
        args.source = captured_;
        args.originalSource = captured_;
        captured_->OnPointerMoved(args);
        RequestInvalidate();
        return;
    }

    UpdateHot(p, mods);
    if (hot_) {
        std::vector<UIElement*> route;
        BuildRoute(hot_, route);
        args.originalSource = hot_;
        RoutePointer(route, args, nullptr, &UIElement::OnPointerMoved);
    }
    RequestInvalidate();
}

void InputManager::PointerPressed(Point p, PointerButton button, ModifierKeys mods,
                                  int clickCount) {
    UIElement* target = HitTest(p);

    // Click-to-focus (matches the old window behavior): a focusable hit gets
    // focus; anything else (non-focusable hit or empty space) clears focus.
    if (focus_) {
        if (target && target->IsFocusable()) focus_->SetFocus(target);
        else focus_->ClearFocus();
    }

    if (target) {
        std::vector<UIElement*> route;
        BuildRoute(target, route);
        PointerEventArgs args;
        args.position = p;
        args.button = button;
        args.modifiers = mods;
        args.clickCount = clickCount;
        args.originalSource = target;
        RoutePointer(route, args, &UIElement::OnPreviewPointerPressed,
                     &UIElement::OnPointerPressed);
    }
    RequestInvalidate();
}

void InputManager::PointerReleased(Point p, PointerButton button, ModifierKeys mods) {
    // A captured element receives the release wherever the pointer is; otherwise
    // route to whatever is under the pointer.
    UIElement* target = captured_ ? captured_ : HitTest(p);
    if (target) {
        std::vector<UIElement*> route;
        BuildRoute(target, route);
        PointerEventArgs args;
        args.position = p;
        args.button = button;
        args.modifiers = mods;
        args.originalSource = target;
        RoutePointer(route, args, nullptr, &UIElement::OnPointerReleased);
    }
    RequestInvalidate();
}

void InputManager::PointerWheel(Point p, int delta, ModifierKeys mods) {
    UIElement* target = HitTest(p);
    if (target) {
        std::vector<UIElement*> route;
        BuildRoute(target, route);
        PointerEventArgs args;
        args.position = p;
        args.wheelDelta = delta;
        args.modifiers = mods;
        args.originalSource = target;
        RoutePointer(route, args, nullptr, &UIElement::OnPointerWheelChanged);
    }
    RequestInvalidate();
}

void InputManager::PointerLeftWindow() {
    if (captured_) return;  // keep hover semantics stable during a capture
    if (hot_) { hot_->OnPointerLeave(); hot_ = nullptr; }
    RequestInvalidate();
}

bool InputManager::KeyDown(unsigned vk, ModifierKeys mods) {
    UIElement* f = focus_ ? focus_->Focused() : nullptr;
    if (f) {
        std::vector<UIElement*> route;
        BuildRoute(f, route);
        KeyEventArgs args;
        args.vk = vk;
        args.modifiers = mods;
        args.originalSource = f;
        // Tunnel Preview root->target.
        for (auto it = route.rbegin(); it != route.rend() && !args.handled; ++it) {
            args.source = *it;
            (*it)->OnPreviewKeyDown(args);
        }
        // Bubble main target->root.
        for (auto it = route.begin(); it != route.end() && !args.handled; ++it) {
            args.source = *it;
            (*it)->OnKeyDownRouted(args);
        }
        if (args.handled) { RequestInvalidate(); return true; }
    }

    // Built-in navigation for keys no control consumed.
    if (focus_ && vk == VK_TAB) {
        focus_->MoveNext(HasModifier(mods, ModifierKeys::Shift));
        RequestInvalidate();
        return true;
    }
    // Basic directional focus (only if something is focused and the move lands).
    if (focus_ && f) {
        bool isDir = true;
        FocusDirection dir = FocusDirection::Up;
        switch (vk) {
            case VK_UP: dir = FocusDirection::Up; break;
            case VK_DOWN: dir = FocusDirection::Down; break;
            case VK_LEFT: dir = FocusDirection::Left; break;
            case VK_RIGHT: dir = FocusDirection::Right; break;
            default: isDir = false; break;
        }
        if (isDir) {
            UIElement* before = focus_->Focused();
            focus_->MoveDirectional(dir);
            if (focus_->Focused() != before) { RequestInvalidate(); return true; }
        }
    }
    return false;
}

void InputManager::TextInput(wchar_t ch) {
    UIElement* f = focus_ ? focus_->Focused() : nullptr;
    if (f) { f->OnTextInput(ch); RequestInvalidate(); }
}

void InputManager::CapturePointer(UIElement* e) {
    captured_ = e;
    // Take OS-level capture so WM_LBUTTONUP still arrives when the pointer leaves
    // the window: without it, a press-drag-outside-release never delivers the up
    // and the control stays stuck in Pressed state (§9.3 completion).
    if (hwnd_) ::SetCapture(hwnd_);
}

void InputManager::ReleaseCapture(UIElement* e) {
    if (captured_ == e) {
        captured_ = nullptr;
        // Release OS capture only if we still hold it (another window / caption
        // button may have stolen it). GetCapture returns the window that currently
        // holds capture, or null if none; checking before releasing prevents
        // disrupting another window's drag gesture.
        if (hwnd_ && ::GetCapture() == hwnd_) ::ReleaseCapture();
    }
}

void InputManager::OnWindowDeactivated() {
    if (captured_) {
        // Deliver a canceling release off-bounds so the control leaves its pressed
        // state without registering a click (roadmap §9.3), then drop capture.
        UIElement* c = captured_;
        captured_ = nullptr;
        // Release OS capture too. Don't check GetCapture() here — deactivation
        // means we've lost focus, so even if capture was stolen by another window
        // we still need to clean up the element state above.
        if (hwnd_) ::ReleaseCapture();
        PointerEventArgs args;
        args.position = Point{-1.0f, -1.0f};
        args.button = PointerButton::Left;
        args.source = c;
        args.originalSource = c;
        c->OnPointerReleased(args);
    }
    if (hot_) { hot_->OnPointerLeave(); hot_ = nullptr; }
    RequestInvalidate();
}

void InputManager::OnCaptureStolen() {
    // WM_CAPTURECHANGED: someone else called ::SetCapture. Deliver the same
    // canceling release as OnWindowDeactivated so the element clears Pressed,
    // but DON'T call ::ReleaseCapture — we no longer hold it.
    if (captured_) {
        UIElement* c = captured_;
        captured_ = nullptr;
        PointerEventArgs args;
        args.position = Point{-1.0f, -1.0f};
        args.button = PointerButton::Left;
        args.source = c;
        args.originalSource = c;
        c->OnPointerReleased(args);
    }
    // Don't clear hot_ — the mouse is still over the window, just capture moved.
    RequestInvalidate();
}

void InputManager::OnElementDetached(UIElement* e) {
    if (captured_ == e) captured_ = nullptr;
    if (hot_ == e) hot_ = nullptr;
    if (focus_) focus_->OnElementDetached(e);
}

}  // namespace fluent
