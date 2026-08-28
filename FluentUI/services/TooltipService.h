// TooltipService.h — window-level hover-tooltip manager.
//
// Mirrors WinUI's ToolTipService: any Element can carry hover text via
// Element::SetTooltip(...). The owning NativeWindowHost tracks the element under
// the pointer and, after a short delay, asks this service to show a small
// popup card near the cursor. Moving away, clicking, or wheeling hides it.
// Keypresses do NOT hide the tooltip: a modifier going down (Ctrl for a hotkey)
// is not a completed action, and the pointer has not moved — the thing being
// pointed at is still relevant. One PopupHost + one TextBlock are reused for
// every tooltip.
//
// The tooltip popup is a non-light-dismiss PopupHost (SetLightDismiss(false)):
// it never steals input and is hidden explicitly by the window, not by clicks.
#pragma once

#include "../fl_common.h"
#include "PopupHost.h"
#include "../controls/TextBlock.h"
#include <string>
#include <memory>

namespace fluent {

class D2DContext;
class DWriteContext;

class TooltipService {
public:
    // Create the (hidden) tooltip popup, sharing the parent's devices. Call once
    // after the window and its D2D/DWrite contexts exist. `theme` is the parent's
    // stable theme snapshot (roadmap §11) so the tooltip card + text theme with
    // the window; it is injected into the content TextBlock and the popup chrome.
    HRESULT Create(HINSTANCE hInst, HWND hwndOwner, D2DContext* d2d,
                   DWriteContext* dwrite, const ThemeSnapshot* theme);

    // Show `text` anchored near the pointer. `cursorScreenX/Y` are physical
    // screen pixels (typically the current cursor position). The card is placed
    // just below-right of the cursor and flipped/clamped to the work area by
    // PopupHost. No-op if text is empty.
    void Show(const std::wstring& text, int cursorScreenX, int cursorScreenY);

    // Hide the tooltip if visible.
    void Hide();

    bool IsVisible() const { return popup_ && popup_->IsOpen(); }

private:
    DWriteContext* dwrite_ = nullptr;
    std::unique_ptr<PopupHost> popup_;
    std::unique_ptr<TextBlock> content_;
};

} // namespace fluent
