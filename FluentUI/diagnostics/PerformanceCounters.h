// PerformanceCounters.h — per-frame and process-level metrics for the frame loop
// (roadmap §18.3). WP-00 introduces the counter surface so later work packages
// have somewhere to record layout/render/present timings, dirty-element counts,
// TextLayout cache hit/miss and active-animation counts without threading a bag
// of out-params through every call site.
//
// Design constraints from the roadmap:
//   * The counters live in the diagnostics layer, not the hot path. Recording a
//     value is a plain field write / increment (no allocation, no locking): the
//     UI is single-threaded (roadmap §16) so a per-window FrameStats owned by the
//     host is enough. There is no global singleton (roadmap §19.4).
//   * A frame's stats are reset at BeginFrame and sampled at EndFrame. The host
//     may keep a small ring of recent frames for P95/P99 export (WP-07); WP-00
//     only defines the shape and the scoped timer so nothing has to change later.
//   * ScopedTimer measures a phase in milliseconds using QueryPerformanceCounter
//     (the same clock the animation system uses, roadmap §14.6) and adds the
//     elapsed time into a float field on destruction.
//
// This header is self-contained (only <windows.h> via fl_common) so tests and
// FluentUIBench can include it without pulling in the graphics stack.
#pragma once

#include "../fl_common.h"
#include <cstdint>

namespace fluent {

// QueryPerformanceCounter frequency, cached once. Exposed so a caller can
// convert its own QPC deltas to milliseconds consistently with ScopedTimer.
inline int64_t QpcFrequency() {
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return freq;
}

inline int64_t QpcNow() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

// Convert a QPC tick delta to milliseconds.
inline double QpcToMs(int64_t ticks) {
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(QpcFrequency());
}

// The metrics recorded for a single frame (roadmap §18.3). Timings are in
// milliseconds; counts are plain integers. A field left at its default (0) means
// "not yet measured by the current pipeline" — WP-00 wires the struct and the
// timing mechanism; individual subsystems fill their fields as they are migrated.
struct FrameStats {
    // Phase timings (ms).
    double cpuFrameMs = 0.0;    // whole frame on the UI thread
    double animationMs = 0.0;   // active-animation tick
    double layoutMs = 0.0;      // Measure + Arrange
    double renderMs = 0.0;      // element traversal + D2D draw
    double presentMs = 0.0;     // Present1 / DComp Commit
    // Cost of ApplyPendingResize (surface resize + the OnLayout it forces).
    //
    // WHY THIS IS SEPARATE FROM layoutMs. ApplyPendingResize runs BEFORE RenderNow
    // starts its clock (RunDueFrame and ServiceModalFrame both call it, then paint),
    // so its cost lands in neither cpuFrameMs nor layoutMs. That is exactly the work
    // a resize drag is made of — for a control that must re-wrap its text at the new
    // width it dominates everything else — and it was invisible on the HUD, which
    // made a visibly stuttering drag read as a cheap frame. Measured by the caller
    // and carried into the next frame's stats, the same way animationMs carries the
    // PumpAnimations tick.
    double resizeMs = 0.0;
    // The two halves of layoutMs, written by Window::OnLayout.
    //
    // WHY BOTH ARE NEEDED. `layout=5.4ms` on a resize line does not say whether to
    // attack Measure or Arrange, and the two have opposite fixes: an expensive
    // Measure is a caching/virtualization problem, an expensive Arrange is a
    // "we place every child every frame" problem that no measure cache can touch.
    // A measured drag on the Gallery had Measure and Arrange within 2x of each
    // other, which is exactly the case a single number cannot distinguish.
    //
    // Assigned (not accumulated) per OnLayout call: a frame that lays out twice
    // reports the last pass, matching how the resize trace reads them right after
    // the OnLayout it just forced.
    double measureMs = 0.0;
    double arrangeMs = 0.0;

    // Work counts.
    uint32_t dirtyElements = 0;    // elements re-measured/re-rendered this frame
    uint32_t dirtyRects = 0;       // WP-07 dirty-rect count (0 = full-window)
    uint32_t drawOps = 0;          // draw calls issued into the DC

    // Text layout cache (roadmap §13.3 / §18.3).
    uint32_t textLayoutsNew = 0;      // IDWriteTextLayout objects created
    uint32_t textLayoutCacheHits = 0; // served from cache

    // Animation.
    uint32_t activeAnimations = 0;

    // Frame accounting.
    uint32_t frameReason = 0;   // FrameReason bitmask (roadmap §14.3)
    bool lateFrame = false;     // missed the frame-latency deadline

    // Reset all per-frame fields to their "nothing recorded" state. Called at the
    // start of each frame by the host before any phase records into it.
    void Reset() { *this = FrameStats{}; }
};

// Process-level counters sampled on demand (roadmap §18.3) — working set,
// private bytes, GDI/USER handles. Cheap enough to sample once per second or
// around a stress loop (e.g. before/after 10,000 popup open/close cycles) to
// prove no monotonic growth. Returns false if a value could not be read.
struct ProcessStats {
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
    uint32_t gdiObjects = 0;
    uint32_t userObjects = 0;

    // Fill from the current process. Implemented in PerformanceCounters.cpp so
    // the psapi dependency stays out of headers.
    bool Sample();
};

// RAII phase timer: measures wall-clock from construction to destruction and
// adds the elapsed milliseconds into `target`. Use one per pipeline phase:
//
//   {
//       ScopedTimer t(stats.layoutMs);
//       RunLayout();
//   }  // stats.layoutMs += elapsed
//
// Accumulates (+=) rather than assigns, so several timed regions contributing to
// the same phase in one frame sum correctly. No allocation; the whole object is
// two int64_t and a reference.
class ScopedTimer {
public:
    explicit ScopedTimer(double& target) : target_(target), start_(QpcNow()) {}
    ~ScopedTimer() { target_ += QpcToMs(QpcNow() - start_); }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    double& target_;
    int64_t start_;
};

} // namespace fluent
