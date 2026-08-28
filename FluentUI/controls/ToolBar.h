// ToolBar.h — horizontal strip of command buttons with overflow.
//
// A ToolBar packs buttons left-to-right and collapses the ones that do not fit
// into an overflow "..." menu at the right edge. Buttons are style-specialized:
// no border, transparent background, hover shows a subtle fill (matching the
// WinUI CommandBar look). A vertical separator is available for grouping.
//
// The overflow measurement is pure layout (MeasureOverride): each button reports
// its desired width, the ToolBar sums them against the available width, and the
// ones past the cut are hidden and mirrored into the overflow MenuFlyout. The
// overflow button itself is always measured last (it only appears when needed).
#pragma once

#include "../layout/Panel.h"
#include "../base/Event.h"
#include "../core/Subscription.h"
#include "../input/RoutedEvent.h"
#include <memory>
#include <vector>
#include <functional>

namespace fluent {

class Button;
class MenuFlyout;
struct MenuItem;

class ToolBar : public Panel {
public:
    ToolBar();

    // Add a command button. Ownership moves to the ToolBar (it becomes a child
    // element, so it attaches, renders, and hit-tests like any other control).
    // `onClick` is invoked when the button is clicked OR when its overflow menu
    // item is chosen.
    void AddButton(std::unique_ptr<Button> btn, std::function<void()> onClick);

    // Add a vertical separator line (1 DIP wide, themed stroke color).
    void AddSeparator();

    // Set the overflow menu items. Called once with the full set of command
    // items; the ToolBar mirrors the overflowing buttons into this menu.
    void SetOverflowItems(std::vector<MenuItem> items);

    // The number of buttons currently visible (not overflowed). Exposed for tests.
    int VisibleButtonCount() const { return visibleCount_; }
    int TotalButtonCount() const {
        int n = 0;
        for (const auto& item : items_) if (!item.isSeparator) ++n;
        return n;
    }
    bool HasOverflow() const { return overflowActive_; }

    void Render(const DrawingContext& dc) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    // The overflow "..." is drawn by ToolBar itself, so it is not in children_ and
    // the base Panel walk would never hit it; the strip's empty areas return null so
    // wheel events pass through to a surrounding scroller.
    UIElement* HitTestDeep(float dipX, float dipY) override;

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;

private:
    struct Item {
        Button* button = nullptr;      // borrowed; owned by Panel::children_ (null for separator)
        std::function<void()> clickHandler;  // invoked when the button or its overflow menu item is chosen
        bool isSeparator = false;
        float width = 0.0f;              // measured desired width
        // Holds the button's Click registration alive. Subscription is RAII: if this
        // member did not exist the Subscribe() result would be destroyed at the end of
        // AddButton and the handler would never fire (latent bug in the first version).
        Subscription clickSub;
    };

    // Recompute which buttons fit in the available width and which overflow.
    void ComputeOverflow(float availW);
    // Show the overflow flyout at the "..." button position.
    void ShowOverflowMenu();
    // Build the overflow menu items from the currently-hidden buttons.
    std::vector<MenuItem> BuildOverflowItems() const;

    // Button click thunk: finds the clicked button in items_ and invokes its
    // stored handler. The Event::Subscribe API takes a plain function pointer.
    static void OnButtonClickThunk(void* owner, Button& btn, RoutedEventArgs&);

    std::vector<Item> items_;
    int visibleCount_ = 0;           // how many items fit (including separators)
    bool overflowActive_ = false;    // true when not all items fit
    float overflowBtnWidth_ = 32.0f; // width of the "..." button
    RectDip overflowBtnRect_;        // absolute rect of the "..." button (when active)
    bool overflowBtnHover_ = false;

    std::unique_ptr<MenuFlyout> overflowFlyout_;
    std::vector<MenuItem> overflowItems_;  // full set (for rebuild)

    Event<ToolBar, RoutedEventArgs> overflowOpened_;
};

} // namespace fluent
