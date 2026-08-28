// PerformanceCountersTests.cpp — unit tests for the WP-00 diagnostics surface
// (FrameStats / ScopedTimer / QPC helpers). Pure logic: no window, no GPU.

#include "../framework/Test.h"
#include "../../FluentUI/diagnostics/PerformanceCounters.h"

#include <thread>
#include <chrono>

using namespace fluent;

TEST(PerformanceCounters, FrameStatsResetClearsEveryField) {
    FrameStats s;
    s.cpuFrameMs = 5.0;
    s.layoutMs = 1.0;
    s.dirtyElements = 42;
    s.textLayoutsNew = 7;
    s.activeAnimations = 3;
    s.frameReason = 0xFF;
    s.lateFrame = true;

    s.Reset();

    EXPECT_NEAR(s.cpuFrameMs, 0.0, 1e-9);
    EXPECT_NEAR(s.layoutMs, 0.0, 1e-9);
    EXPECT_EQ(s.dirtyElements, 0u);
    EXPECT_EQ(s.textLayoutsNew, 0u);
    EXPECT_EQ(s.activeAnimations, 0u);
    EXPECT_EQ(s.frameReason, 0u);
    EXPECT_TRUE(!s.lateFrame);
}

TEST(PerformanceCounters, ScopedTimerAccumulatesIntoTarget) {
    // Two timed regions targeting the same field must sum (+=), not overwrite.
    double phase = 0.0;
    {
        ScopedTimer t(phase);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    double afterFirst = phase;
    EXPECT_TRUE(afterFirst > 0.0);
    {
        ScopedTimer t(phase);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(phase > afterFirst);  // accumulated, not reset
}

TEST(PerformanceCounters, QpcHelpersAreConsistent) {
    EXPECT_TRUE(QpcFrequency() > 0);
    int64_t a = QpcNow();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    int64_t b = QpcNow();
    EXPECT_TRUE(b >= a);
    // A one-second tick delta converts to ~1000 ms.
    EXPECT_NEAR(QpcToMs(QpcFrequency()), 1000.0, 1.0);
}

TEST(PerformanceCounters, ProcessStatsSampleSucceeds) {
    ProcessStats ps;
    EXPECT_TRUE(ps.Sample());
    EXPECT_TRUE(ps.workingSetBytes > 0);
}
