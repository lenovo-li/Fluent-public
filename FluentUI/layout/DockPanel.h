// DockPanel.h — edge-docking container.
//
// Children are docked to an edge of the remaining space, in the order they were
// added. Each docked child consumes a strip from that edge; the next child sees
// only what is left. With LastChildFill (default true) the final child ignores its
// Dock and fills whatever remains — the standard "toolbar on top, status bar on
// bottom, content in the middle" shape.
//
// Dock is an attached property, stored the same way as Canvas's Left/Top: a static
// map keyed by UIElement*, defaulting to Dock::Left when never set. See Canvas.h
// for why the map is static and why it outlives removal from the panel.
//
// The order dependence is the whole point and is not a wart: a Top child added
// before a Left child spans the full width, while the reverse gives the Left child
// full height. WPF behaves identically, and callers rely on it.
#pragma once

#include "Panel.h"
#include <unordered_map>

namespace fluent {

enum class Dock { Left, Top, Right, Bottom };

class DockPanel : public Panel {
public:
    DockPanel() = default;

    // Attached property: which edge this element docks to. Default Dock::Left.
    static void SetDock(UIElement* element, Dock dock);
    static Dock GetDock(UIElement* element);

    // When true (the default) the LAST child ignores its Dock and fills the whole
    // remaining rect. Turn it off to have every child dock to an edge and leave
    // the middle empty.
    void SetLastChildFill(bool fill) { lastChildFill_ = fill; }
    bool LastChildFill() const { return lastChildFill_; }

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

private:
    static std::unordered_map<UIElement*, Dock> docks_;
    bool lastChildFill_ = true;
};

} // namespace fluent
