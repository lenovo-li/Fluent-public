// MenuFlyout.h — Fluent popup menu (context menu / dropdown / submenu host).
//
// A MenuFlyout shows a list of MenuItems in a floating card (PopupHost). It is
// the single class behind context menus, command-button dropdowns, and (from
// stage 3) cascading submenus — mirroring WinUI's MenuFlyout, where a
// "context menu" is just one way to open it.
//
// Two entry points:
//   * ShowAt(screenX, screenY)      — context menu anchored at the cursor.
//   * ShowBelow(anchorScreenRect)   — dropdown anchored below a control
//                                     (used by MenuBar and command buttons).
//
// The flyout owns a stack of levels (each level = one PopupHost + one list
// view). Stage 2 opens only the root level; stage 3 pushes submenu levels on
// hover / Right-arrow. The whole chain is dismissed as a unit: clicking outside
// every popup, losing activation to another app, moving the parent window, or
// pressing Esc. The parent window keeps focus, so keys are routed in via
// WindowServices::RegisterActivePopupKeyHandler.
#pragma once

#include "../core/Control.h"
#include "../core/Subscription.h"
#include "../core/IContextMenu.h"
#include "../graphics/DWriteContext.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace fluent {

class WindowServices;
class PopupHost;

// One row in a menu. A separator is a thin non-interactive divider. A non-empty
// submenu makes this a parent item (drawn with a right chevron); clicking it
// opens the child level instead of invoking. Otherwise onInvoke fires on click.
struct MenuItem {
    std::wstring text;
    std::wstring accelerator;        // right-aligned hint, e.g. L"Ctrl+C"
    bool separator = false;
    bool enabled = true;
    bool checked = false;            // draws a leading check glyph
    std::vector<MenuItem> submenu;   // non-empty => parent item (chevron)
    std::function<void()> onInvoke;  // fired when a leaf item is chosen

    bool HasSubmenu() const { return !submenu.empty(); }
    bool Selectable() const { return enabled && !separator; }

    static MenuItem Sep() { MenuItem m; m.separator = true; m.enabled = false; return m; }
};

class MenuFlyout : public IContextMenu {
public:
    MenuFlyout();
    ~MenuFlyout() override;

    // IContextMenu: a UIElement owning this menu feeds it the tree context on
    // attach. Forwards to SetContext (same service set), so a control's
    // right-click menu is wired with no manual SetContext call from the app.
    void SetOwnerContext(const UIContext& ctx) override { SetContext(ctx); }

    // Provide the tree service context (HINSTANCE/HWND/D2D/DWrite via
    // Context().window + the popup-dismiss/key registries). A MenuFlyout is not a
    // tree node itself (it is owned by a MenuBar or used standalone), so its owner
    // forwards its own Context() here once attached. The flyout attaches each list
    // view to this context, so the list's inherited Dwrite() works at measure time
    // (before PopupHost re-injects its own context on SetContent). Replaces the old
    // manual SetDWrite + Attach(WindowServices*) pair (roadmap §6.2).
    void SetContext(const UIContext& ctx) { ctx_ = ctx; window_ = ctx.window; }

    // Replace the menu items. Closes the menu if open.
    void SetItems(std::vector<MenuItem> items);

    // Open as a context menu with the top-left corner at the cursor (flips /
    // clamps to the work area). screenX/screenY are physical screen pixels.
    // Overrides IContextMenu::ShowAt.
    void ShowAt(int screenX, int screenY) override;

    // Open as a dropdown directly below anchorScreenRect (physical pixels),
    // left-aligned with it; flips above if it would overflow the work area.
    void ShowBelow(const RECT& anchorScreenRect);

    // Close the entire chain.
    void Close();
    bool IsOpen() const;

    // Fired after the whole chain closes (from any dismiss path).
    void SetOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }

    // Owner-supplied dismiss guard: a click at the given screen point that
    // returns true keeps the menu open (the owner handles it — e.g. a MenuBar
    // title toggling/switching its own dropdown). Clicks inside the menu's own
    // popup windows are always kept open regardless of this guard.
    void SetDismissGuard(std::function<bool(int screenX, int screenY)> cb) {
        dismissGuard_ = std::move(cb);
    }

    // Menu card / content opacity [0..1], default 1.0 (opaque). Applied to every
    // level, including submenu levels opened later. A menu is opaque by default:
    // its popup HWND has no material behind it, so translucency there shows the
    // parent window through (see PopupHost::SetCardOpacity).
    void SetCardOpacity(float opacity);
    float CardOpacity() const { return cardOpacity_; }
    void SetContentOpacity(float opacity);
    float ContentOpacity() const { return contentOpacity_; }

private:
    // A single open level: its popup window, its list-view content, and the
    // index (in the parent level) of the item that spawned it (-1 for the root).
    struct Level {
        std::unique_ptr<PopupHost> popup;
        std::unique_ptr<Control> list;          // MenuListView (defined in .cpp)
        const std::vector<MenuItem>* items = nullptr;
        int parentIndex = -1;
    };

    // Open the root level for `items_` anchored at `anchor`. preferBelow places
    // it below the anchor (dropdown); otherwise the anchor is a cursor point.
    void OpenRoot(const RECT& anchor);

    // Open the submenu of item `itemIndex` in level `levelIndex` as a new,
    // deeper level (stage 3). No-op in stage 2 wiring paths.
    void OpenSubmenu(int levelIndex, int itemIndex);

    // Hover moved to `itemIndex` in `levelIndex` (stage 3 opens/closes submenus).
    void OnItemHovered(int levelIndex, int itemIndex);

    // Right/Left arrow at `levelIndex` (stage 3 enters/leaves a submenu level).
    void OnLeftRight(int levelIndex, bool right);

    // Close every level at or deeper than `fromLevel` (keeps shallower ones).
    void CloseFrom(int fromLevel);

    // Invoke item `itemIndex` of level `levelIndex`: fire its callback, close all.
    void InvokeItem(int levelIndex, int itemIndex);

    // Build a MenuListView bound to `items` with callbacks wired to this flyout.
    std::unique_ptr<Control> MakeListView(const std::vector<MenuItem>* items,
                                          int levelIndex);

    // Measure the popup card size (DIP) needed for `items`.
    void MeasureLevel(Control* list, float& outW, float& outH) const;

    // Window hooks: dismiss (click-outside / deactivate / move) + key routing.
    void RegisterWindowHooks();
    void ClearWindowHooks();
    bool HandleKey(UINT vk);      // routed from the parent window
    bool HandleDismiss(int reason, HWND otherHwnd, int screenX, int screenY);

    // True if `screenX/screenY` is inside any open level's popup window.
    bool PointInAnyPopup(int screenX, int screenY) const;
    // True if `hwnd` is one of our open level windows.
    bool IsOwnPopup(HWND hwnd) const;

    // The service context handed down by the owner (MenuBar or the window),
    // replacing the old SetDWrite/Attach injection. window_ is cached from it for
    // the many popup-positioning call sites; list views are attached to ctx_ so
    // their inherited Dwrite() works at measure time (before PopupHost injects).
    UIContext ctx_;
    WindowServices* window_ = nullptr;
    std::vector<MenuItem> items_;
    std::vector<Level> levels_;      // levels_[0] = root; deeper = submenus
    // Closed-but-not-yet-destroyed levels. Closing a level can happen while we are
    // *inside* that PopupHost's WndProc (a menu item's onInvoke closes the chain
    // during pointer-release routing); destroying the PopupHost — and its
    // InputManager — right then would unwind back through freed objects
    // (use-after-free). So CloseFrom only *hides* a level and parks it here; the
    // actual destruction happens later at a safe point (the next OpenRoot, or the
    // destructor), never on the popup's own call stack. Mirrors ComboBox keeping
    // its hidden popup alive.
    std::vector<Level> retired_;
    void ReapRetired();  // defined in .cpp (Level owns a unique_ptr<PopupHost>,
                         // incomplete here — its dtor must run where it is complete)
    // Own the window's key + dismiss registrations while the menu chain is open.
    // Reset on close (and by ~MenuFlyout), so the window never holds a dangling
    // [this] callback after this flyout is destroyed.
    Subscription keySub_;
    Subscription dismissSub_;
    std::function<void()> onClosed_;
    // Owner-supplied hit test: a click at these screen pixels keeps the menu open
    // (the owner, e.g. a MenuBar, handles the toggle/switch itself). May be null.
    std::function<bool(int, int)> dismissGuard_;
    // Opacity applied to every level's popup, remembered here so a submenu opened
    // after the setter still gets it (levels are created lazily on hover).
    float cardOpacity_ = 1.0f;
    float contentOpacity_ = 1.0f;
    // Push cardOpacity_/contentOpacity_ onto a freshly created level's popup.
    void ApplyOpacityTo(Level& level);
};

} // namespace fluent
