// ProgressSweepVisual.cpp — see header. Compositor-thread indeterminate sweep,
// now expressed against ICompositionBackend (Phase 2) instead of raw DComp.

#include "ProgressSweepVisual.h"
#include "../diagnostics/PerformanceCounters.h"  // QpcNow / QpcFrequency
#include <d2d1_1.h>  // ID2D1DeviceContext (D2D 1.1) — the surface draw target
#include <cmath>

namespace fluent {

namespace {
const char* kTag = "ProgressSweep";
// Segment width as a fraction of the track (matches the historical kSegFrac in
// ProgressBar.cpp so the busy indicator looks the same, just compositor-driven).
constexpr float kSegFrac = 0.35f;
} // namespace

HRESULT ProgressSweepVisual::Create(ICompositionBackend* backend) {
    if (!backend) return E_INVALIDARG;
    backend_ = backend;
    // Container: clips the segment to the track rect so the pill never spills past
    // the bar ends. Segment: the moving accent pill, child of the container.
    container_ = backend_->CreateVisual();
    segment_ = backend_->CreateVisual();
    if (!container_ || !segment_) {
        TraceMsg(kTag, "Create: CreateVisual failed");
        Destroy();
        return E_FAIL;
    }
    container_->AddChild(segment_.get());
    return S_OK;
}

void ProgressSweepVisual::Destroy() {
    // Drop the segment's animation first so the compositor stops evaluating it,
    // then release the visuals. The caller removes container_ from the backend
    // root. Order: unparent the child before releasing, then release both.
    if (segment_) segment_->StopAnimation(CompositionProperty::OffsetX);
    if (container_ && segment_) container_->RemoveChild(segment_.get());
    segment_.reset();
    container_.reset();
    backend_ = nullptr;
    trackWpx_ = trackHpx_ = segWpx_ = 0.0f;
    sweeping_ = false;
    segmentDrawn_ = false;
}

float ProgressSweepVisual::CurrentPhaseDeg() const {
    // Sinusoid convention: value(t)=bias+amp*sin(360*freq*t + phase). At StartSweep
    // phase=-90 and t is measured from startQpc_. The current phase is therefore
    // -90 + 360*freq*elapsed, wrapped to [0,360) to keep the float well-scaled.
    if (!sweeping_ || cycleSec_ <= 0.0) return -90.0f;
    const int64_t freq = QpcFrequency();
    if (freq <= 0) return -90.0f;
    const double elapsed = static_cast<double>(QpcNow() - startQpc_) / static_cast<double>(freq);
    double deg = -90.0 + 360.0 * (elapsed / cycleSec_);
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return static_cast<float>(deg);
}

HRESULT ProgressSweepVisual::SetGeometryPx(float originXpx, float originYpx,
                                           float trackWpx, float trackHpx,
                                           D2D1_COLOR_F accent,
                                           float cornerRadiusPx) {
    if (!container_ || !segment_) return E_FAIL;
    const float newW = trackWpx > 1.0f ? trackWpx : 1.0f;
    const float newH = trackHpx > 1.0f ? trackHpx : 1.0f;
    const bool sizeChanged =
        std::fabs(newW - trackWpx_) > 0.5f || std::fabs(newH - trackHpx_) > 0.5f;
    trackWpx_ = newW;
    trackHpx_ = newH;
    segWpx_ = trackWpx_ * kSegFrac;

    // Reposition + reclip the container to the new bar rect (cheap, every call).
    container_->SetOffset(originXpx, originYpx);
    container_->SetClip(0.0f, 0.0f, trackWpx_, trackHpx_);

    // Skip the draw entirely when the segment has no size yet. During startup
    // the demo wires the sweep before layout runs, so trackWpx_ is still 0 and
    // CreateSurface(0, 0, ...) returns E_FAIL. The next SetGeometryPx call —
    // once the ProgressBar has been arranged — will have real dimensions and
    // will draw then. Treating "no size yet" as success avoids a noisy
    // FAILED trace on every cold start.
    if (segWpx_ < 1.0f || trackHpx_ < 1.0f) {
        return S_OK;
    }

    // Redraw the pill when its size changed, on the first draw, or when a theme
    // change forced it (ForceRedrawNextGeometry — accent differs but size doesn't).
    if (sizeChanged || !segmentDrawn_ || forceRedraw_) {
        forceRedraw_ = false;
        FL_RETURN_IF_FAILED(kTag, DrawSegment(accent, cornerRadiusPx));
    }

    // Re-fit the travel range to the new width WITHOUT resetting the phase: seed
    // the replacement animation with the sweep's current phase so the segment
    // stays where it is and just sweeps the new range. Keeps resize jump-free.
    if (sweeping_ && sizeChanged) {
        const float phase = CurrentPhaseDeg();
        SweepSpec spec;
        spec.minX = -segWpx_;
        spec.maxX = trackWpx_;
        spec.cycleSec = cycleSec_;
        spec.phaseDeg = phase;
        segment_->StartOffsetSweep(spec);
        // Re-anchor the clock so CurrentPhaseDeg stays consistent: the new
        // animation starts at `phase`, i.e. as if it began (phase+90)/360*cycle
        // ago. Simplest correct re-anchor: treat now as the phase reference.
        startQpc_ = QpcNow() - static_cast<int64_t>(
            ((phase + 90.0) / 360.0) * cycleSec_ * static_cast<double>(QpcFrequency()));
    }
    return S_OK;
}

HRESULT ProgressSweepVisual::DrawSegment(D2D1_COLOR_F accent,
                                         float cornerRadiusPx) {
    const uint32_t wpx = static_cast<uint32_t>(segWpx_ + 0.5f);
    const uint32_t hpx = static_cast<uint32_t>(trackHpx_ + 0.5f);
    if (wpx == 0 || hpx == 0) return E_FAIL;

    // Draw the accent pill into the segment's backing surface via the backend. The
    // callback's dc is already translated to the surface origin; draw in [0,0,w,h].
    const float r = cornerRadiusPx;
    const bool ok = segment_->DrawSurface(
        wpx, hpx,
        [accent, r, wpx, hpx](ID2D1DeviceContext* dc, float, float) {
            if (!dc) return;  // headless backend: nothing to rasterize
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));  // transparent outside the pill
            ComPtr<ID2D1SolidColorBrush> brush;
            if (SUCCEEDED(dc->CreateSolidColorBrush(accent, brush.GetAddressOf()))) {
                dc->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(0, 0, static_cast<float>(wpx),
                                                  static_cast<float>(hpx)), r, r),
                    brush.Get());
            }
        });
    // A fake/headless backend returns false (no real surface) but the segment is
    // still logically sized — mark it drawn so we don't re-attempt every call.
    segmentDrawn_ = true;
    return ok ? S_OK : S_FALSE;
}

void ProgressSweepVisual::StartSweep(double cycleSec) {
    if (!segment_) { TraceMsg(kTag, "StartSweep: no segment"); return; }
    cycleSec_ = cycleSec > 0.0 ? cycleSec : 1.4;
    // Travel: from fully off the left (segment right edge at track left) to fully
    // off the right (segment left edge at track right). The container clip masks
    // the overhang, so the pill appears to emerge and exit at the ends.
    SweepSpec spec;
    spec.minX = -segWpx_;
    spec.maxX = trackWpx_;
    spec.cycleSec = cycleSec_;
    spec.phaseDeg = -90.0f;  // start at the left edge
    segment_->StartOffsetSweep(spec);
    sweeping_ = true;
    startQpc_ = QpcNow();  // phase reference: -90deg (left edge) at t=0
}

void ProgressSweepVisual::StopSweep() {
    sweeping_ = false;
    if (segment_) segment_->SetOffset(-segWpx_, 0.0f);  // park off the left edge
}

} // namespace fluent
