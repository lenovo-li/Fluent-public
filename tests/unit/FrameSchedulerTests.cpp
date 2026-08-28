// FrameSchedulerTests.cpp — unit tests for the FrameScheduler.
//
// Two halves (roadmap §14):
//   * the animation clock (arm/disarm edges + framerate-independent dt), now on
//     the QPC clock: timestamps are QPC ticks, converted via QpcFrequency();
//   * frame-request coalescing (RequestFrame / NeedsFrame / BeginFrame / EndFrame
//     + FrameReason merge), added in WP-01.
// Pure logic: no window, no GPU.

#include "../framework/Test.h"
#include "../../FluentUI/window/FrameScheduler.h"
#include "../../FluentUI/diagnostics/PerformanceCounters.h"  // QpcFrequency

using namespace fluent;

namespace {
// A QPC tick count `ms` milliseconds after `base`.
int64_t QpcAfterMs(int64_t base, double ms) {
    return base + static_cast<int64_t>(QpcFrequency() * ms / 1000.0);
}
}  // namespace

// ---- animation clock: arm/disarm edges ------------------------------------

// A rising edge arms once; staying wanted does not re-arm.
TEST(FrameScheduler, ArmsOnceOnRisingEdge) {
    int arms = 0, disarms = 0;
    FrameScheduler s;
    s.SetCallbacks([&] { ++arms; }, [&] { ++disarms; });

    EXPECT_FALSE(s.Running());
    s.SetWanted(true);
    EXPECT_TRUE(s.Running());
    EXPECT_EQ(arms, 1);
    s.SetWanted(true);            // already running: no-op
    EXPECT_EQ(arms, 1);
    EXPECT_EQ(disarms, 0);
}

// A falling edge disarms once; staying un-wanted does not re-disarm.
TEST(FrameScheduler, DisarmsOnceOnFallingEdge) {
    int arms = 0, disarms = 0;
    FrameScheduler s;
    s.SetCallbacks([&] { ++arms; }, [&] { ++disarms; });

    s.SetWanted(true);
    s.SetWanted(false);
    EXPECT_FALSE(s.Running());
    EXPECT_EQ(disarms, 1);
    s.SetWanted(false);           // already stopped: no-op
    EXPECT_EQ(disarms, 1);
    EXPECT_EQ(arms, 1);
}

// ---- animation clock: dt (QPC) --------------------------------------------

// The first tick after an arm uses the nominal interval (no bogus elapsed time
// from a stale timestamp).
TEST(FrameScheduler, FirstTickUsesNominalInterval) {
    FrameScheduler s;
    s.SetWanted(true);
    float dt = s.ComputeDt(QpcAfterMs(0, 1'000'000.0));  // large ts, first tick
    EXPECT_NEAR(dt, FrameScheduler::kIntervalMs / 1000.0f, 1e-6f);
}

// Subsequent ticks report the real elapsed time between QPC timestamps.
TEST(FrameScheduler, LaterTicksUseRealElapsed) {
    FrameScheduler s;
    s.SetWanted(true);
    int64_t base = QpcFrequency();       // arbitrary non-zero origin
    s.ComputeDt(base);                   // first tick primes the clock
    float dt = s.ComputeDt(QpcAfterMs(base, 32.0));  // 32 ms later
    EXPECT_NEAR(dt, 0.032f, 1e-4f);
}

// A long gap (stall) is clamped to kMaxDtSec so easing does not jump.
TEST(FrameScheduler, ClampsLargeDt) {
    FrameScheduler s;
    s.SetWanted(true);
    int64_t base = QpcFrequency();
    s.ComputeDt(base);
    float dt = s.ComputeDt(QpcAfterMs(base, 5000.0));  // 5 s stall
    EXPECT_NEAR(dt, FrameScheduler::kMaxDtSec, 1e-6f);
}

// Re-arming resets the clock so the next tick is nominal again, not the elapsed
// time since the pre-disarm tick.
TEST(FrameScheduler, ReArmResetsDtClock) {
    FrameScheduler s;
    s.SetWanted(true);
    s.ComputeDt(QpcFrequency());
    s.SetWanted(false);
    s.SetWanted(true);                   // rising edge again
    float dt = s.ComputeDt(QpcAfterMs(0, 999'999.0));  // unrelated later ts
    EXPECT_NEAR(dt, FrameScheduler::kIntervalMs / 1000.0f, 1e-6f);
}

// ---- frame-request coalescing (WP-01) -------------------------------------

// A fresh scheduler wants no frame.
TEST(FrameScheduler, StartsWithNoPendingFrame) {
    FrameScheduler s;
    EXPECT_FALSE(s.NeedsFrame());
    EXPECT_TRUE(s.PendingReasons() == FrameReason::None);
}

// Multiple requests between frames merge into ONE pending frame; the reasons OR.
TEST(FrameScheduler, CoalescesMultipleRequestsIntoOneFrame) {
    FrameScheduler s;
    s.RequestFrame(FrameReason::Input);
    s.RequestFrame(FrameReason::Layout);
    s.RequestFrame(FrameReason::Input);   // duplicate reason
    EXPECT_TRUE(s.NeedsFrame());
    FrameReason r = s.PendingReasons();
    EXPECT_TRUE(HasReason(r, FrameReason::Input));
    EXPECT_TRUE(HasReason(r, FrameReason::Layout));
    EXPECT_FALSE(HasReason(r, FrameReason::Animation));
}

// BeginFrame snapshots + clears the pending request; EndFrame with no further
// request leaves the scheduler idle.
TEST(FrameScheduler, BeginConsumesEndReachesIdle) {
    FrameScheduler s;
    s.RequestFrame(FrameReason::Paint);
    FrameReason serviced = s.BeginFrame();
    EXPECT_TRUE(HasReason(serviced, FrameReason::Paint));
    EXPECT_FALSE(s.NeedsFrame());          // consumed
    EXPECT_TRUE(s.FrameInProgress());
    s.EndFrame();
    EXPECT_FALSE(s.FrameInProgress());
    EXPECT_FALSE(s.NeedsFrame());          // idle: nothing re-requested
}

// A RequestFrame arriving DURING a frame (between Begin and End) is preserved
// for the next frame — never swallowed by EndFrame.
TEST(FrameScheduler, ReinvalidationDuringFrameIsDeferredNotLost) {
    FrameScheduler s;
    s.RequestFrame(FrameReason::Layout);
    s.BeginFrame();
    // e.g. arranging content triggered another invalidation mid-render:
    s.RequestFrame(FrameReason::Paint);
    s.EndFrame();
    EXPECT_TRUE(s.NeedsFrame());           // still needs another frame
    EXPECT_TRUE(HasReason(s.PendingReasons(), FrameReason::Paint));
    EXPECT_FALSE(HasReason(s.PendingReasons(), FrameReason::Layout));  // that one was serviced
}
