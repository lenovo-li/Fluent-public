// UIContext.h — the per-tree service context injected at attach (roadmap §6.2).
//
// The old model made every control ask its host for services by hand:
// `SetDWrite(...)`, `Attach(WindowServices*)`, `Button::CreateOverlay(...)`. That
// scatters wiring across the app, couples controls to the concrete window, and
// leaks service pointers past the control's tree lifetime. The refactor replaces
// it with a single context the framework hands each element when it is attached
// to a live tree, and clears when it is detached.
//
// A control never stores a raw service pointer beyond its attach period: it reads
// what it needs through the protected UIElement::Context() while attached, and on
// detach the context is cleared (and its subscriptions released). This is what
// lets a plain "create control + add to tree" sequence work with no manual
// injection, and what makes safe teardown deterministic.
//
// SCOPE FOR THIS STEP (WP-02 Step 2): the context carries only the services that
// exist today — the host (WindowServices) for device / popup registration, the
// DirectWrite context for text layout, the animation registry so a control can
// register/deregister itself, plus the owning HWND and current DPI scale. Later
// work packages replace these coarse handles with the finer services the guide
// lists (DrawingService, InputManager, FocusManager, PopupService, ThemeManager,
// FrameScheduler, Dispatcher…); the struct grows there. It is intentionally a
// plain aggregate of non-owning pointers: null members mean "not provided by this
// host / not attached", so it is trivially usable in headless layout tests.
#pragma once

#include "../fl_common.h"

namespace fluent {

class WindowServices;
class DWriteContext;
class AnimationRegistry;
class InputManager;
class FocusManager;
class ResourceCache;
struct ThemeSnapshot;

// Behavior-level settings (not visual tokens — those live in ThemeSnapshot).
// Grouped here so behavior policy can be swapped tree-wide without touching
// per-control defaults, and so a control can still override per-instance.
// All fields have library defaults; `nullptr` in the context means "use the
// static defaults".
struct BehaviorSettings {
    // Hyperlink activation gesture. When true (the library default), a URI
    // hyperlink only navigates on Ctrl+Click — plain Click raises the Click
    // event. When false, plain Click on a URI hyperlink goes straight to the
    // shell (WinUI behavior). A per-instance Hyperlink::SetActivation() call
    // overrides this global.
    bool hyperlinkRequireCtrl = true;
};

struct UIContext {
    // The host window, for device access, DPI/HWND, and active-popup registration
    // (ComboBox / MenuFlyout light-dismiss + key hooks route through it). Non-owning.
    WindowServices* window = nullptr;

    // Shared DirectWrite context for text measurement / layout. Non-owning; owned
    // by the host and outlives the attached tree. Replaces per-control SetDWrite.
    DWriteContext* dwrite = nullptr;

    // The host's active-animation set. A control that starts animating registers
    // through the framework; on detach it is removed so a torn-down control is
    // never ticked. Non-owning.
    AnimationRegistry* animations = nullptr;

    // Owning window handle (Imm* calls, positioning). May be null in tests.
    HWND hwnd = nullptr;

    // Current DPI scale (dpi / 96). Updated by the host on WM_DPICHANGED and
    // re-pushed to the tree so a cached-per-DPI resource can refresh.
    float dpiScale = 1.0f;

    // True while the window is inside the SYSTEM modal move/resize loop
    // (WM_ENTERSIZEMOVE → WM_EXITSIZEMOVE), i.e. the user is holding a border.
    //
    // WHY A COMPOSITED CONTROL CARES. IDCompositionSurface::BeginDraw is a GPU
    // sync point: it maps the surface and waits for the compositor to release it.
    // Measured on the Gallery's TextArea page (five composited TextAreas, eleven
    // surfaces per frame), a single BeginDraw blocked 12.1ms of a 20.9ms resize
    // frame — 61% of the frame, against 4.1ms for all of our own D2D drawing. The
    // fix cannot be "draw faster"; it has to be "do not map a surface this frame".
    // So during the drag a control pushes viewport / clip / extent (cheap, keeps
    // the compositor showing correctly positioned pixels) and defers the
    // RASTERIZATION until the drag ends, where one exact redraw converges.
    //
    // Deliberately a context flag rather than each control asking the window: the
    // controls that need it (TreeView / TextArea) already read the context on
    // every RefreshComposition, and a headless test wires this to false by simply
    // not setting it.
    bool inModalResize = false;

    // Input routing services for the attached tree (roadmap §9, WP-03). `input`
    // is the hit-test + pointer-capture + routing hub the host feeds translated
    // Win32 events into; a control grabs/releases capture through it (e.g. Slider
    // during a drag). `focus` is the single focus authority (Tab / directional /
    // click-to-focus). Both non-owning, owned by the host, null in headless tests
    // that don't exercise routing.
    InputManager* input = nullptr;
    FocusManager* focus = nullptr;

    // Shared resource caches for text layout / icon geometry / stroke styles
    // (roadmap §13.3, WP-04). Owned by the host; a control reaches it to reuse a
    // per-frame-rebuilt IDWriteTextLayout or a cached glyph geometry instead of
    // building its own each frame. Non-owning; null in headless tests that don't
    // exercise caching.
    ResourceCache* resourceCache = nullptr;

    // The current immutable theme snapshot (roadmap §11, WP-05). Owned by the
    // host's ThemeManager; a STABLE pointer whose pointee is overwritten in place
    // on a theme change (the epoch bump on generation is the cache-rebuild
    // signal). A control reads design tokens (colors/spacing/typography) through
    // it. Non-owning; null in headless tests, where a control falls back to a
    // static default snapshot.
    const ThemeSnapshot* theme = nullptr;

    // Behavior-level settings (hyperlink gesture, etc.). Null in headless
    // tests; a control falls back to `BehaviorSettings{}` defaults. Hosts that
    // want to flip a behavior tree-wide (e.g. "single click opens links in
    // this app") hold a BehaviorSettings member and point this at it.
    const BehaviorSettings* behavior = nullptr;

    // A context with no host wired in — the default for a detached element.
    bool IsValid() const { return window != nullptr; }
};

} // namespace fluent
