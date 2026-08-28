// ICompositionBackend.h — the compositor abstraction controls program against
// (Phase 2 of the compositor migration).
//
// The Plan B PoC (indeterminate ProgressBar sweep) reached DirectComposition
// through the concrete WindowServices device/visual accessors, so a control had
// to hold IDCompositionDevice2* / IDCompositionVisual2* / IDCompositionAnimation*
// directly and could not be exercised without a real GPU + window. This header
// collapses that surface to two interfaces:
//
//   * ICompositionVisual — one node in the compositor tree: set offset / opacity
//     / clip immediately, run an offset sweep on the compositor thread, draw its
//     backing surface with Direct2D, and parent child visuals.
//   * ICompositionBackend — the factory + root: create visuals, parent them above
//     the window content, request a commit.
//
// A control obtains the backend from WindowServices::Composition() (null on a
// host without composition or mid device-loss → the control falls back to its
// UI-thread path). Because a control never names a DComp type, its composition
// behaviour is testable headless against a fake backend (tests/framework).
//
// COORDINATE SPACE: physical PIXELS. Composition visuals are pixel-space; the
// owning control converts its DIP bounds via the current DPI scale, exactly as
// the raw-DComp PoC did.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

// Direct2D device context, forward-declared so this header pulls in no graphics
// headers. Only DrawSurface's callback names it; a real backend includes <d2d1.h>
// in its .cpp, a fake backend passes nullptr and the control skips drawing.
struct ID2D1DeviceContext;

namespace fluent {

// The animatable / settable properties. Phase 2 needs offset (ProgressBar sweep,
// future scroll translation) and opacity (future scrollbar fade). Scale / clip
// animation arrive with the scrollbar work (Phase 3+); clip is set immediately
// here via SetClip, not animated.
enum class CompositionProperty {
    OffsetX,
    OffsetY,
    Opacity,
};

// Parameters for an infinitely looping there-and-back offset sweep on the
// compositor thread (indeterminate ProgressBar). Mirrors MakeOffsetSweep:
//   OffsetX(t) = bias + amplitude*sin(360*(1/cycleSec)*t + phaseDeg)  [deg, Hz]
// with bias = amplitude = (maxX-minX)/2. phaseDeg = -90 starts at minX; a
// phase-continuous re-fit on resize passes the sweep's current phase so the
// segment does not jump when the travel range changes.
struct SweepSpec {
    float minX = 0.0f;
    float maxX = 0.0f;
    double cycleSec = 1.4;
    float phaseDeg = -90.0f;
};

// One node in the compositor tree. Owned by whoever created it (unique_ptr from
// ICompositionBackend::CreateVisual); destroying it releases the underlying
// compositor objects. All setters are cheap and do NOT commit — the owner calls
// ICompositionBackend::RequestCommit() once the batch of changes is ready.
class ICompositionVisual {
public:
    virtual ~ICompositionVisual() = default;

    // Immediate transform / appearance (physical pixels; opacity 0..1). Setting
    // an offset replaces any running animation on that axis with a static value.
    virtual void SetOffset(float x, float y) = 0;
    virtual void SetOpacity(float opacity) = 0;

    // Rectangular clip in the visual's own pixel space (children/content outside
    // are masked). ClearClip removes it.
    virtual void SetClip(float left, float top, float right, float bottom) = 0;
    virtual void ClearClip() = 0;

    // Start an infinite offset sweep on the compositor thread (OffsetX). Replaces
    // any prior animation/static value on that axis. Keeps running while the UI
    // thread is busy — the whole point of the abstraction.
    virtual void StartOffsetSweep(const SweepSpec& spec) = 0;

    // Start a one-shot decelerate OffsetY tween (physical pixels) on the compositor
    // thread — smooth scrolling that keeps settling even while the UI thread is
    // blocked (roadmap §11.6). Replaces any prior animation/static value on OffsetY.
    // The caller seeds `fromPx` with the CURRENT visual offset (evaluated via QPC)
    // so a mid-flight retarget does not jump. Holds `toPx` after `durationSec`.
    virtual void StartOffsetYTween(float fromPx, float toPx,
                                   double durationSec) = 0;

    // Start an INFINITE caret-blink square wave on OPACITY (solid for
    // `halfPeriodSec`, hidden for the same, forever) on the compositor thread — no
    // UI-thread timer, no repaint per blink. Replaces any prior opacity
    // animation/static value. Restart it to make the caret solid again immediately
    // after a keystroke (the phase restarts at "solid").
    virtual void StartOpacityBlink(double halfPeriodSec) = 0;

    // Drop the animation on `property` (the value freezes at its current point;
    // pair with SetOffset to pin it somewhere specific).
    virtual void StopAnimation(CompositionProperty property) = 0;

    // (Re)create this visual's backing surface at the given pixel size and draw
    // it via the callback. The callback receives a D2D device context already
    // translated to the surface origin, plus that origin (surfaceOffsetX/Y) in
    // case it needs the raw values. On a fake backend the context is nullptr and
    // the callback must tolerate that (it is only recording that a draw happened).
    // Returns false if the surface could not be created/bound.
    //
    // TRANSFORM CONTRACT — the surface is a TILE inside a shared atlas texture, and
    // the tile's origin (surfaceOffsetX/Y) CHANGES between draws. The incoming
    // transform already carries that translation, so a callback that needs its own
    // transform must PREMULTIPLY onto the existing one (GetTransform, then
    // `mine * base`), never replace it. Replacing it drops the atlas translation and
    // the drawing lands outside this visual's tile — clipped away or into a
    // neighbour's — which flickers frame to frame as tiles get reassigned.
    using DrawCallback =
        std::function<void(ID2D1DeviceContext* dc, float surfaceOffsetX,
                           float surfaceOffsetY)>;
    virtual bool DrawSurface(uint32_t pixelW, uint32_t pixelH,
                             const DrawCallback& draw) = 0;

    // Parent / unparent a child visual (this visual becomes its container, e.g.
    // a clipped track holding a moving segment). The child is owned by the caller.
    virtual void AddChild(ICompositionVisual* child) = 0;
    virtual void RemoveChild(ICompositionVisual* child) = 0;
};

// The compositor factory + root. Obtained from WindowServices::Composition().
class ICompositionBackend {
public:
    virtual ~ICompositionBackend() = default;

    // Create a detached visual (not yet parented). Returns null on failure.
    virtual std::unique_ptr<ICompositionVisual> CreateVisual() = 0;

    // Parent a visual above the window content / remove it. The caller owns the
    // visual and keeps it alive while it is on the root.
    virtual void AddToRoot(ICompositionVisual* visual) = 0;
    virtual void RemoveFromRoot(ICompositionVisual* visual) = 0;

    // Publish pending changes on the next window frame (coalesced with painting).
    virtual void RequestCommit() = 0;
};

} // namespace fluent
