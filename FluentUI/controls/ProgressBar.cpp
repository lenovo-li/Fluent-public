// ProgressBar.cpp

#include "ProgressBar.h"
#include "../styling/ThemeTokens.h"
#include "../window/WindowServices.h"
#include "../composition/ICompositionBackend.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kBarH    = 4.0f;    // track height (DIP)
constexpr float kAnimTau = 0.05f;   // determinate fill time constant (s)
constexpr float kSweepSec = 1.4f;   // one indeterminate sweep cycle (s)
constexpr float kSegFrac = 0.35f;   // indeterminate segment width as track frac
} // namespace

bool ProgressBar::WantsAnimationTick() const {
    if (!IsEffectivelyVisible()) return false;
    // When the indeterminate sweep runs on the compositor thread it needs NO
    // UI-thread tick (that is the whole point — it keeps moving while the UI
    // thread is busy). Only the fallback path (no composition device) or the
    // determinate fill ease still want per-frame ticks.
    if (indeterminate_) return !compositorActive_;
    // The determinate fill must not grow in from empty on first paint. Checked only on
    // this branch: the indeterminate sweep is a continuous animation with no settled state
    // to snap to, so priming does not apply to it.
    if (!AnimationPrimed()) return false;
    return animFill_.Animating(value_, 0.001f);
}

void ProgressBar::OnAnimationTick(float dtSec) {
    if (!IsEffectivelyVisible()) return;
    if (indeterminate_) {
        if (compositorActive_) return;  // compositor owns the motion
        float s = static_cast<float>(sweep_) + dtSec / kSweepSec;
        if (s >= 1.0f) s -= 1.0f;
        sweep_.SetImmediate(s);
        return;
    }
    animFill_.Approach(value_, dtSec, kAnimTau);
}

void ProgressBar::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    SetDesired({IsAuto(width_)  ? (availW > 0 ? availW : 160.0f) : width_,
                     IsAuto(height_) ? kBarH : height_});
}

void ProgressBar::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;

    const float left  = bounds_.x;
    const float right = bounds_.right();
    const float width = std::max(1.0f, right - left);
    const float ty    = bounds_.y + (bounds_.h - kBarH) * 0.5f;
    const float r     = kBarH * 0.5f;

    // Track groove.
    dc.FillRoundedRect(
        D2D1::RoundedRect(D2D1::RectF(left, ty, right, ty + kBarH), r, r),
        EffectiveBackground(th.dark ? D2D1::ColorF(1, 1, 1, 0.16f)
                                    : D2D1::ColorF(0, 0, 0, 0.10f)));

    if (indeterminate_) {
        // Compositor path (Plan B): the sweeping segment is a DComp visual drawn
        // above this content by the compositor thread — draw only the groove here
        // (already done above) and let the compositor animate the segment.
        if (compositorActive_) return;
        // Fallback path: no composition device — sweep on the UI thread as before.
        float seg = width * kSegFrac;
        float tri = static_cast<float>(sweep_) < 0.5f
                    ? static_cast<float>(sweep_) * 2.0f
                    : 2.0f - static_cast<float>(sweep_) * 2.0f;
        float eased = tri * tri * (3.0f - 2.0f * tri);
        float centerX = left + seg * 0.5f + (width - seg) * eased;
        float segL = centerX - seg * 0.5f;
        float segR = centerX + seg * 0.5f;
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(segL, ty, segR, ty + kBarH), r, r),
            EffectiveAccentColor(pal.accent));
    } else {
        float p = std::clamp(static_cast<float>(animFill_), 0.0f, 1.0f);
        float fillR = left + width * p;
        if (fillR > left + 0.5f) {
            dc.FillRoundedRect(
                D2D1::RoundedRect(D2D1::RectF(left, ty, fillR, ty + kBarH), r, r),
                EffectiveAccentColor(pal.accent));
        }
    }
}

// ---------------------------------------------------------------------------
// Plan B: indeterminate sweep on the DirectComposition compositor thread.
// ---------------------------------------------------------------------------

void ProgressBar::TrackGeometryPx(float& ox, float& oy, float& w, float& h,
                                  float& radius) const {
    // Match Render(): the groove is a kBarH-tall rounded bar, centered vertically
    // in the element bounds. Convert those window-DIP metrics to physical pixels
    // (composition visuals are pixel-space) via the current DPI scale.
    //
    // Geometry comes from the CLIPPED rect, not bounds_: inside a ScrollPanel this
    // control's bounds_ can sit far outside the container (y=-506 was observed while
    // scrolled), and a compositor visual is not covered by the container's D2D clip.
    //
    // The bar is vertically centered in the clipped band, so a control scrolled
    // partly out of view shows only the part of the groove that is actually inside
    // the viewport. Callers must skip an EMPTY clip entirely — see SyncSweepGeometry.
    const RectDip clipped = WindowClippedBounds();
    const float s = Context().dpiScale > 0.0f ? Context().dpiScale : 1.0f;
    const float left = clipped.x;
    const float barH = std::min(kBarH, clipped.h);   // never taller than the visible band
    const float ty = clipped.y + (clipped.h - barH) * 0.5f;
    ox = left * s;
    oy = ty * s;
    w = std::max(1.0f, (clipped.right() - left)) * s;
    h = std::max(0.0f, barH) * s;
    radius = (kBarH * 0.5f) * s;
}

void ProgressBar::SyncSweepGeometry(bool startSweep) {
    if (!sweepVisual_ || !sweepVisual_->Valid()) return;

    // Remember a start request instead of consuming it. This call can arrive while the
    // bar has no visible area — scrolled out of an ancestor viewport, or before the
    // first Arrange (UpdateSweepMode fires on attach, when bounds_ is still empty).
    // Dropping the request there would leave the sweep permanently stopped, because
    // UpdateSweepMode only asks once per entry into indeterminate mode.
    if (startSweep) sweepStartPending_ = true;

    // No visible area: park a zero-size visual and wait. Do NOT derive geometry from
    // an empty rect — centering a bar inside a zero-height band yields a plausible
    // coordinate anchored to the clip edge, which is how the sweep ended up parked in
    // the title bar and tracking it as the user scrolled.
    const RectDip clipped = WindowClippedBounds();
    if (clipped.w <= 0.0f || clipped.h <= 0.0f) {
        sweepVisual_->SetGeometryPx(0.0f, 0.0f, 0.0f, 0.0f,
                                    EffectiveAccentColor(Theme().colors.accent), 0.0f);
        if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr)
            comp->RequestCommit();
        return;
    }

    float ox, oy, w, h, radius;
    TrackGeometryPx(ox, oy, w, h, radius);
    // SetGeometryPx handles everything: reposition + reclip always, redraw the
    // pill when its size/color changed, and — if a sweep is running — re-fit the
    // travel range to the new width phase-continuously (no jump). So resize just
    // calls this.
    sweepVisual_->SetGeometryPx(ox, oy, w, h, EffectiveAccentColor(Theme().colors.accent), radius);

    // Start now that there is real geometry. Also covers the scroll-back-into-view
    // case: the animation is (re)started against the true track width rather than the
    // 1px placeholder the zero-size branch above left behind.
    if (sweepStartPending_) {
        sweepVisual_->StartSweep(kSweepSec);
        sweepStartPending_ = false;
    }
    if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr)
        comp->RequestCommit();
}

void ProgressBar::UpdateSweepMode() {
    const bool shouldRun = indeterminate_ && IsAttached() && IsEffectivelyVisible();
    if (shouldRun) {
        WindowServices* win = Window();
        ICompositionBackend* comp = win ? win->Composition() : nullptr;
        if (!comp) { compositorActive_ = false; return; }  // fallback to UI sweep
        if (!sweepVisual_) sweepVisual_ = std::make_unique<ProgressSweepVisual>();
        if (!sweepVisual_->Valid()) {
            if (FAILED(sweepVisual_->Create(comp))) {
                sweepVisual_.reset();
                compositorActive_ = false;
                return;
            }
            comp->AddToRoot(sweepVisual_->Root());
        }
        compositorActive_ = true;
        SyncSweepGeometry(/*startSweep=*/true);
    } else if (sweepVisual_) {
        // Leaving indeterminate (or detaching): stop + remove the visual so the
        // determinate/idle bar is drawn purely on the window content surface.
        if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr) {
            sweepVisual_->StopSweep();
            comp->RemoveFromRoot(sweepVisual_->Root());
            comp->RequestCommit();
        }
        sweepVisual_->Destroy();
        sweepVisual_.reset();
        compositorActive_ = false;
        sweepStartPending_ = false;  // drop any request the torn-down visual never used
    } else {
        compositorActive_ = false;
        sweepStartPending_ = false;
    }
}

void ProgressBar::OnAttachedToTree() {
    UpdateSweepMode();  // create + start the sweep if we attached in indeterminate mode
}

void ProgressBar::OnDetachedFromTree() {
    // Tear the compositor sweep down while the host / device are still valid
    // (mode is unchanged; we just release the visual for this attach period).
    if (sweepVisual_) {
        if (ICompositionBackend* comp = Window() ? Window()->Composition() : nullptr) {
            sweepVisual_->StopSweep();
            comp->RemoveFromRoot(sweepVisual_->Root());
        }
        sweepVisual_->Destroy();
        sweepVisual_.reset();
    }
    compositorActive_ = false;
}

void ProgressBar::OnBoundsChanged() {
    LayoutCostProbe::Scope probe(LayoutCostKey::ProgressBarBoundsChanged);
    // Resize: reposition + re-fit the travel range to the new width, phase-
    // continuously (SetGeometryPx handles the no-jump re-fit). Not a fresh start.
    if (compositorActive_) SyncSweepGeometry(/*startSweep=*/false);
}

void ProgressBar::OnVisibilityChanged(bool) {
    UpdateSweepMode();
}

void ProgressBar::OnAncestorVisibilityChanged() {
    UpdateSweepMode();
}

void ProgressBar::OnThemeChanged() {
    // Accent may have changed. SetGeometryPx only redraws the pill on a SIZE
    // change, so force a redraw here by invalidating the cached size first.
    if (compositorActive_ && sweepVisual_) {
        sweepVisual_->ForceRedrawNextGeometry();
        SyncSweepGeometry(/*startSweep=*/false);
    }
}

void ProgressBar::OnDpiChanged(float dpiScale) {
    // DPI change resizes the pixel geometry — SetGeometryPx sees the size change
    // and redraws + re-fits automatically.
    // Context has already been refreshed by the host; keep the parameter named so
    // a future regression cannot silently return to using the startup scale.
    UNREFERENCED_PARAMETER(dpiScale);
    if (compositorActive_) SyncSweepGeometry(/*startSweep=*/false);
}

void ProgressBar::OnDeviceLost() {
    // The composition device is gone; drop our visuals (the host rebuilds the
    // stack and calls OnDeviceRestored). Do not touch the dead device.
    if (sweepVisual_) { sweepVisual_->Destroy(); sweepVisual_.reset(); }
    compositorActive_ = false;
}

void ProgressBar::OnDeviceRestored() {
    UpdateSweepMode();  // recreate + restart against the fresh device
}

} // namespace fluent
