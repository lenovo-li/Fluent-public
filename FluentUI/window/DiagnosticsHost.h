// DiagnosticsHost.h — performance diagnostics and session logging.
//
// Encapsulates the diagnostic subsystems NativeWindowHost used to hold:
// two FrameRing<120> buffers (frame build cost + frame interval, the raw data
// behind the HUD's FPS / P95 / P99 jank numbers) and SessionLog (the event
// recorder for the diagnostics timeline).
//
// Accessors are deliberately NOT called FrameRing() — a member function with
// the same name as the class template would hide the template inside this
// class's scope, which breaks the member declarations themselves.
#pragma once

#include "../diagnostics/FrameRing.h"
#include "../diagnostics/SessionLog.h"

namespace fluent {

// DiagnosticsHost owns the performance rings and the event log. The window
// feeds it per-frame costs; the HUD reads the aggregates back.
class DiagnosticsHost {
public:
    DiagnosticsHost() = default;

    // Per-frame CPU build cost (measure + arrange + render), in ms.
    FrameRing<120>& Frames() { return frameRing_; }
    const FrameRing<120>& Frames() const { return frameRing_; }

    // Wall-clock gap between consecutive painted frames, in ms.
    FrameRing<120>& Intervals() { return intervalRing_; }
    const FrameRing<120>& Intervals() const { return intervalRing_; }

    // Session event log (timeline entries for the diagnostics overlay).
    SessionLog& Log() { return sessionLog_; }
    const SessionLog& Log() const { return sessionLog_; }

private:
    // Ring of the last 120 frame BUILD costs. Fed from RenderNow; the HUD turns
    // it into the CPU-cost histogram.
    FrameRing<120> frameRing_;

    // Ring of the last 120 frame INTERVALS — the real on-screen cadence, which
    // is what FPS and the jank percentiles are computed from. Distinct from
    // frameRing_: a cheap frame that arrives late still shows up as jank here.
    FrameRing<120> intervalRing_;

    SessionLog sessionLog_;
};

} // namespace fluent
