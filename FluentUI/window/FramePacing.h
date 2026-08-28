// FramePacing.h — pure "how long may the loop sleep before the next frame is due".
//
// WHY THIS EXISTS: the content used to be a swap chain, and Present1 blocked on
// vsync, so the message loop got its pacing for free — it could ask
// MsgWaitForMultipleObjectsEx for a 0 timeout and rely on Present for
// backpressure. Route 2 replaced the swap chain with an IDCompositionVirtualSurface,
// and Commit() does NOT block. With the frame-latency waitable gone too, a loop that
// still passes timeout=0 whenever something animates spins as fast as the CPU allows:
// a 36s session on a 120Hz display measured 69762 frames (~1927 fps, ~16x the panel)
// — invisible on screen because DWM only samples at vsync, but it burns a core and
// makes every FPS/interval number in the diagnostics meaningless.
//
// This computes the milliseconds to wait so frames land about one refresh apart.
// Deliberately NOT DCompositionWaitForCompositorClock: that blocks on the compositor
// tick only and does not wake for window messages, so it cannot replace the
// MsgWaitForMultipleObjectsEx that the loop needs for input. Feeding a real timeout
// to that same wait keeps one wait primitive, keeps input latency unchanged, and
// works on Windows 10 (the compositor-clock API is Windows 11 / NTDDI_WIN10_CO,
// and its DCOMPOSITION_MAX_WAITFORCOMPOSITORCLOCK_OBJECTS cap is referenced by the
// SDK annotation but never actually defined in the header).
//
// Pacing is a floor on the frame INTERVAL, not a deadline: waiting slightly too long
// costs at most one frame of latency, while waiting too little costs a wasted frame.
#pragma once

#include <cstdint>

namespace fluent {

// Milliseconds between refreshes for `refreshHz`, clamped to a sane range. 0 /
// nonsense input falls back to 60Hz — a wrong-but-bounded interval is far better than
// an unpaced spin. The clamp tolerates 24Hz projectors up to 1000Hz panels.
inline double RefreshIntervalMs(int refreshHz) {
    if (refreshHz < 24 || refreshHz > 1000) return 1000.0 / 60.0;
    return 1000.0 / static_cast<double>(refreshHz);
}

// How long the loop may wait before the next frame is due, given when the last frame
// was painted. Returns a whole number of milliseconds suitable for a wait timeout.
//
//   elapsedMs : time since the last painted frame
//   refreshHz : monitor refresh (0 = unknown -> 60Hz assumed)
//
// Rounds DOWN so we never sleep past the deadline, and returns 0 when the frame is
// already due (the loop then renders immediately — correct, not a spin, because the
// deadline advances once the frame is painted). A 120Hz panel yields 8.33ms, so a
// loop 1ms into the interval waits 7ms and wakes just before the frame is due.
//
// CAUTION — this millisecond form cannot pace a high-refresh panel on its own, and
// is kept only for the callers that need a "is it due yet" boolean (ServiceModalFrame
// tests == 0) or a coarse timeout fallback. A whole-millisecond timeout handed to
// MsgWaitForMultipleObjectsEx is rounded UP to the system timer period, which is
// 15.6ms by default and was 5ms on the machine this was measured on. Measured there:
// a 120Hz panel asking for an 8ms wait actually slept 14.65ms, pinning the loop at
// 68fps — the frames were cheap (1-3ms CPU), the loop was simply oversleeping.
// Use FrameWaitRemainingMs + a high-resolution waitable timer for the real wait.
inline unsigned FrameWaitMs(double elapsedMs, int refreshHz) {
    const double interval = RefreshIntervalMs(refreshHz);
    if (elapsedMs >= interval) return 0u;               // already due
    const double remaining = interval - elapsedMs;
    if (remaining <= 1.0) return 0u;                    // sub-ms: not worth a syscall
    return static_cast<unsigned>(remaining);            // truncate: never overshoot
}

// Fixed slice subtracted from every computed wait, in milliseconds, to absorb the
// cost of waking from the wait and getting back to the paint call.
//
// This is a MEASURED constant, not a guess. With no margin, a high-resolution
// waitable timer on a 120Hz panel produced a mean interval of 8.62-8.71ms against
// the ideal 8.333ms, i.e. 115-116fps: each wake arrives a fraction of a millisecond
// late and the error accumulates one frame at a time. Sweeping the margin at three
// simulated frame costs (0.5 / 1.5 / 3.0ms CPU):
//
//     margin   mean interval    fps      dropped frames
//     0.00ms   8.62 - 8.71ms    115-116  3 of 245 at 3ms CPU
//     0.20ms   8.43 - 8.49ms    118-119  0
//     0.30ms   8.33 - 8.36ms    120      0        <- chosen
//     0.50ms   8.11 - 8.12ms    123      0        overshoots the panel
//
// 0.30ms lands on the refresh boundary at every tested load with nothing dropped.
// Erring small is the safer direction: waking slightly early only means the frame
// is ready before the compositor samples it, whereas waking late costs a whole
// refresh. Overshooting (0.50ms) is wasted work — frames the panel never shows.
inline constexpr double kFrameWakeMarginMs = 0.30;

// Slack around the deadline, below which a wait is not worth a syscall and the
// frame counts as due. Both the "should I sleep" and the "should I paint" decisions
// are expressed through FrameIsDue below so they use the SAME threshold — if they
// disagreed by even a fraction of a millisecond the loop would either spin (sleep
// says no, paint says no) or stutter (both say wait, nothing advances).
inline constexpr double kFrameDueEpsilonMs = 0.05;

// True when the next frame should be painted now rather than waited for.
//
// Deliberately one function feeding both the wait and the paint, in the same spirit
// as VisualOverflowDip feeding both the dirty-rect report and the render cull: the
// two judgments cannot drift apart because there is only one of them.
//
// Note the interaction with kFrameWakeMarginMs. FrameWaitRemainingMs already
// subtracts the margin, so at the moment the timer fires `remainingMs` is ~0 and
// this returns true. The margin is therefore not a fudge factor bolted onto the
// deadline — it IS the deadline the loop targets, and the frame is due when it is
// reached.
inline bool FrameIsDue(double remainingMs) {
    return remainingMs <= kFrameDueEpsilonMs;
}

// Should a frame be painted NOW, when the decision can only be made at discrete,
// externally-timed opportunities and waiting is impossible?
//
// This is the modal-loop counterpart to FrameIsDue, and it is a genuinely different
// question. The main loop CAN wait: it sleeps precisely until the deadline, so "is
// it due" is the whole decision. Inside the system's modal move/resize loop our loop
// is not running and blocking would add latency to the drag, so the window may only
// decide "paint or skip" on each message the foreign pump happens to deliver —
// WM_SIZE / WM_MOVING at the mouse's report rate. Skipping means waiting a whole
// further opportunity, not a fraction of a millisecond.
//
// Applying a due-test there quantizes badly. With a fixed threshold, an opportunity
// arriving a hair early is skipped and the next one lands a full mouse period later,
// so the interval jumps from ~8ms to ~15ms and back — the classic beat pattern, and
// exactly what a session log showed as Moving avgFps=45 with jank P95 of 31ms while
// frames cost under 3ms of CPU. Measured over a simulated second of dragging on a
// 120Hz panel, painting only when `remaining <= 1ms`:
//
//     mouse rate   jitter 0ms   0.5ms   1.0ms   2.0ms
//       125Hz         125        120      93      85
//       250Hz         125        122     109     104
//
// NEAREST-OPPORTUNITY SAMPLING instead. Painting now lands `remaining` early;
// waiting lands `gap - remaining` late. Take whichever is closer, i.e. paint when
// `remaining <= gap/2`. The threshold adapts to the observed opportunity spacing on
// its own: a fast mouse gives a small gap and a tight threshold (so it does not
// overshoot the panel), a slow mouse gives a large gap and a loose one (so it does
// not beat). Same simulation:
//
//     mouse rate   jitter 0ms   0.5ms   1.0ms   2.0ms
//       125Hz         125        125     125     124
//       250Hz         125        125     123     112
//
//   remainingMs : time until the next frame is due (FrameWaitRemainingMs)
//   gapMs       : estimated spacing between opportunities; <= 0 means unknown, in
//                 which case this degrades to a plain due-test rather than guessing
inline bool FrameIsNearestOpportunity(double remainingMs, double gapMs) {
    if (FrameIsDue(remainingMs)) return true;   // already due: never defer
    if (gapMs <= 0.0) return false;             // no estimate yet: wait for due
    return remainingMs <= gapMs * 0.5;
}

// Is the gap between two consecutive painted frames a CADENCE sample — something the
// FPS / jank percentiles should be computed from?
//
// Only when the loop was continuously busy across it. If the loop blocked waiting for
// input in between, the gap measures how long the user sat still, which is not a frame
// rate and would read as one enormous stutter.
//
// WHY THIS IS NOT A SIZE THRESHOLD. The obvious form is "discard gaps over 500ms, they
// must be idle resumes", and that is what this replaced. It conflates the two cases it
// has to separate:
//
//     the window idled 3s waiting for input   -> not a cadence sample
//     the frame genuinely COST 3s             -> the single most important sample
//
// and it resolves them the wrong way on exactly the workload where the numbers are
// being read. A resize drag over a large document makes every real interval exceed the
// threshold; all of them are dropped as "idle" and the ring keeps only the cheap frames
// between them, so the HUD reports a four-digit FPS for a window redrawing about once a
// second. No choice of threshold fixes that — the two cases are not distinguishable by
// magnitude, because a slow frame is genuinely slow for as long as it takes.
//
// Continuity is a property of the scheduler at the end of the previous frame (was a
// frame pending, an animation live, a resize unapplied), so the host samples it there
// and passes it in. Then a 3-second frame is kept, a 3-second idle is dropped, and
// there is no load at which the two get confused.
//
//   hasPreviousFrame : false on the very first frame (no timestamp to subtract)
//   loopWasContinuous: was there pending work when the previous frame ended
inline bool FrameIntervalIsCadenceSample(bool hasPreviousFrame, bool loopWasContinuous) {
    return hasPreviousFrame && loopWasContinuous;
}

// Smoothing factor for the opportunity-spacing estimate. Exponential rather than a
// single last-interval reading because mouse deliveries are jittery: one long gap
// (the user paused, or the system stalled) must not widen the threshold enough to
// start overshooting on the frames that follow.
inline constexpr double kOpportunityGapSmoothing = 0.25;

// Fold one observed spacing into the running estimate.
inline double UpdateOpportunityGap(double previousEstimate, double observedMs) {
    if (observedMs <= 0.0) return previousEstimate;      // duplicate/reordered
    if (previousEstimate <= 0.0) return observedMs;      // first sample seeds it
    // Ignore absurd outliers (the user stopped moving for a second, then resumed):
    // they say nothing about the current delivery rate and would blow the threshold
    // wide open for the next several frames.
    if (observedMs > previousEstimate * 8.0) return previousEstimate;
    return previousEstimate * (1.0 - kOpportunityGapSmoothing) +
           observedMs * kOpportunityGapSmoothing;
}

// Milliseconds remaining until the next frame is due, as a REAL number so a
// high-resolution timer can be programmed with sub-millisecond accuracy. Returns
// <= 0 when the frame is already due (paint now, do not wait).
//
// This is the same judgment as FrameWaitMs without the truncation to whole
// milliseconds and with the wake margin folded in — the two together are what make
// a 120Hz panel actually reach 120fps rather than 68.
inline double FrameWaitRemainingMs(double elapsedMs, int refreshHz) {
    const double interval = RefreshIntervalMs(refreshHz);
    return interval - elapsedMs - kFrameWakeMarginMs;
}

} // namespace fluent
