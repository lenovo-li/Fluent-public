// SessionLog.cpp — see header. Pure in-memory capture + report serialization.

#include "SessionLog.h"
#include <algorithm>
#include <cstdio>

namespace fluent {

const char* SessionPhaseName(SessionPhase p) {
    switch (p) {
        case SessionPhase::Idle:      return "Idle";
        case SessionPhase::Animating: return "Animating";
        case SessionPhase::Resizing:  return "Resizing";
        case SessionPhase::Moving:    return "Moving";
        case SessionPhase::Dpi:       return "Dpi";
        case SessionPhase::Other:     return "Other";
    }
    return "?";
}

void SessionLog::Clear() {
    active_ = false;
    dpiScalePct_ = 100;
    initWpx_ = initHpx_ = 0;
    initWdip_ = initHdip_ = 0.0f;
    refreshHz_ = 0;
    // Reset the threshold alongside refreshHz_, since Start() derives one from the other.
    // Leaving a previous session's value here would silently apply a 240 Hz threshold to
    // a session started on a 60 Hz monitor (or vice versa) after the window moves.
    jankThresholdMs_ = kDefaultJankThresholdMs;
    startClockSec_ = 0.0;
    totalFrames_ = 0;
    lastFrameTSec_ = 0.0;
    for (auto& a : phases_) a = PhaseAgg{};
    events_.clear();
    jank_.clear();
    eventsTruncated_ = false;
    jankTruncated_ = false;
}

void SessionLog::Start(int dpiScalePct, int wPx, int hPx, float wDip, float hDip,
                       int refreshHz) {
    Clear();
    active_ = true;
    dpiScalePct_ = dpiScalePct;
    initWpx_ = wPx;
    initHpx_ = hPx;
    initWdip_ = wDip;
    initHdip_ = hDip;
    refreshHz_ = refreshHz;
    // Derive the jank threshold from THIS display's cadence rather than leaving the
    // 60 Hz-shaped default in place.
    //
    // The old fixed 33 ms meant "about two missed frames" only at 60 Hz. At 240 Hz a
    // frame is 4.17 ms, so 33 ms is EIGHT missed frames — a stutter long enough to see
    // plainly would not be recorded at all, and the log would report a clean session on
    // the machine where the developer cannot feel the problem either. That is the exact
    // combination that ships jank to slower hardware.
    //
    // Two frame intervals is the definition being expressed (one late frame is normal
    // scheduling noise; two in a row is a visible hitch), so compute it. The 8 ms floor
    // keeps the threshold above timer granularity on very high refresh rates, where 2
    // intervals is only 8.3 ms and QPC/scheduler noise alone could trip it.
    if (refreshHz > 1) {
        const float twoFrames = 2.0f * 1000.0f / static_cast<float>(refreshHz);
        jankThresholdMs_ = twoFrames < 8.0f ? 8.0f : twoFrames;
    }
    // Reserve up front so the per-event path never reallocates mid-session.
    events_.reserve(1024);
    jank_.reserve(1024);
}

void SessionLog::RecordFrame(const FrameStats& f, float intervalMs,
                             SessionPhase phase, double tSec) {
    if (!active_) return;
    ++totalFrames_;
    lastFrameTSec_ = tSec;

    PhaseAgg& a = AggFor(phase);
    ++a.frames;
    a.sumIntervalMs += intervalMs;
    if (intervalMs > a.maxIntervalMs) a.maxIntervalMs = intervalMs;
    // Track the worst single frame by CPU cost so the report can show which
    // stage (layout/render/present) dominated the hitch.
    const float cpu = static_cast<float>(f.cpuFrameMs);
    if (cpu > a.worstCpuMs) {
        a.worstCpuMs = cpu;
        a.worstLayMs = static_cast<float>(f.layoutMs);
        a.worstRenMs = static_cast<float>(f.renderMs);
        a.worstPreMs = static_cast<float>(f.presentMs);
    }
    // Tracked independently: the tick runs before the frame, so the worst-CPU frame
    // is usually not the one that paid for a surface refill.
    const float tick = static_cast<float>(f.animationMs);
    if (tick > a.worstTickMs) a.worstTickMs = tick;

    // Individually log a hitched frame (interval well above the refresh budget),
    // capped so a long session cannot grow without bound.
    //
    // Idle is EXCLUDED: this is an on-demand renderer, so while idle it deliberately
    // paints nothing until something changes. The gap before the next frame is then
    // just how long the user sat still — correct behaviour, not a hitch. Counting it
    // drowned the real hitches: a measured session logged 25 "jank" frames of which 17
    // were idle gaps (one of 414ms with 1.01ms of CPU). A gap only means something
    // when continuous frames were WANTED, i.e. animating / resizing / moving / dpi.
    const bool wantedContinuousFrames = phase != SessionPhase::Idle;
    if (wantedContinuousFrames && intervalMs >= jankThresholdMs_) {
        if (jank_.size() < kMaxJank) {
            JankFrame jf;
            jf.tSec = tSec;
            jf.intervalMs = intervalMs;
            jf.cpuMs = cpu;
            jf.layMs = static_cast<float>(f.layoutMs);
            jf.renMs = static_cast<float>(f.renderMs);
            jf.preMs = static_cast<float>(f.presentMs);
            jf.tickMs = static_cast<float>(f.animationMs);
            jf.phase = phase;
            jank_.push_back(jf);
        } else {
            jankTruncated_ = true;
        }
    }
}

void SessionLog::RecordEvent(const char* category, const std::string& detail,
                             double tSec) {
    if (!active_) return;
    if (events_.size() >= kMaxEvents) { eventsTruncated_ = true; return; }
    Event e;
    e.tSec = tSec;
    e.cat = category ? category : "";
    e.detail = detail;
    events_.push_back(std::move(e));
}

namespace {
// Approx FPS from an average interval (ms). 0 -> 0.
float FpsFromInterval(double avgMs) {
    return avgMs > 0.0 ? static_cast<float>(1000.0 / avgMs) : 0.0f;
}
} // namespace

std::string SessionLog::Serialize() const {
    std::string out;
    out.reserve(8192);
    char line[512];

    const double durSec = lastFrameTSec_ - startClockSec_;
    std::snprintf(line, sizeof(line),
        "=== FluentUI Session Log ===\n"
        "duration=%.1fs  DPI=%d%%  initSize=%dx%dpx (%.0fx%.0fdip)  refresh=%dHz\n"
        "frames=%u  events=%zu  jankFrames=%zu (threshold=%.1fms, idle excluded)\n",
        durSec, dpiScalePct_, initWpx_, initHpx_, initWdip_, initHdip_, refreshHz_,
        totalFrames_, events_.size(), jank_.size(), jankThresholdMs_);
    out += line;

    // --- Per-phase frame summary -----------------------------------------
    out += "\n--- Frame summary by phase ---\n";
    for (int i = 0; i < kPhaseCount; ++i) {
        const PhaseAgg& a = phases_[i];
        if (a.frames == 0) continue;
        const double avgInterval = a.sumIntervalMs / a.frames;
        std::snprintf(line, sizeof(line),
            "%-10s frames=%-6u avgFps=%-5.0f worstInterval=%.1fms  "
            "worstFrame cpu=%.2f lay=%.2f ren=%.2f pre=%.2f  worstTick=%.2f\n",
            SessionPhaseName(static_cast<SessionPhase>(i)), a.frames,
            FpsFromInterval(avgInterval), a.maxIntervalMs,
            a.worstCpuMs, a.worstLayMs, a.worstRenMs, a.worstPreMs,
            a.worstTickMs);
        out += line;
    }

    // --- Event timeline ---------------------------------------------------
    out += "\n--- Events ---\n";
    for (const Event& e : events_) {
        std::snprintf(line, sizeof(line), "[+%7.2f] %-14s %s\n",
                      e.tSec - startClockSec_, e.cat.c_str(), e.detail.c_str());
        out += line;
    }
    if (eventsTruncated_) out += "(event log truncated — cap reached)\n";

    // --- Worst frames (sorted by interval desc, top 30) -------------------
    // `tick` is the pre-frame animation tick (surface refills land there), listed
    // separately because it is not part of cpu.
    out += "\n--- Worst frames (interval ms, phase, cpu lay/ren/pre, tick) ---\n";
    std::vector<JankFrame> sorted = jank_;
    std::sort(sorted.begin(), sorted.end(),
              [](const JankFrame& a, const JankFrame& b) {
                  return a.intervalMs > b.intervalMs;
              });
    const size_t topN = std::min<size_t>(30, sorted.size());
    for (size_t i = 0; i < topN; ++i) {
        const JankFrame& j = sorted[i];
        std::snprintf(line, sizeof(line),
            "[+%7.2f] %-10s interval=%6.1f  cpu=%.2f  %.2f/%.2f/%.2f  tick=%.2f\n",
            j.tSec - startClockSec_, SessionPhaseName(j.phase), j.intervalMs,
            j.cpuMs, j.layMs, j.renMs, j.preMs, j.tickMs);
        out += line;
    }
    if (jankTruncated_) out += "(jank log truncated — cap reached)\n";

    return out;
}

} // namespace fluent
