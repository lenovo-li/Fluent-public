// FrameScheduler.h — on-demand frame scheduling + the per-frame animation clock
// for a top-level window (roadmap §14).
//
// Two responsibilities, kept in one small, HWND-free, unit-testable object:
//
//  1. Frame-request coalescing (WP-01, §14.3). Anything that dirties the window
//     — input, a property change, layout invalidation, an animation tick, a
//     resize/DPI/theme change, a popup — calls RequestFrame(reason). Multiple
//     requests between two frames merge into a single pending frame whose reason
//     is the OR of all requests. The host asks NeedsFrame() and, when it renders,
//     calls BeginFrame()/EndFrame() to consume the pending state. A frame that is
//     re-dirtied *during* rendering is preserved for the next frame, never lost.
//
//  2. The animation clock (F1/F2). The scheduler owns the "should a ~60fps timer
//     be running?" edge logic (arm/disarm) and the framerate-independent dt, but
//     owns no HWND: the host installs an arm/disarm pair that start/stop the real
//     OS timer, and drives ComputeDt() from its tick. dt now comes from the
//     high-resolution QPC clock (§14.6: absolute time, not tick counts) so easing
//     is stable regardless of timer granularity or stalls.
//
// The two halves are independent: an animation running arms the clock AND keeps
// requesting Animation frames; a one-off property change only requests a frame
// and never arms the clock. Idle = no pending frame and no active animation.
#pragma once

#include <cstdint>
#include <functional>

namespace fluent {

// Why a frame was requested (roadmap §14.3). A bitmask so several reasons merge
// into one pending frame; recorded per frame for the Debug HUD / FrameStats.
enum class FrameReason : uint32_t {
    None      = 0,
    Input     = 1u << 0,
    Animation = 1u << 1,
    Layout    = 1u << 2,
    Paint     = 1u << 3,
    Resize    = 1u << 4,
    Scroll    = 1u << 5,
    Theme     = 1u << 6,
    Dpi       = 1u << 7,
    Popup     = 1u << 8,
};

inline FrameReason operator|(FrameReason a, FrameReason b) {
    return static_cast<FrameReason>(static_cast<uint32_t>(a) |
                                    static_cast<uint32_t>(b));
}
inline FrameReason& operator|=(FrameReason& a, FrameReason b) {
    a = a | b;
    return a;
}
inline FrameReason operator&(FrameReason a, FrameReason b) {
    return static_cast<FrameReason>(static_cast<uint32_t>(a) &
                                    static_cast<uint32_t>(b));
}
inline bool AnyReason(FrameReason r) { return r != FrameReason::None; }
inline bool HasReason(FrameReason r, FrameReason test) {
    return AnyReason(r & test);
}

class FrameScheduler {
public:
    // Nominal frame interval (~60fps). Also the dt used for the first tick after
    // an arm, before any real elapsed time is known.
    static constexpr unsigned kIntervalMs = 16;
    // Upper bound on dt (seconds) so easing stays sane after a stall (the timer
    // can be delayed arbitrarily by a modal loop, drag, or a busy UI thread).
    static constexpr float kMaxDtSec = 0.1f;

    // ---- Frame-request coalescing (§14.3) --------------------------------

    // Merge a frame request. Between two frames, all reasons OR together into a
    // single pending frame. Cheap (a bit-or + bool set); safe to call many times
    // per event. Does not itself render — the host polls NeedsFrame().
    void RequestFrame(FrameReason reason) {
        pendingReasons_ |= reason;
        framePending_ = true;
    }

    // True if a frame is pending and the host should render.
    bool NeedsFrame() const { return framePending_; }

    // The merged reasons for the pending (or in-progress) frame.
    FrameReason PendingReasons() const { return pendingReasons_; }

    // Begin servicing a frame: snapshot and clear the pending request, and mark a
    // frame in progress so a re-invalidation during rendering is deferred to the
    // NEXT frame rather than being swallowed by EndFrame(). Returns the reasons
    // being serviced (for FrameStats / the Debug HUD).
    FrameReason BeginFrame() {
        FrameReason serviced = pendingReasons_;
        pendingReasons_ = FrameReason::None;
        framePending_ = false;
        frameInProgress_ = true;
        return serviced;
    }

    // Finish the frame. Any RequestFrame() that arrived during rendering has set
    // framePending_ again; we leave it so the host renders once more. (A frame is
    // never lost, and a stable idle state is reached once nothing re-requests.)
    void EndFrame() { frameInProgress_ = false; }

    bool FrameInProgress() const { return frameInProgress_; }

    // ---- Animation clock (F1/F2) -----------------------------------------

    // Install the host's timer controls. `arm` starts the real OS timer at
    // kIntervalMs; `disarm` stops it. Both are invoked only on state edges.
    void SetCallbacks(std::function<void()> arm, std::function<void()> disarm) {
        arm_ = std::move(arm);
        disarm_ = std::move(disarm);
    }

    // Bring the running state in line with `wanted`. Arms on a rising edge
    // (resetting the dt clock so the next tick uses the nominal interval),
    // disarms on a falling edge, and is a no-op when already in the target state.
    void SetWanted(bool wanted);

    bool Running() const { return running_; }

    // Seconds elapsed for a tick occurring at QPC timestamp `nowQpc` (the host
    // passes QpcNow()). The first tick after an arm returns the nominal interval;
    // later ticks return the real elapsed time, clamped to kMaxDtSec. Advances
    // the internal clock, so call exactly once per tick.
    float ComputeDt(int64_t nowQpc);

private:
    void Arm();
    void Disarm();

    // Frame coalescing state.
    FrameReason pendingReasons_ = FrameReason::None;
    bool framePending_ = false;
    bool frameInProgress_ = false;

    // Animation clock state.
    std::function<void()> arm_;
    std::function<void()> disarm_;
    bool running_ = false;
    // 0 is the "no previous tick yet" sentinel: the next ComputeDt returns the
    // nominal interval instead of a bogus elapsed time. Reset to 0 on every arm.
    int64_t lastTickQpc_ = 0;
};

} // namespace fluent
