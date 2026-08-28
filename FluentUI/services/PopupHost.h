// PopupHost.h — Independent top-level HWND for floating popup content.
//
// A reusable primitive for any control that needs a floating layer (ComboBox
// dropdown, Tooltip, context menu, date picker). Each popup is a borderless
// child window (WS_POPUP + WS_EX_NOACTIVATE) with its own DComp composition target,
// sharing the parent's D2D/DWrite device. Positioned in screen coordinates,
// DPI-aware (fixed at open time), and dismissed via light-dismiss (click
// outside) or parent window deactivation.
//
// The popup renders a rounded card (panelBg + border) with transparent corners
// and draws arbitrary Element content inside. The content element is owned by
// the caller (e.g. ComboBox owns its list view).
#pragma once

#include "../fl_common.h"
#include "../graphics/D2DContext.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/DCompHost.h"
#include "../graphics/ResourceCache.h"
#include "../core/UIElement.h"
#include "../input/InputManager.h"
#include "PopupState.h"
#include <functional>
#include <algorithm>
#include <vector>

namespace fluent {

class PopupHost {
public:
    ~PopupHost();

    // Create the popup window (hidden). hInstParent, hwndOwner from the parent
    // window; d2d/dwrite are shared device-level contexts (safe to share across
    // multiple composition targets on the UI thread).
    HRESULT Create(HINSTANCE hInstParent, HWND hwndOwner, D2DContext* d2d,
                   DWriteContext* dwrite);

    // Position the popup at `anchorScreenRect` (screen pixels) with a DIP size
    // (wDip, hDip), then show. Picks the target monitor and DPI, flips above the
    // anchor if it would overflow the bottom of the work area, and clamps
    // horizontally. SetCapture for light-dismiss. Reuses the existing HWND.
    HRESULT Open(const RECT& anchorScreenRect, float wDip, float hDip);

    // Hide and release capture.
    void Close();
    // "Open" for callers means visible and interactive (Open or Closing counts as
    // not-yet-fully-closed; IsOpen reports true only in the steady Open state).
    bool IsOpen() const { return state_ == PopupState::Open; }
    PopupState State() const { return state_; }

    // The content Element to render inside the card. Not owned; caller (e.g.
    // ComboBox) owns it and must outlive the popup.
    void SetContent(UIElement* content);

    // Share the parent's resource cache so popup-hosted content reuses the same
    // text-layout / glyph-geometry caches (roadmap §13.3). The cache is DWrite /
    // D2D-factory backed and safe to share across composition targets on the UI thread.
    // Must be set before SetContent to be injected into the content's context.
    void SetResourceCache(ResourceCache* cache) { resourceCache_ = cache; }

    // Share the parent's stable theme snapshot (roadmap §11, WP-05) so popup
    // content reads the same tokens the tree does, and the popup paints its own
    // card chrome (fill / border) from it. The pointer is stable (overwritten in
    // place on a theme change); set it before SetContent so it is injected into
    // the content's context. Null falls back to a default light snapshot.
    void SetTheme(const ThemeSnapshot* theme) { theme_ = theme; }

    // Callback fired when the popup closes (e.g. so ComboBox can clear state).
    void SetOnClose(std::function<void()> cb) { onClose_ = std::move(cb); }

    // Forward a keyboard event from the parent window (parent retains focus, but
    // can route keys to the popup content for navigation). Returns true if handled.
    bool ForwardKey(UINT vk);

    // Trigger a repaint (content invalidate callback should call this).
    void Render();

    // Check if a screen-pixel point is inside the popup client rect (for parent
    // window to implement light-dismiss by checking clicks outside).
    bool ContainsScreenPoint(int screenX, int screenY) const;

    // Card background opacity [0..1], default 1.0 = fully opaque.
    //
    // A popup is its own HWND with WS_EX_NOREDIRECTIONBITMAP and NO system
    // backdrop, so unlike the main window it has no material to sit on: whatever
    // it does not paint opaquely shows the desktop / the parent window through.
    // The card fill token is authored at alpha 0.70 for a Mica base, which is why
    // an unflattened menu looked see-through. At opacity 1.0 the card is FLATTENED
    // over windowBackground (FlattenOver) so it renders as the solid color that
    // translucency was meant to look like; below 1.0 the flattened color is scaled
    // back down, so the app gets real translucency when it asks for it.
    void SetCardOpacity(float opacity) { cardOpacity_ = std::clamp(opacity, 0.0f, 1.0f); }
    float CardOpacity() const { return cardOpacity_; }

    // Opacity of the popup's CONTENT [0..1], default 1.0. Independent of the card
    // so an app can fade a menu's items without dissolving the card behind them.
    void SetContentOpacity(float opacity) {
        contentOpacity_ = std::clamp(opacity, 0.0f, 1.0f);
    }
    float ContentOpacity() const { return contentOpacity_; }

    // Enable/disable the popup's own click-outside light-dismiss (default on).
    // Tooltips and menu-chain layers turn this off so an external owner (the
    // TooltipService or a MenuFlyout session) coordinates dismissal across the
    // whole set of windows instead of each popup closing itself independently.
    void SetLightDismiss(bool enabled) { lightDismiss_ = enabled; }

    float DpiScale() const { return dpi_ / 96.0f; }
    HWND Hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    HRESULT InitGraphics(UINT pixelW, UINT pixelH);
    void OnSize(UINT pixelW, UINT pixelH);

    // Hit-test and route pointer events to the content element.
    void RouteMouseMove(int px, int py);
    void RouteMouseDown(int px, int py);
    void RouteMouseUp(int px, int py);
    void RouteMouseWheel(int px, int py, int delta);

    static void InvalidateThunk(void* ctx) {
        static_cast<PopupHost*>(ctx)->Render();
    }

    HWND hwnd_ = nullptr;
    HWND hwndOwner_ = nullptr;
    HINSTANCE hInst_ = nullptr;
    UINT dpi_ = 96;
    UINT pixelW_ = 0, pixelH_ = 0;
    PopupState state_ = PopupState::Closed;
    bool graphicsReady_ = false;

    D2DContext* d2d_ = nullptr;
    DWriteContext* dwrite_ = nullptr;
    const ThemeSnapshot* theme_ = nullptr;  // parent's stable snapshot (non-owning)
    ResourceCache* resourceCache_ = nullptr;  // shared parent cache (non-owning)
    DCompHost comp_;

    UIElement* content_ = nullptr;       // not owned
    // The popup's own input router: hit-tests + routes pointer events over the
    // single content element and owns pointer capture inside this window (WP-03).
    // A popup keeps focus in the parent window, so there is no FocusManager here;
    // keyboard reaches the content via ForwardKey -> OnKeyDownRouted.
    InputManager input_;
    std::vector<UIElement*> roots_;      // holds content_ so InputManager can walk it

    float cardOpacity_ = 1.0f;     // background card opacity [0..1]
    float contentOpacity_ = 1.0f;  // content opacity [0..1]
    bool lightDismiss_ = true;  // click-outside closes this popup itself

    std::function<void()> onClose_;
};

} // namespace fluent
