// FrameWaiterTests.cpp — the loop's wait primitive (window/FrameWaiter.h).
//
// These tests DO touch the clock and the OS scheduler, unlike the rest of the
// suite. That is unavoidable: the entire defect being guarded against is that a
// wait sleeps longer than it was asked to, which is only observable by timing a
// real wait. They are written to be robust anyway:
//
//   * assertions are one-sided BOUNDS, never equality on a duration;
//   * the ceiling is generous enough to survive a loaded CI machine;
//   * the floor is the meaningful half — a wait that returns EARLY would turn the
//     message loop into a spin, and that is a hard error, not jitter.
//
// The specific regression: MsgWaitForMultipleObjectsEx rounds its dwMilliseconds
// argument up to the system timer period (15.6ms by default, 5ms on the machine
// this was diagnosed on). Asking for 8ms on a 120Hz panel slept 14.65ms, pinning
// the loop at 68fps while frames cost 1-3ms of CPU. FrameWaiter fixes it by waiting
// on a high-resolution waitable timer object instead of passing a timeout.
#include "../framework/Test.h"
#include "../../FluentUI/window/FrameWaiter.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace fluent;

namespace {

double QpcMsNow() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 /
           static_cast<double>(freq.QuadPart);
}

// Time a single Wait(requestedMs), returning the elapsed milliseconds.
double TimeWait(FrameWaiter& waiter, double requestedMs) {
    const double start = QpcMsNow();
    waiter.Wait(requestedMs);
    return QpcMsNow() - start;
}

// Median of several samples, so one scheduling hiccup cannot fail the test.
double MedianWait(FrameWaiter& waiter, double requestedMs, int samples = 9) {
    std::vector<double> observed;
    observed.reserve(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i)
        observed.push_back(TimeWait(waiter, requestedMs));
    std::sort(observed.begin(), observed.end());
    return observed[observed.size() / 2];
}

}  // namespace

TEST(FrameWaiter, ReportsWhetherItGotAHighResolutionTimer) {
    FrameWaiter waiter;
    // Not an assertion about the machine — CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    // needs Windows 10 1803. This documents which path the rest of the tests in
    // this file are exercising, and that querying it does not crash.
    const bool hires = waiter.IsHighResolution();
    std::printf("       (FrameWaiter high-resolution timer: %s)\n",
                hires ? "yes" : "no - falling back to timeout waits");
    EXPECT_TRUE(hires || !hires);  // total, by construction
}

TEST(FrameWaiter, DueFrameDoesNotSleep) {
    // A frame already due must be painted now. If Wait blocked here, every overdue
    // frame would be delayed by a further interval and the loop would fall behind
    // under load instead of catching up.
    FrameWaiter waiter;
    EXPECT_TRUE(MedianWait(waiter, 0.0) < 2.0);
    EXPECT_TRUE(MedianWait(waiter, -5.0) < 2.0);
    EXPECT_TRUE(MedianWait(waiter, -100.0) < 2.0);
}

TEST(FrameWaiter, SubEpsilonRemainderDoesNotSleep) {
    // Consistent with FrameIsDue: below the epsilon, a syscall costs more than the
    // wait is worth.
    FrameWaiter waiter;
    EXPECT_TRUE(MedianWait(waiter, kFrameDueEpsilonMs / 2.0) < 2.0);
}

TEST(FrameWaiter, ActuallyWaitsWhenAFrameIsPending) {
    // The floor matters most: returning early makes the loop spin. Allow a small
    // tolerance below the request because a high-resolution timer may fire a hair
    // early, and because the margin in FramePacing already assumes that.
    FrameWaiter waiter;
    const double requested = 8.0;
    const double observed = MedianWait(waiter, requested);
    EXPECT_TRUE(observed >= requested - 1.0);
}

TEST(FrameWaiter, DoesNotOversleepA120HzDeadline) {
    // THE regression test. On a 120Hz panel the loop asks for ~8.03ms
    // (8.333 interval - 0.30 margin). Before FrameWaiter this slept 14.65ms and
    // capped the frame rate at 68fps.
    //
    // The ceiling is set at 12ms: comfortably above a correct wait (~8ms) plus
    // scheduling noise, but below the 14.65ms that the coarse-timeout path
    // produced — so the test distinguishes the fix from the defect rather than
    // merely asserting "some time passed".
    //
    // Skipped when no high-resolution timer is available, because then the coarse
    // behavior is the documented, correct fallback and failing here would be
    // reporting the platform rather than a bug.
    FrameWaiter waiter;
    if (!waiter.IsHighResolution()) {
        std::printf("       (skipped: no high-resolution timer on this platform)\n");
        return;
    }

    const double requested =
        RefreshIntervalMs(120) - kFrameWakeMarginMs;  // ~8.03ms
    const double observed = MedianWait(waiter, requested);
    std::printf("       (120Hz wait: requested %.3fms, median actual %.3fms -> %.1f fps)\n",
                requested, observed, 1000.0 / observed);

    EXPECT_TRUE(observed >= requested - 1.0);  // did not spin
    EXPECT_TRUE(observed < 12.0);              // did not oversleep to ~68fps
}

TEST(FrameWaiter, SustainsRoughlyTheTargetCadenceOver120HzFrames) {
    // Aggregate check: run a short burst of paced waits and assert the achieved
    // cadence is nearer the panel than the 68fps the old path produced. Uses the
    // mean over many frames, which is exactly the quantity the session log reports
    // as avgFps and the quantity the user observed as "60-70".
    FrameWaiter waiter;
    if (!waiter.IsHighResolution()) {
        std::printf("       (skipped: no high-resolution timer on this platform)\n");
        return;
    }

    const double interval = RefreshIntervalMs(120);
    const int frames = 40;
    const double start = QpcMsNow();
    for (int i = 0; i < frames; ++i)
        waiter.Wait(interval - kFrameWakeMarginMs);
    const double total = QpcMsNow() - start;
    const double fps = 1000.0 / (total / frames);
    std::printf("       (sustained cadence over %d waits: %.1f fps)\n", frames, fps);

    // 95fps floor: well above the 68fps defect, well below 120 so a loaded machine
    // does not produce a false failure.
    EXPECT_TRUE(fps > 95.0);
    // And it must not run away faster than the panel, which would be wasted work.
    EXPECT_TRUE(fps < 200.0);
}

TEST(FrameWaiter, WaitForeverSentinelIsDistinctFromADueFrame) {
    // kWaitForever must not be mistaken for "overdue by a lot" — they take opposite
    // branches (block on input vs return immediately). Guard the sentinel's contract
    // without actually blocking forever in a test.
    EXPECT_TRUE(FrameWaiter::kWaitForever < 0.0);
    EXPECT_FALSE(FrameWaiter::kWaitForever == 0.0);
    // Any realistic overdue amount is far above the sentinel, so the comparison in
    // Wait() cannot confuse them.
    EXPECT_TRUE(FrameWaiter::kWaitForever < -1e6);
}
