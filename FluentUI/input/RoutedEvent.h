// RoutedEvent.h — argument structs for tunneling/bubbling input (roadmap §9.2).
//
// A routed event travels the element chain from the hit element up to the root
// (Bubble) after an optional root->target Preview pass (Tunnel). The args carry
// the routing state shared by every phase:
//   * source          — the element currently handling (updated as it walks),
//   * originalSource   — the deepest element the pointer/key actually hit,
//   * handled          — set true by a handler to stop further routing.
// PointerEventArgs / KeyEventArgs add the event-specific payload. These are the
// values passed to the UIElement routing virtuals (OnPointerPressed etc.) and to
// the typed control Events.
#pragma once

#include "InputTypes.h"

namespace fluent {

class UIElement;  // forward: args reference elements without a layering cycle

struct RoutedEventArgs {
    UIElement* source = nullptr;          // element currently on the route
    UIElement* originalSource = nullptr;  // deepest hit element (route origin)
    bool handled = false;                 // set by a handler to stop routing
};

struct PointerEventArgs : RoutedEventArgs {
    Point position;                              // window DIPs
    PointerButton button = PointerButton::None;  // button for press/release
    ModifierKeys modifiers = ModifierKeys::None;
    int wheelDelta = 0;                          // WHEEL_DELTA units (wheel only)
    // 1 = single, 2 = double, 3 = triple (press only; saturates at 3 — see
    // ClickCounter.h). Defaults to 1 so a handler that does not care reads a plain
    // click, and so every existing construction site keeps its previous meaning.
    int clickCount = 1;
};

struct KeyEventArgs : RoutedEventArgs {
    unsigned vk = 0;                             // virtual-key code (VK_*)
    ModifierKeys modifiers = ModifierKeys::None;
};

}  // namespace fluent
