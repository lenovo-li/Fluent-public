// SessionLog.h — in-memory diagnostic capture for a whole app session
// (roadmap §18.3 diagnostics; companion to FrameStats / FrameRing / DebugHud).
//
// Purpose: record the user's operations (resize, click, toggle) and per-frame
// performance while the app runs, then serialize a readable report the app
// writes to disk on close. This makes performance issues (resize frame drops,
// misplaced compositor visuals) diagnosable AFTER THE FACT from a log file,
// without watching the on-screen HUD live.
//
// PERFORMANCE CONTRACT (the whole point — must not perturb what it measures):
//   * Capture is IN-MEMORY ONLY. No file I/O, no allocation, no syscalls on the
//     per-frame path — RecordFrame does bounded aggregate updates + at most one
//     push into a pre-reserved, capped vector for a jank frame. Cheaper than the
//     two ring pushes RenderNow already does.
//   * The log NEVER requests a frame. It only records frames the host already
//     paints, so the app's idle-zero-refresh property is preserved.
//   * The file is written ONCE, on close (Serialize() -> app writes it). Writing
//     every frame would be the one thing that self-inflicts the jank we measure.
//
// This class does NO file I/O (the FluentUI rendering library stays file-free —
// see ConfigStore's note). Serialize() returns a std::string; the app persists
// it (portable / %APPDATA%) exactly as it persists config.
#pragma once

#include "PerformanceCounters.h"  // FrameStats
#include <cstdint>
#include <string>
#include <vector>

namespace fluent {

// What the UI thread was doing when a frame was recorded. Drives the per-phase
// summary ("does resizing jank, and which stage — layout/render/present?").
enum class SessionPhase : uint8_t {
    Idle,       // steady repaint with nothing special (e.g. HUD live-refresh)
    Animating,  // an AnimationRegistry tick drove the frame
    Resizing,   // inside the system modal loop with a pending resize
    Moving,     // inside the system modal loop, position-only (no resize)
    Dpi,        // a DPI change frame
    Other,
};
const char* SessionPhaseName(SessionPhase p);

class SessionLog {
public:
    // Begin a session: clear state and stamp the header. `dpiScalePct` is the
    // display scale (e.g. 200), `wPx/hPx` the initial client pixel size,
    // `wDip/hDip` the same in DIPs, `refreshHz` the monitor refresh (0 if
    // unknown). tSec is the session-relative clock origin (usually 0).
    void Start(int dpiScalePct, int wPx, int hPx, float wDip, float hDip,
               int refreshHz);

    // Record one painted frame. Cheap + bounded: updates per-phase aggregates and,
    // only if the frame's interval exceeds the jank threshold, appends a capped
    // jank entry. `intervalMs` is the gap since the previous painted frame (the
    // real cadence); `f` supplies cpu/layout/render/present; `tSec` is the
    // session-relative timestamp.
    void RecordFrame(const FrameStats& f, float intervalMs, SessionPhase phase,
                     double tSec);

    // Record a discrete operation/event (resize begin/end, click, toggle, a
    // compositor-geometry trace). Rare (a few per second); appends to a capped
    // vector. `category` is a short tag ("resize", "input", "ProgressSweep").
    void RecordEvent(const char* category, const std::string& detail, double tSec);

    // Produce the human-readable report (pure; no I/O). Safe to call once at
    // shutdown. Empty-session-safe.
    std::string Serialize() const;

    void Clear();
    bool Active() const { return active_; }
    size_t EventCount() const { return events_.size(); }
    size_t FrameCount() const { return totalFrames_; }

    // Jank threshold: a frame whose interval exceeds this many ms is logged
    // individually. Start() sets it to two intervals of the display it is told about, so
    // "jank" means the same thing (a visible two-frame hitch) at 60, 120 and 240 Hz.
    //
    // Override the jank threshold. Start() normally derives it from the refresh rate it
    // is given (two frame intervals, floored at 8 ms), so call this only AFTER Start()
    // and only to study a specific threshold — otherwise Start() overwrites it.
    void SetJankThresholdMs(float ms) { jankThresholdMs_ = ms; }
    // What the current session counts as jank. Exposed so the summary and the tests can
    // state the threshold instead of assuming 33 ms.
    float JankThresholdMs() const { return jankThresholdMs_; }

private:
    struct Event {
        double tSec = 0.0;
        std::string cat;
        std::string detail;
    };
    // A single hitched frame, kept with enough breakdown to see what dominated.
    // `tickMs` is the animation tick that ran BEFORE the frame (PumpAnimations), so
    // it is NOT part of cpuMs — a compositor scroll surface refill lands there and
    // nowhere else, which is the only way a long-document scroll's UI-thread cost
    // becomes visible in the log.
    struct JankFrame {
        double tSec = 0.0;
        float intervalMs = 0.0f;
        float cpuMs = 0.0f, layMs = 0.0f, renMs = 0.0f, preMs = 0.0f;
        float tickMs = 0.0f;
        SessionPhase phase = SessionPhase::Other;
    };
    // Per-phase running aggregate (exact; no per-frame storage).
    struct PhaseAgg {
        uint32_t frames = 0;
        double sumIntervalMs = 0.0;   // for average cadence -> avg FPS
        float maxIntervalMs = 0.0f;   // worst cadence in this phase
        // Worst single frame by CPU cost, with its stage breakdown.
        float worstCpuMs = 0.0f, worstLayMs = 0.0f, worstRenMs = 0.0f,
              worstPreMs = 0.0f;
        // Worst animation tick seen in this phase, tracked separately from worstCpu:
        // the tick runs outside the frame, so the frame with the worst CPU is often
        // not the one with the worst refill.
        float worstTickMs = 0.0f;
    };

    static constexpr size_t kMaxEvents = 10000;  // ~ caps the timeline
    static constexpr size_t kMaxJank = 4096;     // caps hitch entries
    static constexpr int kPhaseCount = 6;

    PhaseAgg& AggFor(SessionPhase p) { return phases_[static_cast<int>(p)]; }
    const PhaseAgg& AggFor(SessionPhase p) const {
        return phases_[static_cast<int>(p)];
    }

    bool active_ = false;
    // Header.
    int dpiScalePct_ = 100;
    int initWpx_ = 0, initHpx_ = 0;
    float initWdip_ = 0.0f, initHdip_ = 0.0f;
    int refreshHz_ = 0;
    double startClockSec_ = 0.0;

    // Fallback only, used before Start() and when the refresh rate is unknown. Start()
    // replaces it with two intervals of the ACTUAL display: a fixed 33 ms is ~2 frames at
    // 60 Hz but 8 frames at 240 Hz, so on a high-refresh panel it would hide every
    // stutter short of a quarter second.
    static constexpr float kDefaultJankThresholdMs = 33.0f;
    float jankThresholdMs_ = kDefaultJankThresholdMs;

    uint32_t totalFrames_ = 0;
    double lastFrameTSec_ = 0.0;

    PhaseAgg phases_[kPhaseCount];
    std::vector<Event> events_;
    std::vector<JankFrame> jank_;
    bool eventsTruncated_ = false;
    bool jankTruncated_ = false;
};

} // namespace fluent
