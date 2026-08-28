// DebugHud.h — pure formatting for the on-screen performance HUD (roadmap §18.3).
//
// The host owns rendering (DWrite/D2D); this header only turns the already-
// collected FrameStats + FrameRing percentiles into HUD text lines, so the
// formatting logic is unit-testable with no window or GPU. FormatHudLines()
// takes the last frame's stats and the CPU-time percentiles and returns the
// multi-line string the host draws in a corner.
//
// Kept dependency-light: FrameStats (diagnostics/PerformanceCounters.h) + plain
// floats for the percentiles. No Windows, no D2D.
#pragma once

#include <cstdio>
#include <string>

#include "PerformanceCounters.h"

namespace fluent {

// Frame-INTERVAL percentiles (ms), i.e. the wall-clock gap between consecutive
// presented frames — the real cadence the eye sees. FPS derives from p50; the
// p95/p99/max spikes are the "jank" indicator (an occasional 33ms interval is a
// visible stutter even when p50 is a smooth 16.6ms). The host reads these from a
// FrameRing<N> of intervals and passes them in; plain values here (not the ring
// template) keep this header instantiation-free and trivially testable.
//
// NOTE: this is the frame INTERVAL, not FrameStats.cpuFrameMs (which is the CPU
// cost to *build* one frame). A HUD shows both: interval -> FPS/smoothness,
// cpuFrameMs -> how much CPU headroom is left.
struct HudPercentiles {
    float p50Ms = 0.0f;
    float p95Ms = 0.0f;
    float p99Ms = 0.0f;
    float maxMs = 0.0f;
    unsigned sampleCount = 0;  // frames recorded in the ring so far
};

// CPU-cost percentiles (ms) over the same window — the "build one frame" cost, not
// the cadence.
//
// WHY THE HUD NEEDS THIS AND NOT JUST THE INTERVAL P50. A frame-time distribution
// under real load is frequently BIMODAL: a handful of catastrophic frames (a full
// re-wrap of a large document at a new width) interleaved with a burst of trivial
// ones (repaints that found nothing to redo). A median is meaningless there — it
// lands in whichever mode holds more samples, which is the cheap one, and the HUD
// then reports a four-digit FPS while the window visibly stutters. P99 and Max
// cannot be averaged away by a burst of cheap frames, so they are what actually
// says "something here costs 900ms". Fed from the host's CPU FrameRing.
struct HudCpuPercentiles {
    float p50Ms = 0.0f;
    float p99Ms = 0.0f;
    float maxMs = 0.0f;
};

// Approximate instantaneous FPS from a frame time (ms). 0 ms -> 0 fps (avoids a
// divide-by-zero and reads as "no data" rather than a bogus spike).
inline float FpsFromMs(float ms) { return ms > 0.0f ? 1000.0f / ms : 0.0f; }

// Decode a FrameReason bitmask into a short tag string for the HUD (e.g.
// "Anim|Paint"). Empty mask -> "-". Order matches FrameReason bit order.
inline std::string FormatFrameReason(uint32_t reason) {
    if (reason == 0) return "-";
    struct Bit { uint32_t mask; const char* name; };
    static const Bit kBits[] = {
        {1u << 0, "Input"},  {1u << 1, "Anim"},  {1u << 2, "Layout"},
        {1u << 3, "Paint"},  {1u << 4, "Resize"}, {1u << 5, "Scroll"},
        {1u << 6, "Theme"},  {1u << 7, "Dpi"},   {1u << 8, "Popup"},
    };
    std::string out;
    for (const Bit& b : kBits) {
        if (reason & b.mask) {
            if (!out.empty()) out += '|';
            out += b.name;
        }
    }
    return out;
}

// Build the HUD text (lines separated by '\n'). Pure: same inputs -> same
// output, no side effects. `interval` holds the frame-interval percentiles (FPS
// + jank), `cpu` the CPU-cost percentiles, and `f` the last frame's phase split
// and work counts. Layout:
//   FPS 60  (interval P50 16.6ms)
//   jank P95 17.2  P99 22.0  Max 34 ms
//   CPU P50 2.10  P99 8.40  Max 34.0 ms
//   CPU 2.10ms  lay 0.30  ren 1.50  pre 0.30
//   tick 0.80ms  rsz 0.00  draw 128  dirtyEls 3  rects 1
//   anim 2  reason Anim|Paint
// The lay/ren/pre split (layout vs render vs present ms) is what makes a resize
// frame-drop diagnosable: watch which of the three grows while dragging the edge.
// `tick` is the animation tick that ran BEFORE the frame (PumpAnimations) and is
// therefore not inside CPU — it is where a compositor scroll surface gets refilled,
// so a long-document scroll shows its real UI-thread cost there and nowhere else.
// `rsz` is ApplyPendingResize (surface resize + forced OnLayout), likewise outside
// CPU and likewise the dominant cost of a resize drag for a text-heavy control.
//
// The CPU percentile line exists because the FPS line can lie: see HudCpuPercentiles.
// Read them together — a low interval P50 next to a huge CPU Max means a bimodal
// distribution, i.e. the FPS number is describing the cheap mode only.
inline std::string FormatHudLines(const FrameStats& f, const HudPercentiles& interval,
                                 const HudCpuPercentiles& cpu = {}) {
    char buf[640];
    std::snprintf(
        buf, sizeof(buf),
        "FPS %.0f  (interval P50 %.1fms)\n"
        "jank P95 %.1f  P99 %.1f  Max %.0f ms\n"
        "CPU P50 %.2f  P99 %.2f  Max %.1f ms\n"
        "CPU %.2fms  lay %.2f  ren %.2f  pre %.2f\n"
        "tick %.2fms  rsz %.2f  draw %u  dirtyEls %u  rects %u\n"
        "anim %u  reason %s",
        FpsFromMs(interval.p50Ms), interval.p50Ms,
        interval.p95Ms, interval.p99Ms, interval.maxMs,
        cpu.p50Ms, cpu.p99Ms, cpu.maxMs,
        static_cast<float>(f.cpuFrameMs),
        static_cast<float>(f.layoutMs), static_cast<float>(f.renderMs),
        static_cast<float>(f.presentMs),
        static_cast<float>(f.animationMs), static_cast<float>(f.resizeMs),
        f.drawOps, f.dirtyElements, f.dirtyRects,
        f.activeAnimations, FormatFrameReason(f.frameReason).c_str());
    return std::string(buf);
}

}  // namespace fluent
