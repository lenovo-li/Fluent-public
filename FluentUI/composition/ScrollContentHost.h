// ScrollContentHost.h — compositor-thread content scrolling with an overscan
// surface (roadmap §11.2 "T2", §11.5). Phase 3 of the compositor migration.
//
// A list-like control (TreeView first) draws MORE than the viewport — the
// viewport plus an overscan margin above and below — into a single composition
// surface, then the compositor translates that surface on its own thread. Short
// scrolls stay entirely on the compositor: they keep moving smoothly even while
// the UI thread is blocked (a window resize, a slow layout). Only when the view
// nears the edge of the drawn surface does the UI thread redraw (refill + rebase),
// and that redraw is coalesced with the next frame.
//
// Visual tree parented above the window content:
//     viewport_  (container, positioned + clipped to the control's bounds)
//     ├── clip_    (clipped to the SCROLLABLE region = bounds inset by contentInset)
//     │   └── content_ (surface = region + overscan; OffsetY driven by the compositor)
//     │       └── caret_   (optional text caret; inherits content's OffsetY)
//     └── overlay_ (surface = full bounds: scrollbar / focus ring / frame; static)
//
// WHY clip_ EXISTS: a text editor scrolls its text inside padding, so the text must be
// masked to the padded region while the scrollbar and frame still paint over the FULL
// bounds. The mask cannot live on content_ — a visual's clip is in its own space, so it
// would slide up and down with the scroll offset. It therefore needs its own node,
// fixed in viewport space, between the container and the scrolled surface. With a zero
// inset (a list control like TreeView) clip_ spans the whole viewport and is a
// pass-through.
//
// COORDINATE MODEL (the one invariant everything rests on):
//   * The public API is in DIPs; the host converts to physical pixels internally
//     (composition visuals are pixel-space) using the DPI scale.
//   * `surfaceOrigin_` (DIP) = the content-space Y that maps to the TOP of the
//     content surface. The surface holds content rows [surfaceOrigin_,
//     surfaceOrigin_ + surfaceHeight_).
//   * The logical scroll position shown on screen is the EFFECTIVE offset. A
//     smooth scroll is a decelerate tween of that offset; EffectiveOffset()
//     evaluates it from QPC using the SAME DecelerateCubic the compositor runs, so
//     a click mid-animation hits the row actually under the cursor (§11.10).
//   * The content visual's OffsetY (pixels) = (surfaceOrigin_ - effectiveOffset) *
//     dpiScale. As the offset grows (scroll down) the surface slides up. The
//     offset↔OffsetY map is affine, so evaluating the tween in offset space on the
//     UI thread and driving it in OffsetY space on the compositor stay identical.
//
// The host owns its visuals; the control owns the host and drives refill/redraw
// through the two draw callbacks. Null backend / creation failure → Valid() is
// false and the control falls back to its UI-thread scroll path.
#pragma once

#include "../fl_common.h"
#include "ICompositionBackend.h"
#include <d2d1_1.h>
#include <functional>
#include <memory>

namespace fluent {

// Compose a "window DIPs → composition surface tile pixels" transform for content
// drawn by a control that thinks in window coordinates (the scrollbar overlay, the
// focus ring). `base` is the transform the surface's device context ARRIVED with —
// it carries the atlas tile origin, which DirectComposition may move between draws,
// so it must be preserved by premultiplying rather than overwritten. Row-vector
// order: shift the control's origin to zero, scale to pixels, then land in the tile.
//
// Dropping `base` puts the drawing outside the visual's tile (clipped away, or over
// a neighbour's), which flickers as tiles are reassigned — the regression this guards.
inline D2D1_MATRIX_3X2_F SurfaceTransformFromWindowDip(const D2D1_MATRIX_3X2_F& base,
                                                       float originXDip,
                                                       float originYDip,
                                                       float dpiScale) {
    return D2D1::Matrix3x2F::Translation(-originXDip, -originYDip) *
           D2D1::Matrix3x2F::Scale(dpiScale, dpiScale) *
           *D2D1::Matrix3x2F::ReinterpretBaseType(&base);
}

class ScrollContentHost {
public:
    // Draw the content surface. Called on the UI thread during a refill: the dc is
    // already set up so the callback draws in DIPs with (0,0) at surfaceOrigin — i.e.
    // a content row at content-space Y is drawn at local y = Y - surfaceOrigin. The
    // callback must clear the surface (transparent) then paint the rows spanning
    // [surfaceOrigin, surfaceOrigin + surfaceHeight). dc is null on a headless
    // backend (nothing to rasterize); the callback must tolerate that.
    using DrawContentCallback =
        std::function<void(ID2D1DeviceContext* dc, float surfaceOriginDip,
                           float surfaceHeightDip)>;
    // Draw the overlay surface (scrollbar, focus ring) in DIPs with (0,0) at the
    // viewport top-left. Not translated with the content.
    using DrawOverlayCallback =
        std::function<void(ID2D1DeviceContext* dc, float viewportWDip,
                           float viewportHDip)>;

    ~ScrollContentHost() { Destroy(); }

    // Build the three visuals over `backend` and parent the viewport on the root.
    // Returns S_OK only when fully created. `dpiScale` = dpi/96.
    HRESULT Create(ICompositionBackend* backend, float dpiScale);
    // Release all visuals (removes the viewport from the root). Safe if never created.
    void Destroy();
    bool Valid() const { return viewport_ != nullptr; }

    // Geometry (DIPs). SetViewport repositions + reclips the viewport container and
    // sizes the surfaces; a size change forces a content + overlay redraw on the
    // next EnsureContent/EnsureOverlay. SetContentHeight sets the scrollable extent.
    void SetViewport(const RectDip& viewportDip);

    // Clip the composed sub-tree to an ancestor's viewport, in WINDOW DIPs (the same
    // space SetViewport takes). A control inside a scrolling container passes
    // UIElement::AncestorViewportClip() here; without it the DComp visuals are bounded
    // only by their own viewport and paint over the title bar / status bar, because a
    // container's D2D clip cannot reach the composition side.
    //
    // Kept SEPARATE from the viewport rect on purpose: the clip must be expressed
    // relative to the viewport's true position (localTop = clip.y - viewport.y), so
    // pre-intersecting it into the viewport rect would destroy the operand that
    // subtraction needs. Re-applied automatically on the next SetViewport.
    void SetAncestorClip(const RectDip& clipWindowDip);
    void ClearAncestorClip();
    // Inset (DIPs) of the SCROLLABLE region inside the viewport — the padded text box
    // of an editor. The content surface is sized and masked to this region while the
    // overlay still covers the full viewport (scrollbar, frame). Default: all zero, so
    // the region IS the viewport (list controls need nothing here). Content-space Y
    // stays measured from the region's top, so a caller's text coordinates are
    // unaffected by the inset.
    struct Inset { float left = 0, top = 0, right = 0, bottom = 0; };
    void SetContentInset(const Inset& inset);

    // The color the content surface is cleared to before the draw callback paints.
    //
    // WHY THIS IS NOT ALWAYS TRANSPARENT. The surface is a separate DComp visual, so
    // whatever the control painted into the WINDOW frame (its themed background box)
    // is not underneath these pixels — the compositor blends the surface straight onto
    // whatever the window shows there. Clearing to transparent therefore blends the
    // text against nothing, and with a near-transparent themed fill (dark theme's
    // controlFillDefault is white at alpha 0.06) light text over a dark Mica window
    // came out dark-on-dark: invisible. Clearing to the control's own resolved fill
    // gives the glyphs the same base they would have had on the UI-thread path.
    //
    // Default stays fully transparent: a list control (TreeView) draws its own row
    // fills and relies on seeing the window behind the gaps.
    void SetSurfaceClearColor(const D2D1_COLOR_F& color);
    // The scrollable region in window DIPs (viewport minus the inset).
    RectDip ContentRegion() const;
    void SetContentHeight(float contentHeightDip);
    void SetDpiScale(float dpiScale);

    // Tell the host whether a modal move/resize loop is in progress, pushed by the
    // owning control from UIContext.inModalResize on every RefreshComposition.
    //
    // While true, RasterizeSurface and RedrawOverlay return without mapping a
    // surface — see UIContext.h for the BeginDraw measurement that motivates it.
    // Geometry (viewport, clip, extent, offsets) is unaffected and keeps flowing, so
    // the compositor still shows the existing pixels in the right place.
    //
    // THE FALLING EDGE IS LOAD-BEARING. Going true→false marks both surfaces dirty,
    // because everything skipped during the drag left them stale: without it the
    // final size would keep whatever pixels the drag started with, and nothing else
    // would ever ask for a redraw (a resize that ends on an unchanged quantized
    // surface clears no flag). The control's own post-drag RefreshComposition then
    // rasterizes once, at the exact final size.
    void SetModalResize(bool inModalResize);


    // Uniform opacity [0..1] for the whole composed sub-tree (content + overlay +
    // caret), pushed onto the VIEWPORT visual so the compositor applies it once to
    // the composite — true opacity semantics, and free (no re-rasterization, no UI
    // thread work). A composited control needs this because it paints into its own
    // surface, so an opacity folded into the host's DrawingContext never reaches it.
    // Survives device loss (re-applied by OnDeviceRestored).
    void SetOpacity(float opacity);
    float Opacity() const { return opacity_; }

    // Attach / detach the viewport from the compositor root. This is what implements
    // WPF Collapsed semantics for a composited control: SetTreeVisible(false) when
    // the element's effective visibility (self + all ancestors) becomes false, and
    // the DComp visual is removed from the root — the compositor stops drawing it,
    // and (equally important for perf) any call to EnsureContent / RedrawOverlay /
    // RasterizeSurface can short-circuit because nobody will see the output anyway.
    // Survives device loss (re-applied by OnDeviceRestored). Do NOT confuse this
    // with SetOpacity(0), which keeps the visual on the root and forces every
    // scroll / resize / theme change to re-raster a surface nobody can see.
    void SetTreeVisible(bool visible);
    bool TreeVisible() const { return treeVisible_; }

    // The SCROLLABLE height (region, not the full viewport) — the "one page" a
    // PageDown moves and what MaxOffset subtracts.
    float ScrollableHeight() const { return ContentRegion().h; }
    float MaxOffset() const;

    // The logical scroll offset currently SHOWN (DIPs). Evaluates an in-flight
    // decelerate tween from QPC (and lazily clears the "animating" flag once it has
    // settled). Use this for hit-test, selection mapping, and the scrollbar thumb.
    float EffectiveOffset();
    // The tween's destination (DIPs); == EffectiveOffset() when settled.
    float TargetOffset() const { return targetOffset_; }
    bool IsAnimating() const { return animating_; }

    // Smooth-scroll to an absolute offset on the compositor thread: seeds the tween
    // at the CURRENT effective offset (no jump on a mid-flight retarget, §11.6),
    // clamps the target, drives the compositor OffsetY tween, and requests a commit.
    // Recenters the overscan surface on the target (so the fling lands mid-buffer).
    void AnimateTo(float offsetDip, const DrawContentCallback& drawContent);
    void AnimateBy(float deltaDip, const DrawContentCallback& drawContent) {
        AnimateTo(TargetOffset() + deltaDip, drawContent);
    }
    // Jump to an offset immediately (drag / keyboard / EnsureVisible): cancels any
    // tween, pins the visual, refills if needed, commits.
    void SetOffsetImmediate(float offsetDip, const DrawContentCallback& drawContent);

    // Ensure the content surface covers the current view (refill + rebase if the
    // effective offset is near a drawn edge) and apply the current offset. Pass
    // `forceRedraw` = true when the CONTENT PIXELS changed but the offset did not
    // (selection highlight moved, rows added, theme recolored) — the surface is
    // re-rasterized at its current origin so the change shows without needing a
    // scroll to trigger a refill. No-op while a compositor tween owns the offset.
    void EnsureContent(const DrawContentCallback& drawContent,
                       bool forceRedraw = false);
    // Redraw the overlay surface (scrollbar fade / focus ring change).
    void RedrawOverlay(const DrawOverlayCallback& drawOverlay);

    // --- Text caret (optional; a text editor's blinking insertion bar) --------
    // The caret is its OWN visual parented under the CONTENT visual, which buys two
    // things for free:
    //   * it inherits the content's OffsetY, so it rides a compositor scroll tween
    //     with no UI-thread involvement and can never drift from the text;
    //   * its blink is an opacity animation on the compositor, so blinking costs no
    //     timer and no repaint — and it does not re-rasterize the text surface (which
    //     is what makes a live composition surface shimmer).
    // `rectContentDip` is in the SAME space the content callback draws in: x from the
    // surface's left edge, y in content space (so the caller passes the caret's
    // content-space Y, NOT a viewport-relative one).
    void SetCaret(const RectDip& rectContentDip, const D2D1_COLOR_F& color);
    void SetCaretVisible(bool visible);
    bool HasCaret() const { return caret_ != nullptr; }
    // Start / restart the infinite blink. Restarting resets the phase to "solid",
    // which is what a keystroke should do. No-op without a caret.
    void StartCaretBlink(double halfPeriodSec);
    void StopCaretBlink();   // freeze the caret solid (e.g. focus lost -> hide)
    void DestroyCaret();

    // Device-loss: drop visuals (device is gone); recreate against the fresh backend
    // and restore the scroll position + force a redraw. The control re-supplies the
    // draw callbacks on its next paint.
    void OnDeviceLost();
    HRESULT OnDeviceRestored(ICompositionBackend* backend, float dpiScale);

    // The content surface's current logical top (DIPs) — for tests / diagnostics.
    float SurfaceOrigin() const { return surfaceOrigin_; }
    float SurfaceHeight() const { return surfaceHeight_; }

private:
    // (Re)rasterize the content surface at the current surfaceOrigin_ (no recenter).
    void RasterizeSurface(const DrawContentCallback& drawContent);
    // Push the content visual's OffsetY for a given effective offset (static).
    void ApplyContentOffset(float effectiveOffsetDip);
    // Choose a new surfaceOrigin centered on `centerOffsetDip`, redraw the content
    // surface there, and re-anchor the visual/tween so the screen does not jump.
    void Rebase(float centerOffsetDip, const DrawContentCallback& drawContent);
    // Is the drawn surface too close to an edge to cover the view around `offset`?
    bool NeedsRefill(float offsetDip) const;
    // Recreate the surfaces' pixel sizes from the viewport + overscan.
    void UpdateSurfaceMetrics();

    // Re-apply the caret's pixel offset/size from its cached content-space rect.
    void ApplyCaretGeometry();

    // Push clip_'s offset + clip from the current viewport/inset.
    void ApplyContentClip();

    ICompositionBackend* backend_ = nullptr;  // borrowed
    std::unique_ptr<ICompositionVisual> viewport_;  // clipped container
    std::unique_ptr<ICompositionVisual> clip_;      // masks content to the region
    std::unique_ptr<ICompositionVisual> content_;   // scrolled surface
    std::unique_ptr<ICompositionVisual> overlay_;   // scrollbar/focus (static)
    std::unique_ptr<ICompositionVisual> caret_;     // text caret (child of content_)

    float dpiScale_ = 1.0f;
    // True while the window is inside a modal move/resize loop (see UIContext.h
    // comment for why a composited control cares). Cached from UIContext during
    // SetViewport so RasterizeSurface / RedrawOverlay can check it without a context
    // pointer. Used to defer BeginDraw until WM_EXITSIZEMOVE converges.
    bool inModalResize_ = false;
    RectDip viewportDip_{};
    Inset inset_{};
    // Ancestor viewport clip in window DIPs (see SetAncestorClip). When absent the
    // viewport clips to its own bounds only.
    RectDip ancestorClip_{};
    bool hasAncestorClip_ = false;
    float contentHeight_ = 0.0f;
    // Sub-tree opacity on viewport_. Cached so a device-loss rebuild restores it
    // (the visuals are recreated from scratch and default to 1.0).
    float opacity_ = 1.0f;
    // Desired logical-tree visibility and actual compositor-root membership are
    // separate: device loss drops the latter while the former must survive so a
    // hidden control is not accidentally re-parented during restore.
    bool treeVisible_ = true;
    bool rootAttached_ = false;
    // What the content surface is cleared to before each draw (see
    // SetSurfaceClearColor). Transparent by default.
    D2D1_COLOR_F surfaceClear_{0.0f, 0.0f, 0.0f, 0.0f};

    // Surface coverage (DIPs). surfaceWidth_/surfaceHeight_ = quantized viewport +
    // overscan (capped, 256px quanta); surfaceOrigin_ = content-space Y at top.
    float surfaceOrigin_ = 0.0f;
    float surfaceWidth_ = 0.0f;
    float surfaceHeight_ = 0.0f;
    bool contentDrawn_ = false;  // a content surface has been rasterized
    bool overlayDrawn_ = false;

    // Smooth-scroll tween state (offset space, DIPs). EffectiveOffset() evaluates
    // DecelerateCubic(animFrom_, targetOffset_, animDuration_) at now-animStartQpc_.
    float targetOffset_ = 0.0f;
    float animFrom_ = 0.0f;
    double animDuration_ = 0.0;
    int64_t animStartQpc_ = 0;
    bool animating_ = false;

    // Caret state (content-space DIPs; re-applied on DPI change / rebase).
    RectDip caretRect_{};
    D2D1_COLOR_F caretColor_{};
    bool caretVisible_ = false;
    bool caretConfigured_ = false;  // SetCaret was called (survives device loss)
    bool caretDrawn_ = false;      // surface rasterized at the cached size/color
    uint32_t caretDrawnW_ = 0, caretDrawnH_ = 0;
    double caretBlinkHalfSec_ = 0.0;  // >0 while a blink animation is running
};

} // namespace fluent
