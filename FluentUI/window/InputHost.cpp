// InputHost.cpp
#include "InputHost.h"
#include "../core/UIContext.h"
#include "../core/UIElement.h"

using namespace fluent;

void InputHost::Initialize(HWND hwnd, UIContext* context,
                           std::vector<UIElement*>* roots,
                           void (*invalidateThunk)(void*), void* owner) {
    // Wire context pointers (UIContext::input / UIContext::focus point into this host)
    context->input = &input_;
    context->focus = &focus_;

    // Wire the input routing (WP-03): both managers walk the registered roots; the
    // InputManager repaints via Render and tells the FocusManager who to focus.
    input_.SetRoots(roots);
    input_.SetFocusManager(&focus_);
    input_.SetHwnd(hwnd);
    input_.SetInvalidate(invalidateThunk, owner);
    focus_.SetRoots(roots);
}
