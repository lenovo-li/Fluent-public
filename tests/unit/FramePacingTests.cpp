// FramePacingTests.cpp — unit tests for the pure frame-pacing helper
// (window/FramePacing.h).
//
// Context: the window content used to be a swap chain whose Present1 blocked on
// vsync, so the message loop got pacing for free and passed timeout=0. Route 2
// replaced it with a DComp virtual surface and Commit() does not block, so a loop
// still passing 0 spins — a real 36s session logged 69762 frames on a 120Hz panel
// (~1927 fps). These tests pin the replacement arithmetic.

#include "../framework/Test.h"
#include "../../FluentUI/window/FramePacing.h"

using namespace fluent;

// --- RefreshIntervalMs --------------------------------------------------------

TEST(FramePacing, IntervalForCommonRefreshRates) {
    EXPECT_NEAR(RefreshIntervalMs(60), 16.667, 0.01);
    EXPECT_NEAR(RefreshIntervalMs(120), 8.333, 0.01);
    EXPECT_NEAR(RefreshIntervalMs(144), 6.944, 0.01);
    EXPECT_NEAR(RefreshIntervalMs(240), 4.167, 0.01);
}

TEST(FramePacing, UnknownRefreshFallsBackTo60Hz) {
    // 0 = EnumDisplaySettings could not tell us. A wrong-but-bounded interval is far
    // better than an unpaced spin, so this must not return 0.
    EXPECT_NEAR(RefreshIntervalMs(0), 16.667, 0.01);
    EXPECT_NEAR(RefreshIntervalMs(-1), 16.667, 0.01);
}

TEST(FramePacing, AbsurdRefreshRatesAreClamped) {
    // Garbage from a virtual display / RDP session must not produce a microscopic or
    // enormous interval.
    EXPECT_NEAR(RefreshIntervalMs(1), 16.667, 0.01);       // below the 24Hz floor
    EXPECT_NEAR(RefreshIntervalMs(100000), 16.667, 0.01);  // above the 1000Hz ceiling
    // Legitimate extremes stay honoured.
    EXPECT_NEAR(RefreshIntervalMs(24), 1000.0 / 24.0, 0.01);
    EXPECT_NEAR(RefreshIntervalMs(1000), 1.0, 0.01);
}

// --- FrameWaitMs --------------------------------------------------------------

TEST(FramePacing, WaitsTheRemainderOfTheInterval) {
    // 120Hz = 8.33ms. 1ms in -> ~7.3ms left -> truncates to 7 (never overshoot).
    EXPECT_EQ(FrameWaitMs(1.0, 120), 7u);
    // 60Hz = 16.67ms. 4ms in -> ~12.7ms left -> 12.
    EXPECT_EQ(FrameWaitMs(4.0, 60), 12u);
}

TEST(FramePacing, ZeroWhenFrameAlreadyDue) {
    // At or past the deadline the loop must render immediately. This is NOT a spin:
    // painting advances lastFrameQpc_, so the next turn has a fresh deadline.
    EXPECT_EQ(FrameWaitMs(8.333, 120), 0u);
    EXPECT_EQ(FrameWaitMs(50.0, 120), 0u);   // badly overdue (we were blocked)
    EXPECT_EQ(FrameWaitMs(16.7, 60), 0u);
}

TEST(FramePacing, SubMillisecondRemainderDoesNotSyscall) {
    // Under ~1ms left, waiting costs more than it saves — and a timeout of 0 vs 1 is
    // indistinguishable at this scale anyway.
    EXPECT_EQ(FrameWaitMs(7.8, 120), 0u);   // 0.53ms left
    EXPECT_EQ(FrameWaitMs(16.0, 60), 0u);   // 0.67ms left
}

TEST(FramePacing, NeverSleepsPastTheDeadline) {
    // Truncation (not rounding) is deliberate: sleeping long enough to miss the
    // deadline drops a frame, while waking early only costs one extra loop turn.
    // Sweep the whole interval and assert the wait never exceeds what remains.
    for (int hz : {60, 120, 144, 240}) {
        const double interval = RefreshIntervalMs(hz);
        for (double e = 0.0; e < interval; e += 0.1) {
            const unsigned w = FrameWaitMs(e, hz);
            EXPECT_TRUE(static_cast<double>(w) <= interval - e);
        }
    }
}

TEST(FramePacing, BoundsTheFrameRateToTheRefresh) {
    // The property that matters: pacing must stop the ~1927 fps spin. A frame painted
    // JUST NOW (elapsed ~0) must be told to wait very nearly a whole interval —
    // returning 0 here is precisely the busy-loop regression, so this asserts a
    // non-zero wait directly rather than substituting the nominal interval for it.
    for (int hz : {60, 120, 240}) {
        const double interval = RefreshIntervalMs(hz);
        const unsigned w = FrameWaitMs(0.0, hz);
        EXPECT_TRUE(w > 0u);                                   // must actually wait
        EXPECT_TRUE(static_cast<double>(w) >= interval - 1.0);  // ~a full interval
        // The implied cadence is bounded by the truncated wait, so it can exceed the
        // panel by the sub-ms remainder that truncation drops (120Hz waits 8ms, not
        // 8.33ms -> 125fps). That is the deliberate trade: waking early costs one loop
        // turn, waking late drops a frame. What matters is it is ~the refresh and not
        // the ~1927 fps of an unpaced loop.
        EXPECT_TRUE(1000.0 / static_cast<double>(w) <= 1000.0 / (interval - 1.0));
    }
}

TEST(FramePacing, AFreshFrameAlwaysWaits) {
    // Same invariant stated at the granularity the loop actually hits: after painting,
    // elapsed is a fraction of a millisecond, and the loop must still be told to wait.
    // Without this the loop renders back-to-back at CPU speed.
    for (int hz : {60, 120, 144, 240}) {
        EXPECT_TRUE(FrameWaitMs(0.0, hz) > 0u);
        EXPECT_TRUE(FrameWaitMs(0.2, hz) > 0u);
    }
}

// --- FrameWaitRemainingMs -----------------------------------------------------
//
// The real-valued form. FrameWaitMs truncates to whole milliseconds, which is what
// a wait timeout takes — but a whole-millisecond timeout is rounded UP to the system
// timer period by MsgWaitForMultipleObjectsEx (15.6ms by default, 5ms as measured),
// so a 120Hz panel asking for 8ms actually slept 14.65ms and the loop ran at 68fps.
// These tests pin the sub-millisecond arithmetic that fixes it.

TEST(FramePacing, RemainingKeepsSubMillisecondPrecision) {
    // The whole point: no truncation. 120Hz, 1ms elapsed -> 8.333 - 1 - 0.30 margin.
    EXPECT_NEAR(FrameWaitRemainingMs(1.0, 120), 7.033, 0.001);
    // Contrast with the millisecond form, which throws the fraction away.
    EXPECT_EQ(FrameWaitMs(1.0, 120), 7u);
}

TEST(FramePacing, RemainingSubtractsTheWakeMargin) {
    // A freshly painted frame must be told to wait the interval MINUS the measured
    // wake cost. Without the margin the loop wakes ~0.3ms late every frame and the
    // error compounds into 115fps instead of 120 on a 120Hz panel.
    EXPECT_NEAR(FrameWaitRemainingMs(0.0, 120), 8.333 - kFrameWakeMarginMs, 0.001);
    EXPECT_NEAR(FrameWaitRemainingMs(0.0, 60), 16.667 - kFrameWakeMarginMs, 0.001);
}

TEST(FramePacing, WakeMarginIsSmallerThanAnyRefreshInterval) {
    // The margin must never swallow a whole interval, or the loop would consider
    // every frame due the instant it painted and spin. 1000Hz (1ms) is the clamp
    // ceiling and therefore the tightest case.
    EXPECT_TRUE(kFrameWakeMarginMs < RefreshIntervalMs(1000));
    // And it must be small enough that it cannot invert the sign for a fresh frame.
    for (int hz : {24, 60, 120, 144, 240, 1000})
        EXPECT_TRUE(FrameWaitRemainingMs(0.0, hz) > 0.0);
}

TEST(FramePacing, RemainingGoesNegativeWhenOverdue) {
    // Negative means "already due, paint now" — the loop must not clamp this to 0
    // and lose the information that it is behind.
    EXPECT_TRUE(FrameWaitRemainingMs(8.333, 120) <= 0.0);
    EXPECT_TRUE(FrameWaitRemainingMs(50.0, 120) < 0.0);
    EXPECT_TRUE(FrameWaitRemainingMs(16.7, 60) <= 0.0);
}

// --- FrameIsDue ---------------------------------------------------------------

TEST(FramePacing, FrameIsDueAtAndPastTheDeadline) {
    EXPECT_TRUE(FrameIsDue(0.0));
    EXPECT_TRUE(FrameIsDue(-1.0));
    EXPECT_TRUE(FrameIsDue(-50.0));
}

TEST(FramePacing, FrameIsNotDueWithTimeRemaining) {
    EXPECT_FALSE(FrameIsDue(1.0));
    EXPECT_FALSE(FrameIsDue(8.0));
    // Just above the epsilon is still worth waiting for.
    EXPECT_FALSE(FrameIsDue(kFrameDueEpsilonMs * 2.0));
}

TEST(FramePacing, FrameIsDueWithinTheEpsilon) {
    // Sub-epsilon remainders count as due: a syscall costs more than the wait saves.
    EXPECT_TRUE(FrameIsDue(kFrameDueEpsilonMs));
    EXPECT_TRUE(FrameIsDue(kFrameDueEpsilonMs / 2.0));
}

TEST(FramePacing, WakingFromTheTimerLeavesTheFrameDue) {
    // The load-bearing interaction between the margin and the due check. The loop
    // waits FrameWaitRemainingMs, so when the timer fires, `elapsed` has advanced by
    // exactly that much — and the frame must then read as DUE. If it did not, the
    // loop would wake, decide to wait again, and spin at CPU speed.
    for (int hz : {60, 120, 144, 240}) {
        const double firstWait = FrameWaitRemainingMs(0.0, hz);
        EXPECT_TRUE(firstWait > 0.0);
        // Simulate the timer firing exactly on time.
        const double remainingAtWake = FrameWaitRemainingMs(firstWait, hz);
        EXPECT_TRUE(FrameIsDue(remainingAtWake));
        // And firing slightly early (timers can be a hair fast) must ALSO be due,
        // otherwise a fast wake turns into a spin.
        EXPECT_TRUE(FrameIsDue(FrameWaitRemainingMs(firstWait - 0.02, hz)));
    }
}

TEST(FramePacing, PacedCadenceMatchesTheRefresh) {
    // End-to-end arithmetic check on the property the user actually observes: with
    // the margin applied, the implied cadence is the panel's refresh rather than the
    // 68fps the truncated-timeout path produced. Allow 1fps of slack for the margin.
    for (int hz : {60, 120, 144, 240}) {
        const double interval = RefreshIntervalMs(hz);
        // One full cycle: wait, then paint. The cycle length is the wait plus the
        // margin the wait deliberately left on the table.
        const double cycle = FrameWaitRemainingMs(0.0, hz) + kFrameWakeMarginMs;
        EXPECT_NEAR(cycle, interval, 0.001);
        EXPECT_NEAR(1000.0 / cycle, static_cast<double>(hz), 1.0);
    }
}

// --- FrameIsNearestOpportunity ------------------------------------------------
//
// The modal-loop rule. Unlike the main loop, the caller cannot wait — it must decide
// "paint or skip" at each externally-delivered message — so skipping costs a whole
// opportunity rather than the fraction of a millisecond that remains.

TEST(FramePacing, NearestOpportunityAlwaysPaintsADueFrame) {
    // Due is due, whatever the gap estimate says. This is the invariant that keeps a
    // large gap estimate from ever *delaying* a frame.
    for (double gap : {0.0, 1.0, 8.0, 100.0}) {
        EXPECT_TRUE(FrameIsNearestOpportunity(0.0, gap));
        EXPECT_TRUE(FrameIsNearestOpportunity(-5.0, gap));   // overdue
    }
}

TEST(FramePacing, NearestOpportunityWithoutAnEstimateFallsBackToTheDueTest) {
    // First opportunity of a drag: no spacing observed yet. Guessing a gap here would
    // risk painting far too early, so the rule degrades to the plain due-test.
    EXPECT_FALSE(FrameIsNearestOpportunity(4.0, 0.0));
    EXPECT_FALSE(FrameIsNearestOpportunity(4.0, -1.0));
    EXPECT_TRUE(FrameIsNearestOpportunity(0.0, 0.0));   // still paints when due
}

TEST(FramePacing, NearestOpportunityPaintsWhenSkippingWouldOvershoot) {
    // gap = 8ms: the next chance is 8ms away. With 3ms left, painting now is 3ms
    // early while waiting would be 5ms late -> paint.
    EXPECT_TRUE(FrameIsNearestOpportunity(3.0, 8.0));
    // With 5ms left, painting is 5ms early and waiting only 3ms late -> skip.
    EXPECT_FALSE(FrameIsNearestOpportunity(5.0, 8.0));
    // Exactly halfway is the boundary, and it must resolve toward painting: erring
    // early costs a fraction of a frame, erring late costs a whole one.
    EXPECT_TRUE(FrameIsNearestOpportunity(4.0, 8.0));
}

TEST(FramePacing, NearestOpportunityThresholdTracksTheGap) {
    // The self-tuning property, and the reason this is not a fixed tolerance. A fast
    // opportunity stream must get a TIGHT threshold (so we do not paint faster than
    // the panel) and a slow one a LOOSE threshold (so jitter never costs a frame).
    // Same `remaining`, opposite decisions, driven only by the gap:
    const double remaining = 3.0;
    EXPECT_FALSE(FrameIsNearestOpportunity(remaining, 1.0));   // 1000Hz mouse: wait
    EXPECT_TRUE(FrameIsNearestOpportunity(remaining, 16.0));   // 60Hz stream: paint
    // And the threshold is monotonic in the gap.
    double prevThreshold = 0.0;
    for (double gap : {1.0, 2.0, 4.0, 8.0, 16.0}) {
        // Find the largest `remaining` still accepted, by construction gap/2.
        const double threshold = gap * 0.5;
        EXPECT_TRUE(FrameIsNearestOpportunity(threshold, gap));
        EXPECT_FALSE(FrameIsNearestOpportunity(threshold + 0.01, gap));
        EXPECT_TRUE(threshold > prevThreshold);
        prevThreshold = threshold;
    }
}

TEST(FramePacing, NearestOpportunityDoesNotAliasAgainstA125HzMouse) {
    // The regression this rule exists for, as a direct simulation. A 120Hz panel and
    // a 125Hz mouse are close enough that a fixed threshold skips every other message
    // once jitter straddles it, halving the rate. Session log evidence: Moving
    // avgFps=45 with sub-3ms frames.
    //
    // Deterministic sawtooth jitter so the test cannot flake.
    const int hz = 120;
    const double interval = RefreshIntervalMs(hz);
    const double mouse = 8.0;   // 125Hz
    const double jitter[] = {-1.0, 0.6, -0.4, 1.0, 0.2, -0.8};

    auto simulate = [&](bool useNearest) {
        double now = 0.0, lastPaint = -interval;
        int painted = 0;
        for (int i = 0; i < 400; ++i) {
            now += mouse + jitter[i % 6];
            const double remaining = FrameWaitRemainingMs(now - lastPaint, hz);
            const bool paint = useNearest ? FrameIsNearestOpportunity(remaining, mouse)
                                          : FrameIsDue(remaining);
            if (paint) { lastPaint = now; ++painted; }
        }
        return painted * 1000.0 / now;   // fps
    };

    const double fixedRuleFps = simulate(false);
    const double nearestFps = simulate(true);
    // The fixed rule aliases down toward half the refresh.
    EXPECT_TRUE(fixedRuleFps < 100.0);
    // Nearest-opportunity holds the panel rate...
    EXPECT_TRUE(nearestFps > 110.0);
    // ...without overshooting it, which would be wasted frames.
    EXPECT_TRUE(nearestFps <= 130.0);
}

// --- UpdateOpportunityGap ----------------------------------------------------

TEST(FramePacing, GapEstimateSeedsFromTheFirstSample) {
    // With no prior estimate the first observation is taken whole: smoothing from 0
    // would start every drag with an artificially tight threshold.
    EXPECT_NEAR(UpdateOpportunityGap(0.0, 8.0), 8.0, 0.001);
    EXPECT_NEAR(UpdateOpportunityGap(-1.0, 8.0), 8.0, 0.001);
}

TEST(FramePacing, GapEstimateConvergesOnASteadyRate) {
    // A stable message stream must converge on its true spacing, or the threshold
    // stays permanently wrong.
    double gap = 16.0;
    for (int i = 0; i < 40; ++i) gap = UpdateOpportunityGap(gap, 4.0);
    EXPECT_NEAR(gap, 4.0, 0.05);
}

TEST(FramePacing, GapEstimateIgnoresNonPositiveObservations) {
    // Two messages in the same QPC tick, or a reordered sample. Folding a 0 in would
    // drag the estimate toward zero and tighten the threshold for no reason.
    EXPECT_NEAR(UpdateOpportunityGap(8.0, 0.0), 8.0, 0.001);
    EXPECT_NEAR(UpdateOpportunityGap(8.0, -3.0), 8.0, 0.001);
}

TEST(FramePacing, GapEstimateRejectsOutliers) {
    // The user pauses mid-drag for a second, then resumes. That one enormous gap says
    // nothing about the current delivery rate, and accepting it would widen the
    // threshold enough to overshoot for the next several frames.
    const double before = 8.0;
    EXPECT_NEAR(UpdateOpportunityGap(before, 1000.0), before, 0.001);
    // Just inside the 8x cutoff is still accepted (a genuine slowdown must land).
    const double accepted = UpdateOpportunityGap(before, before * 7.0);
    EXPECT_TRUE(accepted > before);
}

TEST(FramePacing, GapEstimateIsSmoothedNotSnapped) {
    // One sample must move the estimate part of the way, not all of it — a single
    // jittery delivery should not swing the threshold.
    const double moved = UpdateOpportunityGap(8.0, 4.0);
    EXPECT_TRUE(moved < 8.0);    // moved toward the observation
    EXPECT_TRUE(moved > 4.0);    // but did not jump to it
    EXPECT_NEAR(moved, 8.0 * 0.75 + 4.0 * 0.25, 0.001);
}

// --- Pacing reference point: frame START, not frame END -----------------------
//
// SCOPE, READ THIS BEFORE TRUSTING THESE TESTS. The bug these describe was NOT in
// any function in FramePacing.h — the formula was already correct. It was in which
// timestamp NativeWindowHost fed to it: `lastFrameQpc_` (stamped when a frame
// finishes) instead of `lastFrameStartQpc_` (stamped when it begins). That choice
// lives in RenderNow behind private members and needs a real window and a real
// device, so THESE TESTS CANNOT CATCH A REVERT OF IT. What they do pin is the
// arithmetic that makes the choice matter, so the reasoning survives in a form the
// next person can check, and so a future change to the margin's meaning fails here
// rather than silently costing 10fps again.
//
// The arithmetic: the loop waits `interval - margin` from its reference point, and
// the margin is calibrated to cancel wake latency. If the reference is the frame's
// END, the frame's own cost falls outside the interval it was meant to fit inside,
// so the steady-state cycle is `interval + cost`. Measured in the demo at 0.78ms
// CPU on a 120Hz panel: 9.1ms intervals, 110fps where 120 was expected — and
// perfectly reproducible, because it is a systematic offset, not jitter.
namespace {

// Steady-state cycle length in ms for a loop pacing off `refreshHz`, modelling the
// margin as exactly cancelling wake latency (that is its calibration).
//   costMs     : the frame's own cost
//   startBased : true = reference at frame start, false = at frame end
double ModelPacedCycleMs(int refreshHz, double costMs, bool startBased) {
    const double interval = RefreshIntervalMs(refreshHz);
    const double base = (interval - kFrameWakeMarginMs) + kFrameWakeMarginMs;
    const double cycle = startBased ? base : base + costMs;
    // A frame that outruns the interval paces itself: its deadline is already past.
    return (cycle < costMs) ? costMs : cycle;
}

} // namespace

TEST(FramePacing, EndBasedReferenceInflatesEveryCycleByTheFrameCost) {
    const double interval = RefreshIntervalMs(120);
    for (double cost : {0.5, 0.78, 1.5, 3.0}) {
        EXPECT_NEAR(ModelPacedCycleMs(120, cost, /*startBased*/ false),
                    interval + cost, 0.001);
    }
}

TEST(FramePacing, StartBasedReferenceHoldsTheRefreshRegardlessOfFrameCost) {
    for (int hz : {60, 120, 144, 240}) {
        const double interval = RefreshIntervalMs(hz);
        for (double cost : {0.1, 0.5, 0.78, 1.5}) {
            if (cost >= interval) continue;   // see the degradation test below
            EXPECT_NEAR(ModelPacedCycleMs(hz, cost, /*startBased*/ true),
                        interval, 0.001);
        }
    }
}

TEST(FramePacing, TheReferencePointIsWorthTenFpsOnA120HzPanel) {
    // The user-visible number, at the frame cost the demo's HUD reported.
    const double cost = 0.78;
    const double endFps = 1000.0 / ModelPacedCycleMs(120, cost, false);
    const double startFps = 1000.0 / ModelPacedCycleMs(120, cost, true);
    EXPECT_TRUE(endFps < 112.0);      // the regression as observed
    EXPECT_TRUE(startFps > 119.0);    // what the fix should produce
    EXPECT_TRUE(startFps <= 120.5);   // without overshooting the panel
}

TEST(FramePacing, AnOverBudgetFrameDegradesToItsOwnCostNotWorse) {
    // A frame that cannot fit the interval must not additionally be made to wait.
    // Both reference points agree here; the test guards against a future "always
    // wait at least X" tweak introducing a stall.
    const double interval = RefreshIntervalMs(120);
    for (double cost : {10.0, 20.0, 50.0}) {
        EXPECT_TRUE(cost > interval);
        EXPECT_NEAR(ModelPacedCycleMs(120, cost, true), cost, 0.001);
    }
}

// ---------------------------------------------------------------------------
// Cadence-sample classification (FrameIntervalIsCadenceSample)
//
// The HUD's FPS/jank percentiles are only meaningful over intervals during which the
// loop was actually trying to paint. These tests pin the rule that replaced a
// "gap > 500ms means the window was idle" heuristic — a heuristic that silently
// discarded every genuinely slow frame and reported four-digit FPS on a visibly
// stuttering resize drag.
// ---------------------------------------------------------------------------

// The first frame has no predecessor, so there is no interval to classify.
TEST(FramePacing, FirstFrameIsNotACadenceSample) {
    EXPECT_TRUE(!FrameIntervalIsCadenceSample(/*hasPreviousFrame*/ false,
                                              /*loopWasContinuous*/ true));
    EXPECT_TRUE(!FrameIntervalIsCadenceSample(false, false));
}

// A gap the loop spent blocked on input is not a frame rate, however long it was.
TEST(FramePacing, IdleGapIsNotACadenceSample) {
    EXPECT_TRUE(!FrameIntervalIsCadenceSample(/*hasPreviousFrame*/ true,
                                              /*loopWasContinuous*/ false));
}

// THE REGRESSION THIS EXISTS FOR. A frame that genuinely cost seconds is a cadence
// sample and must be kept. Under the old magnitude test it was indistinguishable from
// an idle resume and was thrown away — which is how the ring came to hold only the
// cheap frames between the expensive ones.
TEST(FramePacing, ASlowFrameIsStillACadenceSample) {
    // Continuity is what decides it; the duration of the gap does not enter.
    EXPECT_TRUE(FrameIntervalIsCadenceSample(/*hasPreviousFrame*/ true,
                                             /*loopWasContinuous*/ true));
}

// The classification cannot depend on how big the gap is: same inputs, same answer,
// whether the frame took a microsecond or a minute. This is the property the old
// threshold lacked, so it is asserted directly rather than implied.
TEST(FramePacing, CadenceClassificationIsIndependentOfGapSize) {
    // Simulate the bimodal drag: sub-millisecond catch-up frames interleaved with
    // multi-second re-wrap frames, all of them continuous.
    for (double gapMs : {0.2, 0.9, 8.33, 120.0, 499.0, 501.0, 900.0, 3000.0}) {
        (void)gapMs;  // deliberately unused: the rule must not consult it
        EXPECT_TRUE(FrameIntervalIsCadenceSample(true, true));
        EXPECT_TRUE(!FrameIntervalIsCadenceSample(true, false));
    }
}
