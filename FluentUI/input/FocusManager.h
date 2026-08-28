// FocusManager.h — the single keyboard-focus authority (roadmap §9.4, WP-03).
//
// Before this, the window owned a `focused_` pointer and flipped element focus
// bits directly, so "who is focused" was really tracked in two places (the window
// and each element's bool). FocusManager makes it one: it is the only code that
// calls UIElement::SetFocused, so an element's IsFocused() can never disagree
// with the manager. The window keeps its SetFocusElement entry point but forwards
// here; the caret-blink timer and IME follow the FocusChanged event.
//
// Navigation: MoveNext walks the tab order (CollectFocusables over the roots,
// registration order); MoveDirectional is a basic geometric up/down/left/right
// (nearest focusable whose center lies in the requested direction). Detaching the
// focused element clears focus so a torn-down control never stays "focused".
#pragma once

#include "../base/Event.h"
#include <vector>

namespace fluent {

class UIElement;
class FocusManager;

// Payload for FocusChanged: the previously and newly focused elements (either may
// be null). Raised after the element focus bits have been updated.
struct FocusChangedArgs {
    UIElement* oldFocus = nullptr;
    UIElement* newFocus = nullptr;
};

// Direction for MoveDirectional (arrow-key spatial navigation, basic version).
enum class FocusDirection { Up, Down, Left, Right };

class FocusManager {
public:
    // Borrow the host's root element list (same vector the window hit-tests /
    // renders). Not owned; must outlive the manager.
    void SetRoots(const std::vector<UIElement*>* roots) { roots_ = roots; }

    UIElement* Focused() const { return focused_; }

    // The one focus mutator. `e` must be focusable (else focus is cleared to
    // null). Clears the previously focused element, sets the new one, and raises
    // FocusChanged. A no-op if `e` is already focused.
    void SetFocus(UIElement* e);
    void ClearFocus() { SetFocus(nullptr); }

    // Tab order navigation: move focus to the next (or previous) focusable element
    // with wrap-around. With nothing focused, focuses the first (or last).
    void MoveNext(bool backward);

    // Arrow-key navigation (basic): focus the nearest focusable whose center is in
    // the given direction from the current focus. No-op with nothing focused or no
    // candidate in that direction.
    void MoveDirectional(FocusDirection dir);

    // Called by the InputManager when an element leaves the tree: if it currently
    // holds focus, focus is cleared (without touching the now-detaching element).
    void OnElementDetached(UIElement* e);

    Event<FocusManager, FocusChangedArgs>& FocusChanged() { return focusChanged_; }

private:
    void CollectFocusables(std::vector<UIElement*>& out) const;

    const std::vector<UIElement*>* roots_ = nullptr;
    UIElement* focused_ = nullptr;
    Event<FocusManager, FocusChangedArgs> focusChanged_;
};

}  // namespace fluent
