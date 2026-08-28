// ModalFrameHeartbeat.h — a precise frame tick that survives the system's modal
// move/resize loop.
//
// FLUENTUI_INTERNAL — implementation detail of NativeWindowHost.
//
// THE PROBLEM. While the user drags a window edge, Windows runs its OWN modal
// message loop inside DefWindowProc. Our loop — and therefore PumpAnimations,
// FrameWaiter, and RunDueFrame — is not running. The window can only paint from
// whatever messages that foreign pump happens to dispatch to our WndProc.
//
// Two message sources were available, and both cap the frame rate:
//
//   * WM_SIZE / WM_MOVING — dense while the mouse MOVES, absent when it holds
//     still. Cannot be relied on alone: a user who presses the resize border and
//     holds without moving gets no messages at all.
//   * WM_TIMER (the old kAnimTimerId heartbeat) — the fallback for exactly that
//     motionless case, and the reason a held mouse button dropped the window to
//     ~30fps. Two separate limits stack up:
//       1. USER_TIMER_MINIMUM is 10ms, so ~100fps is the ceiling even nominally,
//          and the constant was 16ms (~62fps) to begin with.
//       2. WM_TIMER is a synthesized, LOW-PRIORITY message: it is only generated
//          when the queue has nothing else, and its period is rounded up to the
//          system timer resolution (15.6ms by default, 5ms as measured here). A
//          16ms request measured ~28.6ms in practice — 35fps, which is what the
//          user reported and what the session log showed as Moving avgFps=45.
//
// THE FIX. Drive the tick from OUTSIDE the message queue's scheduling: a waitable
// timer, created with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (the same primitive
// FrameWaiter uses for the main loop), waited on by a dedicated thread that does
// nothing but PostMessage a private message to the window on each tick. A posted
// message is an ordinary queued message, so the system's modal pump dispatches it
// at normal priority, and its cadence is the timer's — independent of both
// USER_TIMER_MINIMUM and the system timer resolution.
//
// WHY A THREAD, given the repo has none. The tick must be produced while this
// thread is blocked inside DefWindowProc's modal loop, so it cannot come from
// this thread. The alternatives were considered and rejected:
//   * SetWindowsHookEx / a WH_MSGFILTER hook — runs only when the modal loop
//     processes a message, so it does not help the motionless case at all.
//   * CreateTimerQueueTimer — would also work, and is a thread pool rather than a
//     dedicated thread. Rejected because its callbacks come from a shared pool
//     whose timing is coarser and less predictable under load, and because a
//     single-purpose thread that blocks on one handle is easier to reason about
//     for lifetime than a pool callback that may already be in flight during
//     teardown.
//
// THREAD SAFETY. The thread touches exactly three things: its own timer handle,
// an event used to ask it to stop, and the target HWND, which it only ever passes
// to PostMessage (documented as safe from any thread; a message to a destroyed
// HWND fails harmlessly rather than crashing). It reads no window state and calls
// no framework code, so it cannot race the UI thread. All painting still happens
// on the UI thread, inside the WndProc, when the posted message is dispatched.
//
// LIFETIME. Start() is called on WM_ENTERSIZEMOVE and Stop() on WM_EXITSIZEMOVE
// and on window destruction. Stop() signals the stop event and JOINS the thread,
// so no tick can arrive after it returns. Both are idempotent.
#pragma once

#include "../fl_common.h"

#include <atomic>
#include <thread>

namespace fluent {

// The private message posted on each tick. WM_APP+n is the documented range for
// application-private messages, and NativeWindowHost's window class uses none of it.
constexpr UINT WM_FLUENT_MODAL_TICK = WM_APP + 17;

class ModalFrameHeartbeat {
public:
    ModalFrameHeartbeat() = default;
    ~ModalFrameHeartbeat() { Stop(); }

    ModalFrameHeartbeat(const ModalFrameHeartbeat&) = delete;
    ModalFrameHeartbeat& operator=(const ModalFrameHeartbeat&) = delete;

    bool IsRunning() const { return running_; }

    // Begin posting WM_FLUENT_MODAL_TICK to `hwnd` every `periodMs`. Idempotent:
    // a second call while running is ignored (the period is fixed for the life of
    // one drag, which is fine — the refresh rate cannot change mid-drag without a
    // WM_DPICHANGED, and that ends the modal loop).
    void Start(HWND hwnd, double periodMs) {
        if (running_ || !hwnd) return;

        // Clear any count left by the previous drag. A tick posted just before Stop()
        // may never have been dispatched (the queue is drained by the modal loop that
        // is now gone), and starting the next drag with a phantom in-flight tick would
        // suppress the first real one.
        inFlight_.store(0, std::memory_order_release);

        // Manual-reset stop event: the worker waits on it alongside the timer, so
        // a stop request interrupts the wait immediately rather than after the
        // current period elapses.
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_) {
            TraceMsg("ModalHeartbeat", "CreateEventW failed; falling back to WM_TIMER");
            return;
        }

        timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_) {
            // Pre-1803: a coarse waitable timer still beats WM_TIMER, because the
            // win here is as much about message PRIORITY as about resolution.
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0,
                                            TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
        if (!timer_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
            TraceMsg("ModalHeartbeat", "no waitable timer; falling back to WM_TIMER");
            return;
        }

        // Periodic timer: negative due time = relative first fire, then lPeriod
        // milliseconds thereafter. Sub-millisecond precision on the first fire;
        // the repeat period is whole milliseconds, which is the API's limit and is
        // adequate (8ms vs 8.33ms drifts by one frame every 25 frames, and the
        // drag path re-syncs on every WM_SIZE anyway).
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(periodMs * 10000.0);
        const LONG period = static_cast<LONG>(periodMs < 1.0 ? 1.0 : periodMs);
        if (!SetWaitableTimerEx(timer_, &due, period, nullptr, nullptr, nullptr, 0)) {
            CloseHandle(timer_);
            CloseHandle(stopEvent_);
            timer_ = nullptr;
            stopEvent_ = nullptr;
            TraceMsg("ModalHeartbeat", "SetWaitableTimerEx failed; falling back to WM_TIMER");
            return;
        }

        running_ = true;
        // Copy the handles + the in-flight counter the worker needs so it never reads
        // members the UI thread might be clearing during Stop().
        HANDLE timer = timer_;
        HANDLE stop = stopEvent_;
        std::atomic<int>* inFlight = &inFlight_;
        worker_ = std::thread([hwnd, timer, stop, inFlight] {
            HANDLE waits[2] = {stop, timer};
            for (;;) {
                const DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (r != WAIT_OBJECT_0 + 1) break;   // stop signalled, or an error
                // AT MOST ONE UNCONSUMED TICK IN THE QUEUE.
                //
                // The timer keeps firing while the UI thread is busy, and it can be
                // busy for a long time — a resize frame that re-wraps a large document
                // takes as long as it takes. Posting unconditionally then queues one
                // tick per period for the whole duration, and when the UI thread comes
                // back it dispatches the entire backlog. Every one of those calls
                // ServiceModalFrame, and every one finds the deadline long past, so
                // they paint back-to-back.
                //
                // Those frames are near-free (the coalesced size usually has not
                // changed, so nothing re-wraps) and they are indistinguishable from a
                // genuinely fast window: a burst of sub-millisecond intervals is what
                // put "1000+ fps" on the HUD during a visibly stuttering drag. They
                // are also pure waste — the backlog describes refresh boundaries that
                // have already passed and cannot be painted for.
                //
                // Dropping a tick while one is already pending is free of risk: the
                // pending tick has not been serviced yet, so it will do the work this
                // one would have. The counter is decremented by the WndProc handler as
                // it consumes the message.
                if (inFlight->load(std::memory_order_acquire) > 0) continue;
                inFlight->fetch_add(1, std::memory_order_release);
                // PostMessage, never SendMessage: SendMessage would block this
                // thread until the UI thread finished painting, and a stop request
                // arriving mid-paint would then deadlock against the join in Stop().
                if (!PostMessageW(hwnd, WM_FLUENT_MODAL_TICK, 0, 0)) {
                    inFlight->fetch_sub(1, std::memory_order_release);
                    break;
                }
            }
        });
    }

    // Called by the WndProc as it consumes a WM_FLUENT_MODAL_TICK, which re-opens the
    // single in-flight slot above. Safe to call for a stale tick that arrives after
    // Stop() — the counter is reset on the next Start().
    void OnTickConsumed() {
        int prev = inFlight_.load(std::memory_order_acquire);
        while (prev > 0 &&
               !inFlight_.compare_exchange_weak(prev, prev - 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
            // retry with the refreshed `prev`
        }
    }

    // Stop ticking and join the worker. After this returns, no further tick can be
    // posted (though one already in the queue may still be dispatched — the WndProc
    // handler is written to be harmless outside a modal loop). Idempotent.
    void Stop() {
        if (stopEvent_) SetEvent(stopEvent_);
        if (worker_.joinable()) worker_.join();
        if (timer_) {
            CancelWaitableTimer(timer_);
            CloseHandle(timer_);
            timer_ = nullptr;
        }
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        running_ = false;
    }

private:
    std::thread worker_;
    HANDLE timer_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    bool running_ = false;
    // Ticks posted but not yet consumed by the WndProc. Bounded to 1 so a long UI-thread
    // stall cannot queue a backlog that then paints back-to-back — see the worker loop.
    // Written by both threads, hence atomic; it is the only shared mutable state.
    std::atomic<int> inFlight_{0};
};

} // namespace fluent
