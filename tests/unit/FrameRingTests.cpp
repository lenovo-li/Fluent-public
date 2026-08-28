// FrameRingTests.cpp — unit tests for the P95/P99 frame-time ring (WP-07 §S2).

#include "../framework/Test.h"
#include "../../FluentUI/diagnostics/FrameRing.h"

using namespace fluent;

// A fresh ring reports nothing.
TEST(FrameRing, EmptyRingIsZero) {
    FrameRing<8> ring;
    EXPECT_EQ(ring.Count(), static_cast<size_t>(0));
    EXPECT_NEAR(ring.P50(), 0.0f, 0.001f);
    EXPECT_NEAR(ring.P95(), 0.0f, 0.001f);
    EXPECT_NEAR(ring.Max(), 0.0f, 0.001f);
}

// Count grows with pushes and saturates at capacity.
TEST(FrameRing, CountSaturatesAtCapacity) {
    FrameRing<4> ring;
    ring.Push(1.0f);
    ring.Push(2.0f);
    EXPECT_EQ(ring.Count(), static_cast<size_t>(2));
    ring.Push(3.0f);
    ring.Push(4.0f);
    ring.Push(5.0f);   // overwrites the oldest (1.0)
    ring.Push(6.0f);   // overwrites 2.0
    EXPECT_EQ(ring.Count(), static_cast<size_t>(4));
    // Oldest two (1,2) were overwritten; the live set is {3,4,5,6}.
    EXPECT_NEAR(ring.Max(), 6.0f, 0.001f);
}

// P50/P95/P99 on a known distribution.
TEST(FrameRing, PercentilesOnSortedInput) {
    FrameRing<16> ring;
    // 10 samples: 1..10.
    for (int i = 1; i <= 10; ++i) ring.Push(static_cast<float>(i));
    EXPECT_EQ(ring.Count(), static_cast<size_t>(10));
    // rank = round(p/100 * (n-1)); n=10 → n-1=9.
    // P50 → round(0.5*9)=round(4.5)=5 → sorted[5]=6.
    EXPECT_NEAR(ring.P50(), 6.0f, 0.001f);
    // P95 → round(0.95*9)=round(8.55)=9 → sorted[9]=10.
    EXPECT_NEAR(ring.P95(), 10.0f, 0.001f);
    // P99 → round(0.99*9)=round(8.91)=9 → sorted[9]=10.
    EXPECT_NEAR(ring.P99(), 10.0f, 0.001f);
}

// Percentile is order-independent (ring holds a multiset, sorted internally).
TEST(FrameRing, PercentileOrderIndependent) {
    FrameRing<16> ring;
    float vals[] = {5.0f, 1.0f, 9.0f, 3.0f, 7.0f};
    for (float v : vals) ring.Push(v);
    // sorted = {1,3,5,7,9}, n=5, n-1=4.
    // P50 → round(0.5*4)=2 → sorted[2]=5.
    EXPECT_NEAR(ring.P50(), 5.0f, 0.001f);
    EXPECT_NEAR(ring.Max(), 9.0f, 0.001f);
}

// P0 returns the minimum; P100 returns the maximum.
TEST(FrameRing, ExtremePercentiles) {
    FrameRing<8> ring;
    for (int i = 1; i <= 5; ++i) ring.Push(static_cast<float>(i * 2));  // 2,4,6,8,10
    EXPECT_NEAR(ring.Percentile(0.0f), 2.0f, 0.001f);
    EXPECT_NEAR(ring.Percentile(100.0f), 10.0f, 0.001f);
}

// A single sample: every percentile is that sample.
TEST(FrameRing, SingleSample) {
    FrameRing<8> ring;
    ring.Push(3.5f);
    EXPECT_NEAR(ring.P50(), 3.5f, 0.001f);
    EXPECT_NEAR(ring.P95(), 3.5f, 0.001f);
    EXPECT_NEAR(ring.P99(), 3.5f, 0.001f);
    EXPECT_NEAR(ring.Max(), 3.5f, 0.001f);
}

// Clear resets the ring to empty.
TEST(FrameRing, ClearResets) {
    FrameRing<8> ring;
    ring.Push(1.0f);
    ring.Push(2.0f);
    ring.Clear();
    EXPECT_EQ(ring.Count(), static_cast<size_t>(0));
    EXPECT_NEAR(ring.P95(), 0.0f, 0.001f);
}

// After wrap-around, percentiles reflect only the live window.
TEST(FrameRing, WrapAroundKeepsRecentWindow) {
    FrameRing<4> ring;
    // Push 100 large frames then 4 small ones: only the last 4 survive.
    for (int i = 0; i < 100; ++i) ring.Push(1000.0f);
    ring.Push(1.0f);
    ring.Push(2.0f);
    ring.Push(3.0f);
    ring.Push(4.0f);
    EXPECT_EQ(ring.Count(), static_cast<size_t>(4));
    EXPECT_NEAR(ring.Max(), 4.0f, 0.001f);  // the 1000s are gone
}
