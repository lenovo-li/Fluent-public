// IContextMenu.h — abstract per-element context menu (roadmap §9, WP-03 follow-up).
//
// A UIElement can own a context menu shown on right-click. The concrete menu
// (MenuFlyout) lives in controls/ and depends on core/, so core/ must not depend
// on it directly. This thin interface breaks that cycle: UIElement owns an
// IContextMenu (by unique_ptr, deleted polymorphically via the virtual dtor), and
// MenuFlyout implements it. Mirrors WPF's FlyoutBase attach point.
//
// The framework feeds the owner's service context (window/DWrite/HWND) to the
// menu when the element attaches, so a control's menu needs no manual wiring —
// the same "attach injects services" rule as the rest of the tree (§6.2).
#pragma once

namespace fluent {

struct UIContext;

class IContextMenu {
public:
    virtual ~IContextMenu() = default;

    // Hand the menu the owning element's tree context (device / window / popup
    // registries). Called by UIElement on attach (and immediately from
    // SetContextMenu if the element is already attached), so the menu can position
    // and render without the app wiring services by hand.
    virtual void SetOwnerContext(const UIContext& ctx) = 0;

    // Open the menu as a context menu with its top-left at the given screen pixels
    // (the menu flips / clamps to the work area itself).
    virtual void ShowAt(int screenX, int screenY) = 0;
};

}  // namespace fluent
