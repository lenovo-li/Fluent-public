// ButtonBase.h — the shared "activatable control" primitive (roadmap §WP-06).
//
// Button, RadioButton and ToggleButton (CheckBox / ToggleSwitch) all opened with
// the identical four lines
//
//     Ctor() { SetFocusable(true); SetClickable(true); }
//     void OnClickRouted(PointerEventArgs&) override { <do the thing>; }
//     void OnKeyDownRouted(KeyEventArgs& e) override {
//         if (e.vk == VK_SPACE || e.vk == VK_RETURN) { <do the thing>; e.handled = true; }
//     }
//     void OnStateChanged()/OnFocusChanged() override { Invalidate(); }
//
// i.e. the same activation gesture (pointer click OR Space/Enter while focused)
// funnelled to one action, plus a repaint on hover/press/focus. ButtonBase owns
// exactly that: it is focusable + clickable, routes both triggers to a single
// virtual OnActivate(), and repaints on state/focus changes. Subclasses implement
// OnActivate() to do their specific thing (Button raises Click; ToggleButton
// toggles; RadioButton selects within its group) and add their own visuals.
#pragma once

#include "../../core/ContentControl.h"

namespace fluent {

class ButtonBase : public ContentControl {
public:
    ButtonBase() {
        SetFocusable(true);
        SetClickable(true);
    }

    // Space/Enter activate when the control has keyboard focus (routed, §9.2).
    void OnKeyDownRouted(KeyEventArgs& e) override {
        if (e.vk == VK_SPACE || e.vk == VK_RETURN) {
            OnActivate();
            e.handled = true;
        }
    }

protected:
    // The single activation point: a completed pointer click (release inside after
    // a press on this clickable_ element) or a Space/Enter keypress. Subclasses
    // override to define what activation does. Not called for a disabled control
    // (the base click gesture / focus both require enabled).
    virtual void OnActivate() {}

    // A pointer click routes to the same action as the keyboard.
    void OnClickRouted(PointerEventArgs&) override { OnActivate(); }

    // Hover / press / focus only change pixels, so repaint (never re-layout).
    // Subclasses that need extra work on a state change override and call up.
    void OnStateChanged() override { Invalidate(); }
    void OnFocusChanged() override { Invalidate(); }
};

} // namespace fluent
