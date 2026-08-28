// MenuBar.h — Fluent top-of-window menu bar (File / Edit / View ...).
//
// A horizontal row of top-level menu titles drawn in the parent window content
// layer (like a ComboBox header). Clicking a title opens its MenuFlyout as a
// dropdown directly below the title (MenuFlyout::ShowBelow). While a menu is
// open, moving the pointer onto a different title switches to it (classic menu
// bar behavior); clicking the open title again closes it.
//
// The bar reuses a single MenuFlyout, re-pointing it at each title's item list
// on demand. Keyboard navigation within the open menu is handled by the flyout
// (arrows / Enter / Esc / Left-Right for submenus). The bar itself is
// mouse-driven in this version (no Alt-key mnemonic activation yet).
#pragma once

#include "../core/Control.h"
#include "../graphics/DWriteContext.h"
#include "MenuFlyout.h"
#include <string>
#include <vector>
#include <memory>

namespace fluent {

class WindowServices;

class MenuBar : public Control {
public:
    MenuBar();
    ~MenuBar();

    // Append a top-level title with its dropdown item list.
    void AddMenu(std::wstring title, std::vector<MenuItem> items);

    // Element overrides.
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;

    // Pure geometry: the drawable slice of one title, clamped to the bar's right
    // edge. Returns an EMPTY rect when the title starts at or past that edge, which
    // means this title and every later one is out of view.
    //
    // Measure() takes min(availW, sum-of-title-widths), so a window too narrow for
    // every title leaves the bar shorter than its content. Render must not paint the
    // overflow: the dirty rect is the bar's bounds, so pixels outside it would never
    // be cleared and the trailing highlight would survive as residue (WP-07 §S4).
    // Split out as a pure function so the clamping is unit-testable headless.
    static RectDip VisibleTitleRect(float titleX, float titleW, const RectDip& bar);

    void OnThemeChanged() override { Invalidate(); }

protected:
    // Build the flyout from the tree context (window/DWrite) on attach, replacing
    // the old manual Attach(WindowServices*)/SetDWrite (roadmap §6.2).
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnPointerLeft() override;

public:
    // Routed pointer input: hover tracking + click (on release) to open/close a
    // top-level menu. Not the base click gesture — the action is position-aware.
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

private:
    struct TopItem {
        std::wstring title;
        std::vector<MenuItem> items;
        float width = 0.0f;  // measured DIP width (title + horizontal padding)
    };

    // Index of the title under a window-DIP x coordinate, or -1.
    int ItemAt(float dipX) const;
    // Screen-pixel rect of title `i` (for ShowBelow anchoring / dismiss exempt).
    RECT ItemScreenRect(int i) const;
    // DIP x offset of title `i` from the bar's left edge.
    float ItemXOffset(int i) const;
    float MeasureText(const std::wstring& s) const;

    void OpenMenu(int i);
    void CloseMenu();

    // Cached from Context().window for the attach period (title screen rects +
    // flyout creation read it); cleared on detach.
    WindowServices* window_ = nullptr;
    std::unique_ptr<MenuFlyout> flyout_;
    std::vector<TopItem> items_;

    int hovered_ = -1;
    int openIndex_ = -1;
    bool switching_ = false;  // guards SetOnClosed while switching menus
};

} // namespace fluent
