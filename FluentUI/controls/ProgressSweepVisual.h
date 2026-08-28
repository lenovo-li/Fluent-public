// ProgressSweepVisual.h — the compositor-thread sweep for an indeterminate
// ProgressBar (Plan B / roadmap §9.3).
//
// An indeterminate ProgressBar shows a short accent segment sliding back and
// forth. Historically that motion was advanced on the UI thread every frame
// (ProgressBar::OnAnimationTick updated a phase, Render redrew the segment), so
// a busy UI thread — a window resize in particular — made it stutter.
//
// This helper moves the motion onto the compositor thread via ICompositionBackend
// (Phase 2): a clipped container visual positioned over the bar, holding a
// fixed-size accent segment surface whose OffsetX is driven by an infinitely
// looping sweep (SweepSpec → MakeOffsetSweep in the real backend). Once started,
// the compositor sweeps the segment on its own thread — it keeps moving even
// while the UI thread is blocked. The UI thread only draws the (static) track
// groove into the window content, and only touches this object on discrete events
// (bounds / DPI / theme / device-loss), never per frame.
//
// Coordinate space is physical PIXELS (composition visuals are pixel-space); the
// owning control converts its DIP bounds via the DPI scale. The control owns the
// lifetime and must call Destroy() before the backend goes away.
#pragma once

#include "../fl_common.h"
#include "../composition/ICompositionBackend.h"
#include <d2d1.h>
#include <cstdint>
#include <memory>

namespace fluent {

class ProgressSweepVisual {
public:
    // Build the container + segment visuals via the backend. `backend` comes from
    // WindowServices::Composition() (may be null → caller falls back to the
    // UI-thread sweep). Returns S_OK only when fully created.
    HRESULT Create(ICompositionBackend* backend);

    // Release all composition objects. Safe to call repeatedly; safe if never
    // created. The caller detaches the root from the backend root first (or relies
    // on backend teardown) — Destroy only drops this object's own visuals.
    void Destroy();

    bool Valid() const { return container_ != nullptr; }

    // Set the bar geometry (physical pixels): reposition + reclip the container,
    // and redraw the segment pill when its size or color changed. If the sweep is
    // currently running, the travel range is re-fitted to the new width PHASE-
    // CONTINUOUSLY — the replacement animation is seeded with the sweep's current
    // phase, so the segment does not jump; it just sweeps the new range from where
    // it is. Safe to call every frame during a resize drag. Called on attach /
    // resize / DPI / theme change.
    HRESULT SetGeometryPx(float originXpx, float originYpx, float trackWpx,
                          float trackHpx, D2D1_COLOR_F accent,
                          float cornerRadiusPx);

    // Force the NEXT SetGeometryPx to redraw the pill surface even if the size did
    // not change (used on a theme change, where only the accent color differs).
    void ForceRedrawNextGeometry() { forceRedraw_ = true; }

    // Start / stop the infinite compositor sweep. StartSweep begins a fresh sweep
    // (phase reset to the left edge) — call once when entering indeterminate mode,
    // NOT on resize (SetGeometryPx re-fits a running sweep without a reset). Stop
    // pins the segment off-screen. Neither commits — the caller requests a
    // composition commit so the change is published.
    void StartSweep(double cycleSec);
    void StopSweep();

    // The root visual to parent above the window content (backend AddToRoot).
    ICompositionVisual* Root() const { return container_.get(); }

private:
    HRESULT DrawSegment(D2D1_COLOR_F accent, float cornerRadiusPx);

    ICompositionBackend* backend_ = nullptr;  // borrowed; owns the compositor
    std::unique_ptr<ICompositionVisual> container_;
    std::unique_ptr<ICompositionVisual> segment_;
    float trackWpx_ = 0.0f;
    float trackHpx_ = 0.0f;
    float segWpx_ = 0.0f;
    // Sweep timing, to re-derive the current phase for a phase-continuous re-fit.
    bool sweeping_ = false;
    bool forceRedraw_ = false;  // theme change: redraw pill even without a resize
    bool segmentDrawn_ = false; // a surface has been drawn at least once
    double cycleSec_ = 1.4;
    int64_t startQpc_ = 0;  // QPC at StartSweep; drives CurrentPhaseDeg()
    // The sweep's current phase in DEGREES (sinusoid convention), from elapsed
    // time since startQpc_. Used to seed a re-fitted animation so it doesn't jump.
    float CurrentPhaseDeg() const;
};

} // namespace fluent
