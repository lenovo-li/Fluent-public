// SessionLogTests.cpp — unit tests for the in-memory session diagnostic log
// (diagnostics/SessionLog.h). Pure: no window/GPU/file I/O — RecordFrame /
// RecordEvent / Serialize operate entirely in memory.

#include "../framework/Test.h"
#include "../../FluentUI/diagnostics/SessionLog.h"
#include "../../FluentUI/window/FramePacing.h"  // FrameIntervalIsCadenceSample

#include <string>

using namespace fluent;

namespace {
// Build a FrameStats with the fields the log reads.
FrameStats MakeFrame(double cpuMs, double layMs, double renMs, double preMs) {
    FrameStats f;
    f.cpuFrameMs = cpuMs;
    f.layoutMs = layMs;
    f.renderMs = renMs;
    f.presentMs = preMs;
    return f;
}
} // namespace

// A disabled log ignores all input and serializes an (empty-session) header.
TEST(SessionLog, InactiveIgnoresInput) {
    SessionLog log;
    // Not Start()ed: RecordFrame/RecordEvent are no-ops.
    log.RecordFrame(MakeFrame(5, 1, 3, 1), 50.0f, SessionPhase::Resizing, 1.0);
    log.RecordEvent("input", "click", 1.0);
    EXPECT_EQ(log.FrameCount(), size_t(0));
    EXPECT_EQ(log.EventCount(), size_t(0));
    EXPECT_TRUE(!log.Active());
}

// Frames aggregate per phase; a slow frame lands in the jank list; the report
// contains the phase summary and the worst-frame breakdown.
TEST(SessionLog, RecordsFramesAndJank) {
    SessionLog log;
    log.Start(/*dpiPct*/ 200, /*wpx*/ 1000, /*hpx*/ 700, /*wdip*/ 500, /*hdip*/ 350,
              /*hz*/ 120);
    log.SetJankThresholdMs(33.0f);

    // Two smooth idle frames (~120fps => ~8.3ms interval): no jank.
    log.RecordFrame(MakeFrame(2, 0.2, 1.5, 0.3), 8.3f, SessionPhase::Idle, 0.10);
    log.RecordFrame(MakeFrame(2, 0.2, 1.5, 0.3), 8.3f, SessionPhase::Idle, 0.20);
    // One slow resize frame (interval 40ms > threshold => jank), present-heavy.
    log.RecordFrame(MakeFrame(14.0, 0.3, 11.8, 1.9), 40.0f, SessionPhase::Resizing, 0.30);

    EXPECT_EQ(log.FrameCount(), size_t(3));

    const std::string r = log.Serialize();
    // Header reflects the DPI (the resize-diagnosis context).
    EXPECT_TRUE(r.find("DPI=200%") != std::string::npos);
    // Per-phase summary present for both phases seen.
    EXPECT_TRUE(r.find("Idle") != std::string::npos);
    EXPECT_TRUE(r.find("Resizing") != std::string::npos);
    // The worst frames section lists the hitched resize frame.
    EXPECT_TRUE(r.find("Worst frames") != std::string::npos);
}

// Events appear in the serialized timeline.
TEST(SessionLog, RecordsEvents) {
    SessionLog log;
    log.Start(100, 800, 600, 800, 600, 60);
    log.RecordEvent("modal", "enter size/move loop", 1.25);
    log.RecordEvent("ProgressSweep", "bounds=(24,290) dpi=2.000", 2.00);

    EXPECT_EQ(log.EventCount(), size_t(2));
    const std::string r = log.Serialize();
    EXPECT_TRUE(r.find("enter size/move loop") != std::string::npos);
    EXPECT_TRUE(r.find("ProgressSweep") != std::string::npos);
}

// Clear() resets to an inactive, empty state.
TEST(SessionLog, ClearResets) {
    SessionLog log;
    log.Start(100, 800, 600, 800, 600, 60);
    log.RecordEvent("x", "y", 1.0);
    log.Clear();
    EXPECT_TRUE(!log.Active());
    EXPECT_EQ(log.EventCount(), size_t(0));
    EXPECT_EQ(log.FrameCount(), size_t(0));
}

// --- Animation tick cost (compositor surface refills) -------------------------
// The animation tick runs in PumpAnimations, BEFORE the frame, so its cost is not
// part of cpuFrameMs. A control's compositor scroll surface is refilled there, which
// for a long document is the dominant UI-thread cost of the whole scroll path — so
// the log has to carry it separately or that cost is invisible after the fact.
// (animationMs was declared but never assigned for a long time; these tests exist so
// it cannot silently go back to reading zero.)

namespace {
FrameStats MakeFrameWithTick(double cpuMs, double tickMs) {
    FrameStats f;
    f.cpuFrameMs = cpuMs;
    f.animationMs = tickMs;
    return f;
}
} // namespace

TEST(SessionLog, ReportsWorstTickPerPhase) {
    SessionLog log;
    log.Start(200, 1000, 700, 500, 350, 120);
    // A cheap frame that paid for an expensive refill, then an expensive frame that
    // did not: the worst tick and the worst CPU frame are deliberately different
    // frames, which is exactly why the tick is tracked on its own.
    log.RecordFrame(MakeFrameWithTick(/*cpu*/ 1.0, /*tick*/ 9.5),
                    16.0f, SessionPhase::Animating, 1.0);
    log.RecordFrame(MakeFrameWithTick(/*cpu*/ 7.0, /*tick*/ 0.2),
                    16.0f, SessionPhase::Animating, 2.0);

    const std::string r = log.Serialize();
    EXPECT_TRUE(r.find("worstTick=9.50") != std::string::npos);
    // The worst-CPU frame is still the 7ms one, unperturbed by the tick tracking.
    EXPECT_TRUE(r.find("cpu=7.00") != std::string::npos);
}

TEST(SessionLog, JankEntryCarriesTheTickCost) {
    SessionLog log;
    log.Start(200, 1000, 700, 500, 350, 120);
    log.SetJankThresholdMs(30.0f);
    // A hitch whose cause is the refill, not the frame: cpu is small, tick is huge.
    log.RecordFrame(MakeFrameWithTick(/*cpu*/ 1.5, /*tick*/ 40.0),
                    48.0f, SessionPhase::Animating, 1.0);

    const std::string r = log.Serialize();
    EXPECT_TRUE(r.find("tick=40.00") != std::string::npos);
    // Without the tick column this hitch would look unexplained (cpu 1.5ms, 48ms gap).
    EXPECT_TRUE(r.find("interval=  48.0") != std::string::npos);
}

TEST(SessionLog, ZeroTickStillReported) {
    // A frame with no animation tick reports 0 rather than omitting the column, so a
    // regression that stops assigning animationMs shows up as a flat 0.00.
    SessionLog log;
    log.Start(100, 800, 600, 800, 600, 60);
    log.RecordFrame(MakeFrameWithTick(2.0, 0.0), 16.0f, SessionPhase::Idle, 1.0);
    const std::string r = log.Serialize();
    EXPECT_TRUE(r.find("worstTick=0.00") != std::string::npos);
}

// --- Idle gaps are not hitches ------------------------------------------------
// This is an on-demand renderer: while idle it deliberately paints nothing until
// something changes, so the gap before the next frame is just how long the user sat
// still. Counting that as jank drowned the real hitches — a measured session reported
// 25 "jank" frames of which 17 were idle gaps (one 414ms gap that cost 1.01ms of CPU).

TEST(SessionLog, IdleGapIsNotJank) {
    SessionLog log;
    log.Start(100, 800, 600, 800, 600, 120);
    log.SetJankThresholdMs(33.0f);
    // A 414ms gap while idle with a trivially cheap frame: the user simply did nothing.
    log.RecordFrame(MakeFrame(1.01, 0.0, 0.27, 0.68), 414.1f, SessionPhase::Idle, 1.0);

    const std::string r = log.Serialize();
    EXPECT_TRUE(r.find("jankFrames=0") != std::string::npos);
    // The frame is still counted and still aggregated into the Idle phase — only the
    // hitch classification is suppressed.
    EXPECT_EQ(log.FrameCount(), size_t(1));
    EXPECT_TRUE(r.find("Idle") != std::string::npos);
}

TEST(SessionLog, GapStillJanksWhenFramesWereWanted) {
    // The same gap during a phase that WANTED continuous frames is a genuine hitch.
    for (SessionPhase p : {SessionPhase::Animating, SessionPhase::Resizing,
                           SessionPhase::Moving, SessionPhase::Dpi}) {
        SessionLog log;
        log.Start(100, 800, 600, 800, 600, 120);
        log.SetJankThresholdMs(33.0f);
        log.RecordFrame(MakeFrame(1.01, 0.0, 0.27, 0.68), 414.1f, p, 1.0);
        EXPECT_TRUE(log.Serialize().find("jankFrames=1") != std::string::npos);
    }
}

TEST(SessionLog, HeaderSaysIdleIsExcluded) {
    // Whoever reads the log must not conclude "idle never hitches" from a low count.
    SessionLog log;
    log.Start(100, 800, 600, 800, 600, 120);
    log.RecordFrame(MakeFrame(1, 0, 0.3, 0.3), 8.3f, SessionPhase::Idle, 1.0);
    EXPECT_TRUE(log.Serialize().find("idle excluded") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The interval the host feeds in (NativeWindowHost.cpp, sessionLogEnabled_ block)
//
// SCOPE, stated because it limits what these tests can prove: RecordFrame takes
// intervalMs as a parameter, so the decision "real gap or 0" belongs to the caller,
// and the caller needs a window. What is testable headless is the COMPOSITION the
// caller performs — FrameIntervalIsCadenceSample deciding, SessionLog consuming —
// so `HostInterval` below mirrors that one expression. A regression typed directly
// into NativeWindowHost.cpp is NOT caught by these; what is caught is a regression in
// the rule they share, and the consequence is spelled out so the next reader knows
// why zeroing here is not harmless.
// ---------------------------------------------------------------------------

namespace {
// Mirrors the host call site: a cadence sample passes its real gap through, anything
// else contributes 0. Keep this in step with NativeWindowHost.cpp.
float HostInterval(double gapMs, bool hasPreviousFrame, bool loopWasContinuous) {
    return FrameIntervalIsCadenceSample(hasPreviousFrame, loopWasContinuous)
               ? static_cast<float>(gapMs)
               : 0.0f;
}
} // namespace

// THE REGRESSION THIS EXISTS FOR. The call site carried its own `gap <= 500.0`
// threshold long after that rule was rejected for the interval ring, so a frame that
// genuinely cost 3 seconds was recorded as intervalMs = 0: absent from the jank list
// and pulling the phase average DOWN, in a log whose entire purpose is proving timing
// regressions after the fact. The worst hitches were the ones guaranteed to be missing.
TEST(SessionLog, MultiSecondFrameSurvivesToTheJankLog) {
    SessionLog log;
    log.Start(100, 800, 600, 800, 600, 120);
    log.SetJankThresholdMs(33.0f);

    // A resize drag: one catastrophic re-wrap frame among cheap catch-up frames. The
    // loop never blocked on input, so every gap is a cadence sample.
    log.RecordFrame(MakeFrame(2.0, 0.5, 1.0, 0.4),
                    HostInterval(8.3, true, true), SessionPhase::Resizing, 1.0);
    log.RecordFrame(MakeFrame(2980.0, 2900.0, 70.0, 3.0),
                    HostInterval(3000.0, true, true), SessionPhase::Resizing, 4.0);

    const std::string r = log.Serialize();
    EXPECT_TRUE(r.find("jankFrames=1") != std::string::npos);
    // The worst interval must report the real duration, not a clamped or zeroed one.
    EXPECT_TRUE(r.find("worstInterval=3000.0ms") != std::string::npos);
}

// The zeroing was not merely a missing entry: 0 enters sumIntervalMs and inflates the
// phase's avgFps, so a stuttering session reads as a fast one. Asserted via avgFps
// because that is the number a reader actually looks at.
TEST(SessionLog, ZeroedIntervalWouldInflateAverageFps) {
    // Correct: two frames at 8.3ms and 3000ms -> avg 1504ms -> well under 1 fps.
    SessionLog good;
    good.Start(100, 800, 600, 800, 600, 120);
    good.RecordFrame(MakeFrame(2, 0.5, 1, 0.4),
                     HostInterval(8.3, true, true), SessionPhase::Resizing, 1.0);
    good.RecordFrame(MakeFrame(2980, 2900, 70, 3),
                     HostInterval(3000.0, true, true), SessionPhase::Resizing, 4.0);
    // avg interval = (8.3 + 3000) / 2 = 1504.15ms -> 0.66 fps, printed "%-5.0f" -> "1".
    EXPECT_TRUE(good.Serialize().find("avgFps=1  ") != std::string::npos);

    // What the old threshold produced: the 3s frame contributes 0, so the average
    // collapses to ~4.15ms and the report claims 241 fps during a visible stall.
    SessionLog bad;
    bad.Start(100, 800, 600, 800, 600, 120);
    bad.RecordFrame(MakeFrame(2, 0.5, 1, 0.4), 8.3f, SessionPhase::Resizing, 1.0);
    bad.RecordFrame(MakeFrame(2980, 2900, 70, 3), 0.0f, SessionPhase::Resizing, 4.0);
    const std::string br = bad.Serialize();
    EXPECT_TRUE(br.find("jankFrames=0") != std::string::npos);
    EXPECT_TRUE(br.find("avgFps=241") != std::string::npos);
}

// An idle resume still contributes 0 — that part of the old behaviour was correct and
// must not regress in the other direction while fixing the above. Note the gap here is
// SMALLER than the slow frame above: magnitude is not what decides it, continuity is.
TEST(SessionLog, IdleResumeStillContributesZero) {
    EXPECT_EQ(HostInterval(414.1, /*hasPreviousFrame*/ true,
                           /*loopWasContinuous*/ false), 0.0f);
    // And the very first frame, which has no predecessor to measure against.
    EXPECT_EQ(HostInterval(8.3, /*hasPreviousFrame*/ false,
                           /*loopWasContinuous*/ true), 0.0f);
    // While a continuous loop passes the same 414ms through as a real hitch.
    EXPECT_EQ(HostInterval(414.1, true, true), 414.1f);
}

// --- Jank threshold must scale with the display, not sit at a 60 Hz constant ------
//
// WHY THIS MATTERS MORE THAN IT LOOKS. The threshold decides which frames get recorded
// as hitches, so it is the instrument that has to work on hardware the developer does
// not own. A fixed 33 ms is roughly two frames at 60 Hz — the intended meaning — but at
// 240 Hz a frame is 4.17 ms, so 33 ms is EIGHT missed frames. On a high-refresh machine
// the log would therefore report a clean session while the window visibly stuttered,
// and the developer (whose CPU is fast enough that nothing feels wrong either) would
// have no signal at all. That combination is precisely how jank reaches slower machines.
//
// Start() now derives the threshold from the refresh rate it is handed, so "jank" means
// the same THING (a visible two-frame hitch) rather than the same NUMBER everywhere.
TEST(SessionLog, JankThresholdDerivesFromRefreshRate) {
    SessionLog log;

    log.Start(100, 800, 600, 800, 600, /*refreshHz*/ 60);
    std::printf("   60 Hz -> jank threshold %.2f ms\n", log.JankThresholdMs());
    EXPECT_NEAR(log.JankThresholdMs(), 2.0f * 1000.0f / 60.0f, 0.01f);   // 33.3

    log.Start(100, 800, 600, 800, 600, /*refreshHz*/ 120);
    std::printf("  120 Hz -> jank threshold %.2f ms\n", log.JankThresholdMs());
    EXPECT_NEAR(log.JankThresholdMs(), 2.0f * 1000.0f / 120.0f, 0.01f);  // 16.7

    // The developer's own panel. A 33 ms constant would be 8 frames here.
    log.Start(100, 800, 600, 800, 600, /*refreshHz*/ 240);
    std::printf("  240 Hz -> jank threshold %.2f ms\n", log.JankThresholdMs());
    EXPECT_NEAR(log.JankThresholdMs(), 8.33f, 0.05f);
    EXPECT_TRUE(log.JankThresholdMs() < 33.0f);   // the actual bug being fixed
}

// Absurd or unknown refresh rates must not produce a nonsense threshold. 0 and 1 both
// mean "unknown" from EnumDisplaySettingsW, and a 1000 Hz panel would give a 2 ms
// threshold that scheduler noise alone would trip, filling the log with false hitches.
TEST(SessionLog, JankThresholdHasAFloorAndAnUnknownFallback) {
    SessionLog log;

    log.Start(100, 800, 600, 800, 600, /*refreshHz*/ 0);
    EXPECT_NEAR(log.JankThresholdMs(), 33.0f, 0.01f);   // documented fallback

    log.Start(100, 800, 600, 800, 600, /*refreshHz*/ 1000);
    std::printf("  1000 Hz -> jank threshold %.2f ms (floored)\n", log.JankThresholdMs());
    EXPECT_NEAR(log.JankThresholdMs(), 8.0f, 0.01f);    // floor, not 2.0
}

// A frame that is jank at 240 Hz but not at 60 Hz must be RECORDED when the session was
// started on a 240 Hz display. This is the end-to-end version of the two tests above: it
// asserts the recording behaviour rather than just the threshold field, so the fix cannot
// regress by setting the threshold correctly and then ignoring it.
//
// 12 ms is chosen to sit between the two thresholds: comfortably under 60 Hz's 33.3 ms
// (three frames at 240 Hz is not a hitch worth logging on a 60 Hz panel — it is less than
// one frame there) and clearly over 240 Hz's 8.33 ms.
TEST(SessionLog, HitchThatOnlyMattersAtHighRefreshIsRecordedThere) {
    constexpr float kInterval = 12.0f;

    // Asserted on the jankFrames COUNT in the header, which is the actual verdict.
    // Two things that look usable are not: the "Worst frames" heading is emitted
    // unconditionally (so finding it proves nothing), and the interval string itself
    // appears in the per-phase summary as worstInterval regardless of whether the frame
    // was classified as jank. Both were tried and both passed vacuously.
    SessionLog fast;
    fast.Start(100, 800, 600, 800, 600, /*refreshHz*/ 240);
    fast.RecordFrame(MakeFrame(11.0, 4.0, 6.0, 1.0), kInterval,
                     SessionPhase::Animating, 0.10);
    const std::string fastReport = fast.Serialize();
    EXPECT_TRUE(fastReport.find("jankFrames=1") != std::string::npos);
    EXPECT_TRUE(fastReport.find("threshold=8.3ms") != std::string::npos);

    SessionLog slow;
    slow.Start(100, 800, 600, 800, 600, /*refreshHz*/ 60);
    slow.RecordFrame(MakeFrame(11.0, 4.0, 6.0, 1.0), kInterval,
                     SessionPhase::Animating, 0.10);
    const std::string slowReport = slow.Serialize();
    // Same frame, same interval, but under one 60 Hz frame — correctly not a hitch there.
    EXPECT_TRUE(slowReport.find("jankFrames=0") != std::string::npos);
    EXPECT_TRUE(slowReport.find("threshold=33.3ms") != std::string::npos);
}
