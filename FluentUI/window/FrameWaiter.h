// FrameWaiter.h — the one wait the message loop performs each turn.
//
// FLUENTUI_INTERNAL — implementation detail of the message loop.
//
// WHY THIS EXISTS. FramePacing computes *when* the next frame is due; this decides
// *how* to wait that long without oversleeping. Those are separate problems and the
// second one is where the frame rate was being lost.
//
// The loop's wait has to satisfy two constraints at once:
//   1. Wake for window messages, or input latency regresses.
//   2. Wake at a sub-millisecond deadline, or a high-refresh panel is unreachable.
//
// MsgWaitForMultipleObjectsEx satisfies (1) but its dwMilliseconds argument is
// rounded UP to the system timer period — 15.6ms by default, 5ms on the machine
// this was measured on. So the old code, asking for an 8ms wait on a 120Hz panel,
// actually slept 14.65ms and pinned the loop at 68fps. The frames themselves were
// cheap (1-3ms of CPU against an 8.33ms budget); the loop was just asleep.
//
// The fix is to keep MsgWaitForMultipleObjectsEx — it is the only primitive that
// wakes for messages — but hand it an OBJECT to wait on instead of a timeout: a
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION waitable timer, armed with a relative
// 100-nanosecond deadline. The wait then returns on whichever comes first, the
// timer or a message, and the timer's resolution is independent of the system
// timer period. Measured on a 120Hz panel, with kFrameWakeMarginMs applied:
//
//     approach                                    mean interval    fps
//     timeout=8ms, default timer resolution        14.65ms          68
//     timeout=8ms, after timeBeginPeriod(1)         7.39ms         135  (overshoots)
//     high-resolution waitable timer                8.33ms         120  <- chosen
//
// WHY NOT timeBeginPeriod(1). It reaches the target too, but it is a process-wide
// (historically machine-wide) change to the scheduler tick that raises timer
// interrupt frequency and power draw for everything in the process, and it has to
// be paired perfectly across every early-return path or it leaks. Note also that it
// overshot to 135fps: 8ms rounded to a 1ms period still is not 8.33ms, so it burns
// ~13% of its frames on work the panel never displays. A waitable timer is local to
// this object, needs no global state, and expresses the actual deadline.
//
// WHY NOT DCompositionWaitForCompositorClock. It does not wake for window messages,
// so it cannot replace the wait the loop needs for input — the same reason
// FramePacing.h gives for rejecting it. It is also Windows 11 only.
//
// FALLBACK. CREATE_WAITABLE_TIMER_HIGH_RESOLUTION requires Windows 10 1803. If the
// handle cannot be created, Wait() degrades to the old whole-millisecond timeout —
// correct, just coarse (68fps rather than 120 on a 120Hz panel). That is the same
// behavior as before this class existed, so the fallback is a known-good path
// rather than an untested one.
#pragma once

#include "../fl_common.h"
#include "FramePacing.h"

namespace fluent {

class FrameWaiter {
public:
    FrameWaiter() {
        // TIMER_MODIFY_STATE | SYNCHRONIZE is all we need (arm it, wait on it).
        // Asking for less than TIMER_ALL_ACCESS keeps the handle unprivileged.
        timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) {
            // Pre-1803, or the flag was rejected. Fall back to a plain waitable
            // timer if possible; Wait() copes with either, and with neither.
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0,
                                            TIMER_MODIFY_STATE | SYNCHRONIZE);
            highResolution_ = false;
            TraceMsg("FrameWaiter",
                     timer_ ? "high-resolution timer unavailable; using coarse timer"
                            : "no waitable timer; falling back to timeout waits");
        }
    }

    ~FrameWaiter() {
        if (timer_) CloseHandle(timer_);
    }

    FrameWaiter(const FrameWaiter&) = delete;
    FrameWaiter& operator=(const FrameWaiter&) = delete;

    // True when the high-resolution timer is in use (diagnostics / tests).
    bool IsHighResolution() const { return timer_ != nullptr && highResolution_; }

    // Block until a message arrives, or until `remainingMs` has elapsed, whichever
    // is first. `remainingMs <= 0` means the frame is already due: returns
    // immediately after draining any pending input, so a due frame is never delayed.
    // A negative-infinity style "wait forever" is expressed by passing kWaitForever.
    //
    // Idle (nothing scheduled) MUST pass kWaitForever so the loop blocks on input
    // alone and idle costs zero CPU — that property is load-bearing and predates
    // this class.
    void Wait(double remainingMs) {
        if (remainingMs == kWaitForever) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE);
            return;
        }

        // Already due (or within noise of it): do not sleep at all. Still drain
        // input with a zero wait so a burst of messages is not deferred a frame.
        if (FrameIsDue(remainingMs)) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 0, QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE);
            return;
        }

        if (timer_) {
            // Negative due time = relative, in 100ns units. This is where the
            // sub-millisecond precision comes from: 8.03ms becomes 80300, not 8.
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(remainingMs * 10000.0);
            if (SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
                MsgWaitForMultipleObjectsEx(1, &timer_, INFINITE, QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE);
                return;
            }
            // Arming failed — fall through to the timeout path rather than
            // returning, which would turn this into a spin.
        }

        // Fallback: whole-millisecond timeout, rounded down so we never overshoot
        // the deadline. Coarse (bounded by the system timer period) but correct.
        const unsigned ms = static_cast<unsigned>(remainingMs);
        MsgWaitForMultipleObjectsEx(0, nullptr, ms, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }

    // Sentinel for "no frame pending, block until input arrives".
    static constexpr double kWaitForever = -1e300;

private:
    HANDLE timer_ = nullptr;
    bool highResolution_ = true;
};

} // namespace fluent
