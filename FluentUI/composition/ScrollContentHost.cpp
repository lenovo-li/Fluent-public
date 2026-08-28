// ScrollContentHost.cpp — see header. Overscan surface + compositor translation.

#include "ScrollContentHost.h"
#include "../animation/Animation.h"                // DecelerateCubic, kScrollDuration
#include "../diagnostics/PerformanceCounters.h"    // QpcNow / QpcFrequency
#include "../diagnostics/LayoutCostProbe.h"        // Scope, LayoutCostKey
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
const char* kTag = "ScrollContent";

// Overscan margin above AND below the viewport, as a fraction of the viewport
// height, and an absolute cap so a huge viewport doesn't allocate an enormous
// surface. Total surface = viewport * (1 + 2*frac), capped.
// 1.5 rather than 1.0: a fast wheel fling can travel further than one viewport
// before the tween settles, and the surface cannot be refilled while the compositor
// owns OffsetY (see EnsureContent). Raising this is memory-for-coverage — the
// surface is region + 2*overscan tall — so it is kept moderate and paired with the
// midpoint centering in AnimateTo, which buys another 2x for free.
constexpr float kOverscanFrac = 1.5f;      // 1.5 viewports above + 1.5 below
constexpr float kMaxOverscanDip = 1800.0f; // cap each side (scaled with frac)
// Refill when the effective offset comes within this fraction of the drawn edge.
constexpr float kRefillMarginFrac = 0.35f;

float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

HRESULT ScrollContentHost::Create(ICompositionBackend* backend, float dpiScale) {
    if (!backend) return E_INVALIDARG;
    backend_ = backend;
    dpiScale_ = dpiScale > 0.0f ? dpiScale : 1.0f;
    viewport_ = backend_->CreateVisual();
    clip_ = backend_->CreateVisual();
    content_ = backend_->CreateVisual();
    overlay_ = backend_->CreateVisual();
    if (!viewport_ || !clip_ || !content_ || !overlay_) {
        TraceMsg(kTag, "Create: CreateVisual failed");
        Destroy();
        return E_FAIL;
    }
    // clip_ (holding the scrolled content) below overlay so the scrollbar / focus ring
    // draw on top. See the header for why the content needs its own clip node.
    clip_->AddChild(content_.get());
    viewport_->AddChild(clip_.get());
    viewport_->AddChild(overlay_.get());
    if (treeVisible_) {
        backend_->AddToRoot(viewport_.get());
        rootAttached_ = true;
    }
    // Fresh visuals default to opaque; re-assert the cached opacity so a
    // device-loss rebuild (OnDeviceRestored -> Create) does not silently reset it.
    viewport_->SetOpacity(opacity_);
    ApplyContentClip();
    return S_OK;
}

void ScrollContentHost::SetOpacity(float opacity) {
    const float v = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity_ == v) return;
    opacity_ = v;
    if (!viewport_ || !treeVisible_) return;  // cached; applied on create/reveal
    // One property on the container visual: the compositor fades the composed
    // result (content + overlay + caret) in one step, so no surface is redrawn and
    // overlapping strokes inside the sub-tree do not show through each other.
    viewport_->SetOpacity(opacity_);
    if (backend_) backend_->RequestCommit();
}

void ScrollContentHost::SetTreeVisible(bool visible) {
    if (treeVisible_ == visible) return;
    treeVisible_ = visible;

    if (!visible) {
        // Preserve the position currently shown rather than jumping to the tween's
        // destination when the control is revealed again.
        targetOffset_ = Clampf(EffectiveOffset(), 0.0f, MaxOffset());
        animating_ = false;
        if (content_) {
            content_->StopAnimation(CompositionProperty::OffsetY);
            ApplyContentOffset(targetOffset_);
        }
        // The configured blink period is retained, but no infinite compositor
        // animation is allowed to survive in a collapsed subtree.
        if (caret_) caret_->StopAnimation(CompositionProperty::Opacity);
        if (backend_ && viewport_ && rootAttached_) {
            backend_->RemoveFromRoot(viewport_.get());
            rootAttached_ = false;
            backend_->RequestCommit();
        }
        contentDrawn_ = false;
        overlayDrawn_ = false;
        return;
    }

    if (!backend_ || !viewport_) return;
    if (!rootAttached_) {
        backend_->AddToRoot(viewport_.get());
        rootAttached_ = true;
    }
    viewport_->SetOpacity(opacity_);
    SetViewport(viewportDip_);
    ApplyContentOffset(targetOffset_);
    if (caretConfigured_ && !caret_) SetCaret(caretRect_, caretColor_);
    if (caret_) {
        ApplyCaretGeometry();
        caret_->SetOpacity(caretVisible_ ? 1.0f : 0.0f);
        if (caretVisible_ && caretBlinkHalfSec_ > 0.0)
            caret_->StartOpacityBlink(caretBlinkHalfSec_);
    }
    contentDrawn_ = false;
    overlayDrawn_ = false;
    backend_->RequestCommit();
}

void ScrollContentHost::Destroy() {
    DestroyCaret();  // unparent from content_ while it is still alive
    if (backend_ && viewport_ && rootAttached_) backend_->RemoveFromRoot(viewport_.get());
    if (clip_ && content_) clip_->RemoveChild(content_.get());
    if (viewport_ && clip_) viewport_->RemoveChild(clip_.get());
    if (viewport_ && overlay_) viewport_->RemoveChild(overlay_.get());
    overlay_.reset();
    content_.reset();
    clip_.reset();
    viewport_.reset();
    backend_ = nullptr;
    rootAttached_ = false;
    contentDrawn_ = overlayDrawn_ = false;
    animating_ = false;
}

RectDip ScrollContentHost::ContentRegion() const {
    return {viewportDip_.x + inset_.left,
            viewportDip_.y + inset_.top,
            std::max(1.0f, viewportDip_.w - inset_.left - inset_.right),
            std::max(1.0f, viewportDip_.h - inset_.top - inset_.bottom)};
}

float ScrollContentHost::MaxOffset() const {
    return std::max(0.0f, contentHeight_ - ContentRegion().h);
}

void ScrollContentHost::SetContentInset(const Inset& inset) {
    const bool changed =
        std::fabs(inset.left - inset_.left) > 0.01f ||
        std::fabs(inset.top - inset_.top) > 0.01f ||
        std::fabs(inset.right - inset_.right) > 0.01f ||
        std::fabs(inset.bottom - inset_.bottom) > 0.01f;
    inset_ = inset;
    if (!changed) return;
    UpdateSurfaceMetrics();      // overscan follows the region height
    contentDrawn_ = false;       // surface width/height changed
    ApplyContentClip();
    targetOffset_ = Clampf(targetOffset_, 0.0f, MaxOffset());
}

void ScrollContentHost::ApplyContentClip() {
    if (!clip_) return;
    const RectDip r = ContentRegion();
    const float s = dpiScale_;
    // clip_ sits at the region's top-left INSIDE the viewport container, and masks to
    // the region's size. content_ is its child, so content offsets stay measured from
    // the region — the inset is invisible to the caller's coordinates. Whole pixels:
    // a fractional clip edge is resampled by DWM and shimmers.
    clip_->SetOffset(std::round(inset_.left * s), std::round(inset_.top * s));
    clip_->SetClip(0.0f, 0.0f, std::round(r.w * s), std::round(r.h * s));
}

void ScrollContentHost::SetDpiScale(float dpiScale) {
    const float next = dpiScale > 0.0f ? dpiScale : 1.0f;
    if (std::fabs(next - dpiScale_) <= 0.0001f) return;

    // A running DComp tween is expressed in OLD physical pixels. Continuing it
    // after a monitor transition moves the content by the old scale and produces
    // exactly the cross-monitor drift this method must prevent. Settle at the
    // currently visible logical offset, then rebuild all pixel geometry below.
    const float effective = EffectiveOffset();
    animating_ = false;
    targetOffset_ = Clampf(effective, 0.0f, MaxOffset());
    if (content_) content_->StopAnimation(CompositionProperty::OffsetY);

    dpiScale_ = next;
    // Force a resize/redraw on the next EnsureContent by invalidating coverage.
    contentDrawn_ = false;
    overlayDrawn_ = false;
    caretDrawn_ = false;   // caret bar is sized in pixels
    UpdateSurfaceMetrics();
    // Position and clips are pixel-space too. Re-apply them immediately even
    // though their DIP values did not change across monitors.
    SetViewport(viewportDip_);
    ApplyContentOffset(targetOffset_);
    ApplyCaretGeometry();
    if (backend_ && treeVisible_) backend_->RequestCommit();
}

void ScrollContentHost::SetModalResize(bool inModalResize) {
    if (inModalResize == inModalResize_) return;
    inModalResize_ = inModalResize;
    // FALLING EDGE: mark both surfaces stale so the caller's next RefreshComposition
    // converges to the exact final size. Without this the pixels drawn before the drag
    // began would persist: every rasterization in between was skipped, and a drag that
    // ends on the same QUANTIZED surface size clears no dirty flag by itself
    // (SetViewport only clears contentDrawn_ when the quantized surface resized).
    if (!inModalResize_) {
        contentDrawn_ = false;
        overlayDrawn_ = false;
    }
}

void ScrollContentHost::UpdateSurfaceMetrics() {
    const RectDip region = ContentRegion();
    const float overscan = std::min(region.h * kOverscanFrac, kMaxOverscanDip);
    float rawH = region.h + 2.0f * overscan;
    // Quantize to 256px boundaries (on resize): reuse oversized surface on shrink.
    // Eliminates the 14-30ms CreateSurface stalls that cause visible jank. The
    // unused region is clipped by ApplyContentClip, so zero visual impact.
    constexpr float kQuantum = 256.0f;
    surfaceWidth_ = std::ceil(region.w / kQuantum) * kQuantum;
    surfaceHeight_ = std::ceil(rawH / kQuantum) * kQuantum;
}

void ScrollContentHost::SetViewport(const RectDip& v) {
    const bool sizeChanged =
        std::fabs(v.w - viewportDip_.w) > 0.5f || std::fabs(v.h - viewportDip_.h) > 0.5f;
    const float oldW = surfaceWidth_, oldH = surfaceHeight_;
    viewportDip_ = v;
    UpdateSurfaceMetrics();
    const bool surfaceResized =
        std::fabs(surfaceWidth_ - oldW) > 0.5f || std::fabs(surfaceHeight_ - oldH) > 0.5f;
    if (!viewport_) return;
    const float s = dpiScale_;
    // Snap the container position + clip to WHOLE pixels. A composition visual at a
    // fractional pixel offset is resampled by DWM, which shimmers the edges (worst
    // at the focus ring) and amplifies the per-frame jitter during a resize drag.
    //
    // Position the container at its TRUE window position, negative y included. An
    // earlier revision clamped this to (0,0) and shifted the clip to compensate; that
    // could only ever express "clip at the window origin", never "clip at the
    // ancestor viewport's top" (y=48 under a title bar). The clip was also measured
    // from the clamped position rather than the real one, which pinned the whole
    // sub-tree to the clip edge and dragged it along as the user scrolled.
    //
    viewport_->SetOffset(std::round(v.x * s), std::round(v.y * s));

    // Clip in the visual's OWN space: subtract the container's true position from the
    // ancestor clip to convert window coordinates to local ones. Default (no ancestor
    // clip) is the full viewport.
    float localL = 0.0f, localT = 0.0f, localR = v.w, localB = v.h;
    if (hasAncestorClip_) {
        localL = std::max(0.0f, ancestorClip_.x - v.x);
        localT = std::max(0.0f, ancestorClip_.y - v.y);
        localR = std::min(v.w, ancestorClip_.right() - v.x);
        localB = std::min(v.h, ancestorClip_.bottom() - v.y);
        // Fully outside: collapse to an empty clip so nothing composites.
        if (localR < localL) localR = localL;
        if (localB < localT) localB = localT;
    }
    viewport_->SetClip(std::round(localL * s), std::round(localT * s),
                       std::round(localR * s), std::round(localB * s));
    ApplyContentClip();   // the region moved/resized with the viewport
    // Invalidate the CONTENT surface only when the quantized surface actually
    // changed size. This is the whole point of the quantization: a resize drag walks
    // the viewport through hundreds of sizes but crosses a 256px boundary only a
    // handful of times, so the drawn content is reused for most frames (the clip
    // above already narrows it to the new region). Keying this off sizeChanged
    // instead would re-rasterize every frame and quantization would buy nothing.
    if (surfaceResized) contentDrawn_ = false;
    // The OVERLAY (scrollbar) is sized to the viewport, not to the quantized
    // surface, so it must still follow every viewport size change.
    if (sizeChanged) overlayDrawn_ = false;
}

void ScrollContentHost::SetAncestorClip(const RectDip& clipWindowDip) {
    ancestorClip_ = clipWindowDip;
    hasAncestorClip_ = true;
    if (viewport_) SetViewport(viewportDip_);  // re-apply with the new clip
}

void ScrollContentHost::ClearAncestorClip() {
    if (!hasAncestorClip_) return;
    hasAncestorClip_ = false;
    if (viewport_) SetViewport(viewportDip_);
}

void ScrollContentHost::SetSurfaceClearColor(const D2D1_COLOR_F& color) {
    const bool changed = std::fabs(color.r - surfaceClear_.r) > 0.001f ||
                         std::fabs(color.g - surfaceClear_.g) > 0.001f ||
                         std::fabs(color.b - surfaceClear_.b) > 0.001f ||
                         std::fabs(color.a - surfaceClear_.a) > 0.001f;
    if (!changed) return;
    surfaceClear_ = color;
    // The drawn pixels carry the old base, so they have to be re-rasterized. The
    // caller's next EnsureContent does that once this flag is down.
    contentDrawn_ = false;
}

void ScrollContentHost::SetContentHeight(float h) {
    contentHeight_ = std::max(0.0f, h);
    // Re-clamp the target/effective offset to the new extent.
    targetOffset_ = Clampf(targetOffset_, 0.0f, MaxOffset());
}

void ScrollContentHost::ApplyContentOffset(float effectiveOffsetDip) {
    if (!content_) return;
    // content OffsetY (px) = (surfaceOrigin - effectiveOffset) * dpi. The surface
    // top sits at content-space surfaceOrigin_; sliding the surface up by the
    // amount the view has scrolled past that origin puts the right rows in view.
    // Snap to a whole pixel so a static (non-animating) offset does not land the
    // surface on a fractional pixel (edge shimmer). During a tween the compositor
    // owns OffsetY and this static write is not used.
    const float offsetYpx = std::round((surfaceOrigin_ - effectiveOffsetDip) * dpiScale_);
    content_->SetOffset(0.0f, offsetYpx);
}

float ScrollContentHost::EffectiveOffset() {
    if (!animating_) return targetOffset_;
    const int64_t freq = QpcFrequency();
    if (freq <= 0) { animating_ = false; return targetOffset_; }
    const double t = static_cast<double>(QpcNow() - animStartQpc_) /
                     static_cast<double>(freq);
    if (t >= animDuration_) { animating_ = false; return targetOffset_; }
    const DecelerateCubic k =
        DecelerateCubic::Make(animFrom_, targetOffset_, animDuration_);
    return static_cast<float>(k.Eval(t));
}

bool ScrollContentHost::NeedsRefill(float offsetDip) const {
    if (!contentDrawn_) return true;
    // The view spans [offset, offset+viewportH]. Refill if either edge is within
    // the refill margin of the drawn surface edge — but never ask to draw above
    // content 0 or below the max (those edges are legitimately at the buffer end).
    const float regionH = ContentRegion().h;
    const float margin = std::max(regionH * kRefillMarginFrac, 1.0f);
    const float viewTop = offsetDip;
    const float viewBottom = offsetDip + regionH;
    const float surfTop = surfaceOrigin_;
    const float surfBottom = surfaceOrigin_ + surfaceHeight_;
    if (viewTop - surfTop < margin && surfTop > 0.0f) return true;
    if (surfBottom - viewBottom < margin && surfBottom < contentHeight_) return true;
    return false;
}

void ScrollContentHost::RasterizeSurface(const DrawContentCallback& drawContent) {
    if (!content_ || !treeVisible_) return;
    // DEFER surface redraw while the user drags a resize border. BeginDraw is a GPU
    // sync point (waits for the compositor to release the surface), measured at 12ms
    // in the worst frame on the Gallery TextArea page — 61% of a 20.9ms budget. The
    // compositor can translate/clip the EXISTING pixels smoothly without our help, so
    // skip the map and converge on WM_EXITSIZEMOVE (where inModalResize → false).
    if (inModalResize_) return;
    const uint32_t wpx = static_cast<uint32_t>(surfaceWidth_ * dpiScale_ + 0.5f);
    const uint32_t hpx = static_cast<uint32_t>(surfaceHeight_ * dpiScale_ + 0.5f);
    if (wpx == 0 || hpx == 0) return;
    const float originDip = surfaceOrigin_;
    const float heightDip = surfaceHeight_;
    const float s = dpiScale_;
    content_->DrawSurface(wpx, hpx,
        [this, &drawContent, originDip, heightDip, s](ID2D1DeviceContext* dc, float ax,
                                                float ay) {
            if (!dc || !drawContent) return;
            // Draw in DIPs with (0,0) at surfaceOrigin: scale to DIP space, then the
            // callback paints content rows at local y = rowY - surfaceOrigin.
            // The atlas tile origin (ax,ay) MUST stay in the transform — see the
            // transform contract on ICompositionVisual::DrawSurface. Row-vector order:
            // scale first, then translate into the tile.
            dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                             D2D1::Matrix3x2F::Translation(ax, ay));
            dc->Clear(surfaceClear_);
            // Attribute the CONTENT half of DCompDrawCallback. That key wraps the whole
            // callback in the backend and so mixes rows with the scrollbar overlay; the
            // two have different fixes (virtualized row drawing vs. deferring the
            // scrollbar rasterization), and one flat number cannot tell them apart.
            // Nested inside DCompDrawCallback, so it explains part of it, never adds.
            LayoutCostProbe::Scope probe(LayoutCostKey::DCompDrawContent);
            drawContent(dc, originDip, heightDip);
        });
    contentDrawn_ = true;
}

void ScrollContentHost::Rebase(float centerOffsetDip,
                               const DrawContentCallback& drawContent) {
    // Center the surface on the view around centerOffsetDip, clamped so the surface
    // never starts above content 0 (no point drawing empty space above the top).
    const float overscan = (surfaceHeight_ - ContentRegion().h) * 0.5f;
    float newOrigin = centerOffsetDip - overscan;
    const float maxOrigin = std::max(0.0f, contentHeight_ - surfaceHeight_);
    surfaceOrigin_ = Clampf(newOrigin, 0.0f, maxOrigin);
    RasterizeSurface(drawContent);
    // The caret's offset is measured from the surface top, which just moved.
    ApplyCaretGeometry();
}

void ScrollContentHost::EnsureContent(const DrawContentCallback& drawContent,
                                      bool forceRedraw) {
    if (!content_ || !treeVisible_) return;
    // While a compositor tween runs it OWNS content OffsetY. Rebasing (which resets
    // surfaceOrigin_) or writing a static offset here would break the running
    // animation, so leave it untouched until it settles. The buffer was centered on
    // the target when the tween started, so it covers the common short fling; a very
    // long fling that outruns the buffer briefly shows blank and self-heals on the
    // settle refill (roadmap §11.2, an accepted overscan limit).
    if (animating_) {
        (void)EffectiveOffset();  // lazily clears animating_ once the tween is done
        if (animating_) return;
    }
    const float eff = EffectiveOffset();
    if (NeedsRefill(eff)) {
        Rebase(eff, drawContent);          // near a buffer edge: recenter + redraw
    } else if (forceRedraw) {
        RasterizeSurface(drawContent);     // content changed (selection/items/theme):
                                           // redraw at the CURRENT origin, no recenter
    }
    ApplyContentOffset(eff);
}

void ScrollContentHost::AnimateTo(float offsetDip,
                                  const DrawContentCallback& drawContent) {
    const float clamped = Clampf(offsetDip, 0.0f, MaxOffset());
    if (!treeVisible_) {
        animating_ = false;
        targetOffset_ = clamped;
        return;
    }
    const float current = EffectiveOffset();  // seed from where we visibly are
    targetOffset_ = clamped;
    if (std::fabs(clamped - current) < 0.5f) {
        // Nothing meaningful to animate; settle now.
        animating_ = false;
        if (NeedsRefill(clamped)) Rebase(clamped, drawContent);
        ApplyContentOffset(clamped);
        if (backend_) backend_->RequestCommit();
        return;
    }
    // Make sure the surface covers both endpoints of the fling before handing the
    // motion to the compositor (it can't refill while the UI thread is away).
    animFrom_ = current;
    animDuration_ = kScrollDuration;
    animStartQpc_ = QpcNow();
    animating_ = true;
    // Rebase centered on the fling MIDPOINT, not the target. A surface centered on
    // the target covers only `overscan` DIP of backward travel — under repeated fast
    // wheel notches each retarget pushes the destination further ahead, and the view
    // (still near `current`) slides past the undrawn side of the buffer, showing
    // blank mid-fling (the reported fast-wheel symptom). The midpoint covers both
    // endpoints plus the whole path, doubling coverage at no memory cost. A second
    // notch arriving mid-fling recentres on the new midpoint, extending again.
    Rebase((current + clamped) * 0.5f, drawContent);
    // Drive the compositor tween in OffsetY (pixel) space. from/to map through the
    // same affine offset→OffsetY relation as ApplyContentOffset.
    if (content_) {
        const float fromYpx = (surfaceOrigin_ - current) * dpiScale_;
        const float toYpx = (surfaceOrigin_ - clamped) * dpiScale_;
        content_->StartOffsetYTween(fromYpx, toYpx, animDuration_);
    }
    if (backend_) backend_->RequestCommit();
}

void ScrollContentHost::SetOffsetImmediate(float offsetDip,
                                           const DrawContentCallback& drawContent) {
    const float clamped = Clampf(offsetDip, 0.0f, MaxOffset());
    animating_ = false;
    targetOffset_ = clamped;
    if (!treeVisible_) return;
    if (NeedsRefill(clamped)) Rebase(clamped, drawContent);
    ApplyContentOffset(clamped);  // static offset cancels the compositor tween
    if (backend_) backend_->RequestCommit();
}

void ScrollContentHost::RedrawOverlay(const DrawOverlayCallback& drawOverlay) {
    if (!overlay_ || !treeVisible_) return;
    // Same deferral as RasterizeSurface, and for the same BeginDraw reason. The
    // overlay's own DRAWING is cheap (measured 0.14ms of a 20.9ms frame), but every
    // overlay surface is one more BeginDraw — six of the eleven in that frame — so the
    // cost being avoided here is the map, not the painting.
    if (inModalResize_) return;
    const uint32_t wpx = static_cast<uint32_t>(viewportDip_.w * dpiScale_ + 0.5f);
    const uint32_t hpx = static_cast<uint32_t>(viewportDip_.h * dpiScale_ + 0.5f);
    if (wpx == 0 || hpx == 0) return;
    const float wDip = viewportDip_.w;
    const float hDip = viewportDip_.h;
    overlay_->DrawSurface(wpx, hpx,
        [&drawOverlay, wDip, hDip](ID2D1DeviceContext* dc, float ax, float ay) {
            if (!dc || !drawOverlay) return;
            // Leave ONLY the atlas tile translation as the base transform. Unlike the
            // content callback, the overlay drawer maps window DIPs → viewport-local
            // pixels itself, so it supplies its own scale and PREMULTIPLIES it onto
            // this base (see the transform contract on DrawSurface). Setting the scale
            // here too would double-scale it.
            dc->SetTransform(D2D1::Matrix3x2F::Translation(ax, ay));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            // The OVERLAY half of DCompDrawCallback — see the sibling probe in
            // RasterizeSurface for why the split exists.
            LayoutCostProbe::Scope probe(LayoutCostKey::DCompDrawOverlay);
            drawOverlay(dc, wDip, hDip);
        });
    overlayDrawn_ = true;
}

// ---------------------------------------------------------------------------
// Text caret (child of the content visual — inherits the scroll offset)
// ---------------------------------------------------------------------------

void ScrollContentHost::ApplyCaretGeometry() {
    if (!caret_) return;
    // The caret's parent is the CONTENT visual, whose origin is the surface's
    // top-left = content-space surfaceOrigin_. So a caret at content-space Y sits at
    // local (Y - surfaceOrigin_) — which is why this must be re-applied whenever a
    // rebase moves surfaceOrigin_. Snap to whole pixels: a composition visual on a
    // fractional pixel is resampled by DWM, and a 1-DIP-wide caret resampled across
    // two columns looks blurry and appears to wobble as it moves.
    const float s = dpiScale_;
    caret_->SetOffset(std::round(caretRect_.x * s),
                      std::round((caretRect_.y - surfaceOrigin_) * s));

    const uint32_t wpx = static_cast<uint32_t>(std::max(1.0f, caretRect_.w * s) + 0.5f);
    const uint32_t hpx = static_cast<uint32_t>(std::max(1.0f, caretRect_.h * s) + 0.5f);
    if (!caretDrawn_ || wpx != caretDrawnW_ || hpx != caretDrawnH_) {
        const D2D1_COLOR_F color = caretColor_;
        caret_->DrawSurface(wpx, hpx,
            [color](ID2D1DeviceContext* dc, float ax, float ay) {
                if (!dc) return;
                // Keep the atlas tile translation (see the DrawSurface contract) —
                // dropping it would draw the caret into a neighbouring tile.
                dc->SetTransform(D2D1::Matrix3x2F::Translation(ax, ay));
                dc->Clear(color);   // the whole tile IS the caret bar
            });
        caretDrawn_ = true;
        caretDrawnW_ = wpx;
        caretDrawnH_ = hpx;
    }
}

void ScrollContentHost::SetCaret(const RectDip& rectContentDip,
                                 const D2D1_COLOR_F& color) {
    const bool colorChanged =
        color.r != caretColor_.r || color.g != caretColor_.g ||
        color.b != caretColor_.b || color.a != caretColor_.a;
    caretRect_ = rectContentDip;
    caretColor_ = color;
    caretConfigured_ = true;
    if (!content_ || !treeVisible_) return;
    if (colorChanged) caretDrawn_ = false;   // theme switch: re-fill the bar

    if (!caret_) {
        caret_ = backend_ ? backend_->CreateVisual() : nullptr;
        if (!caret_) {
            TraceMsg(kTag, "SetCaret: CreateVisual failed");
            return;
        }
        // Parent under content_ so it rides the compositor scroll tween.
        content_->AddChild(caret_.get());
        caret_->SetOpacity(caretVisible_ ? 1.0f : 0.0f);
    }
    ApplyCaretGeometry();
}

void ScrollContentHost::SetCaretVisible(bool visible) {
    if (caretVisible_ == visible) return;
    caretVisible_ = visible;
    if (!caret_ || !treeVisible_) return;
    if (visible) {
        // Re-arm the blink if one was configured; otherwise just show it solid.
        if (caretBlinkHalfSec_ > 0.0) caret_->StartOpacityBlink(caretBlinkHalfSec_);
        else caret_->SetOpacity(1.0f);
    } else {
        caret_->SetOpacity(0.0f);  // a static opacity drops the blink animation
    }
}

void ScrollContentHost::StartCaretBlink(double halfPeriodSec) {
    caretBlinkHalfSec_ = halfPeriodSec > 0.0 ? halfPeriodSec : 0.0;
    if (!treeVisible_ || !caret_ || !caretVisible_ || caretBlinkHalfSec_ <= 0.0) return;
    // Restarting resets the phase to "solid" — what a keystroke should do.
    caret_->StartOpacityBlink(caretBlinkHalfSec_);
}

void ScrollContentHost::StopCaretBlink() {
    caretBlinkHalfSec_ = 0.0;
    if (caret_) caret_->SetOpacity(caretVisible_ ? 1.0f : 0.0f);
}

// Drop the caret VISUAL. Note the cached rect/colour/blink period survive, so
// OnDeviceRestored can rebuild an identical caret; a caller that wants the caret gone
// for good just stops calling SetCaret (or the host is destroyed outright).
void ScrollContentHost::DestroyCaret() {
    if (content_ && caret_) content_->RemoveChild(caret_.get());
    caret_.reset();
    caretDrawn_ = false;
    caretDrawnW_ = caretDrawnH_ = 0;
}

void ScrollContentHost::OnDeviceLost() {
    // Device gone: drop visuals but KEEP the scroll state (targetOffset_,
    // surfaceOrigin_) so OnDeviceRestored can put the user back where they were.
    // The caret's RECT/colour/blink period are likewise kept (they are plain data) so
    // a restore re-creates an identical caret; only its visual goes.
    DestroyCaret();
    // Unparent first (RemoveFromRoot/RemoveChild are null-safe) so the backend's
    // child list stays consistent, exactly like Destroy().
    if (backend_ && viewport_ && rootAttached_) backend_->RemoveFromRoot(viewport_.get());
    if (clip_ && content_) clip_->RemoveChild(content_.get());
    if (viewport_ && clip_) viewport_->RemoveChild(clip_.get());
    if (viewport_ && overlay_) viewport_->RemoveChild(overlay_.get());
    overlay_.reset();
    content_.reset();
    clip_.reset();
    viewport_.reset();
    backend_ = nullptr;
    rootAttached_ = false;
    contentDrawn_ = false;
    overlayDrawn_ = false;
    animating_ = false;  // a mid-flight tween does not survive device loss
}

HRESULT ScrollContentHost::OnDeviceRestored(ICompositionBackend* backend,
                                            float dpiScale) {
    const bool hadCaret = caretConfigured_;
    HRESULT hr = Create(backend, dpiScale);
    if (FAILED(hr)) return hr;
    if (!treeVisible_) return S_OK;
    // Re-apply geometry; the control re-supplies draw callbacks on its next paint,
    // which will refill the content surface and redraw the overlay.
    SetViewport(viewportDip_);
    ApplyContentOffset(targetOffset_);
    if (hadCaret) {
        // Rebuild the caret from the retained rect/colour and re-arm its blink, so a
        // device loss while typing does not leave the caret gone.
        caretDrawn_ = false;
        SetCaret(caretRect_, caretColor_);
        if (caret_) caret_->SetOpacity(caretVisible_ ? 1.0f : 0.0f);
        if (caretBlinkHalfSec_ > 0.0) StartCaretBlink(caretBlinkHalfSec_);
    }
    return S_OK;
}

} // namespace fluent
