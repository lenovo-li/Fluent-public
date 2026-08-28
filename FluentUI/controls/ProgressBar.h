// ProgressBar.h — Fluent progress bar (determinate + indeterminate).
//
// Determinate: a rounded track with an accent fill whose width eases to the
// current value via the host's per-frame tick. Indeterminate: a short accent
// segment sweeps back and forth continuously (a busy indicator).
//
// ProgressBar is a RangeBase (roadmap §WP-06): RangeBase owns min_/max_/value_
// and the coerce/change pipeline; ProgressBar fixes the range to [0,1] in its
// constructor and adds indeterminate sweep animation + drawing.
#pragma once

#include "primitives/RangeBase.h"
#include "../animation/AnimatedValue.h"
#include "ProgressSweepVisual.h"
#include <memory>

namespace fluent {

class ProgressBar : public RangeBase {
public:
    ProgressBar() {
        // Fixed 0..1 range; the base holds min_/max_ so no public setters needed.
        min_ = 0.0f;
        max_ = 1.0f;
    }

    // Setting a value also switches to determinate mode.
    void SetValue(float v) { SetIndeterminate(false); RangeBase::SetValue(v); }

    // Indeterminate "busy" mode: a segment sweeps back and forth.
    void SetIndeterminate(bool on) {
        if (indeterminate_ == on) return;
        indeterminate_ = on;
        sweep_.SetImmediate(0.0f);
        UpdateSweepMode();  // start/stop the compositor sweep (Plan B)
        Invalidate();
    }
    bool IsIndeterminate() const { return indeterminate_; }

    bool WantsAnimationTick() const override;
    // Adopt the initial state outright on first layout rather than easing into it,
    // so a control built already-set does not animate the first time it is shown.
    void SnapAnimationsToSettledState() override { animFill_.SetImmediate(value_); }
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;

protected:
    // Plan B lifecycle: the indeterminate sweep runs on the DirectComposition
    // compositor thread (ProgressSweepVisual). These hooks keep that visual
    // created, positioned, themed, and torn down in step with the tree/device.
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnBoundsChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnDeviceLost() override;
    void OnDeviceRestored() override;
    void OnVisibilityChanged(bool visible) override;
    void OnAncestorVisibilityChanged() override;

private:
    // True when the sweep is actually running on the compositor (indeterminate +
    // attached + a composition device was available). When false, indeterminate
    // mode falls back to the historical UI-thread sweep (sweep_/OnAnimationTick).
    bool CompositorSweeping() const { return compositorActive_; }
    // Reconcile the sweep visual with the current mode/attachment/device: create +
    // start it when it should run, stop + detach it otherwise. Idempotent.
    void UpdateSweepMode();
    // Push current bounds/DPI/theme into the sweep visual (reposition + reclip +
    // phase-continuous range re-fit; pill redrawn only when its size changed).
    // `startSweep` also kicks off a fresh sweep — true only when entering the mode.
    void SyncSweepGeometry(bool startSweep);
    // Compute the track rect + accent in physical pixels from bounds + DPI.
    void TrackGeometryPx(float& ox, float& oy, float& w, float& h, float& radius) const;

    AnimatedValue animFill_{0.0f}; // eased determinate fill, same units as value_
                                   // (OnAnimationTick approaches value_ directly, not a
                                   // normalised fraction; the [0,1] note here was stale)
    AnimatedValue sweep_{0.0f};    // 0..1 phase of the indeterminate sweep (fallback)
    bool indeterminate_ = false;
    bool compositorActive_ = false;
    // A StartSweep request that arrived while the bar had no visible area (scrolled
    // out of an ancestor viewport, or before the first Arrange). Held until real
    // geometry exists, since UpdateSweepMode only requests a start once per entry
    // into indeterminate mode. See ProgressBar::SyncSweepGeometry.
    bool sweepStartPending_ = false;
    std::unique_ptr<ProgressSweepVisual> sweepVisual_;
};

} // namespace fluent
