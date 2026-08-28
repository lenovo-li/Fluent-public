// ClickCounterTests.cpp — single / double / triple click detection.
//
// The thresholds are injected, so these tests pass explicit values instead of whatever
// the machine's mouse settings happen to be. That is the point of the injection: the
// real code reads GetDoubleClickTime() / SM_CXDOUBLECLK (a user can change both), while
// the logic stays checkable against fixed numbers.
#include "../framework/Test.h"
#include "../../FluentUI/input/ClickCounter.h"

using namespace fluent;

namespace {
constexpr uint32_t kInterval = 500;   // stand-in for GetDoubleClickTime()
constexpr float kSlop = 4.0f;         // stand-in for SM_CXDOUBLECLK
}  // namespace

TEST(ClickCounter, FirstClickIsSingle) {
    ClickCounter c;
    EXPECT_EQ(c.Register(10, 10, 1000, kInterval, kSlop), 1);
}

TEST(ClickCounter, SecondClickInTimeAndPlaceIsDouble) {
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 10, 1200, kInterval, kSlop), 2);
}

TEST(ClickCounter, ThirdClickIsTriple) {
    // Win32 has no triple-click message; this is the whole reason the type exists.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    c.Register(10, 10, 1100, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 10, 1200, kInterval, kSlop), 3);
}

TEST(ClickCounter, FourthClickSaturatesAtThree) {
    // Not 4, and NOT back to 1. Wrapping to 1 would make a fourth rapid click collapse
    // a line selection back to a caret placement — the jitter would look like the
    // selection randomly failing.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    c.Register(10, 10, 1100, kInterval, kSlop);
    c.Register(10, 10, 1200, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 10, 1300, kInterval, kSlop), 3);
    EXPECT_EQ(c.Register(10, 10, 1400, kInterval, kSlop), 3);
}

TEST(ClickCounter, TooSlowRestartsAtSingle) {
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    // 501 ms later: past the interval.
    EXPECT_EQ(c.Register(10, 10, 1501, kInterval, kSlop), 1);
}

TEST(ClickCounter, ExactlyAtIntervalStillCounts) {
    // The comparison is <=, matching "within the double-click time" rather than
    // "strictly faster than". A click at exactly the threshold is a double-click.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 10, 1500, kInterval, kSlop), 2);
}

TEST(ClickCounter, TooFarRestartsAtSingle) {
    // Time alone is not enough: two deliberate clicks on different words are fast but
    // far apart, and must not read as a double-click on the second one.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(20, 10, 1100, kInterval, kSlop), 1);
}

TEST(ClickCounter, WithinSlopCounts) {
    // A hand shake of a couple of pixels must not break a double-click.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(13, 12, 1100, kInterval, kSlop), 2);
}

TEST(ClickCounter, SlopIsABoxNotACircle) {
    // (4,4) is outside a radius-4 circle but inside the 4x4 box. Win32 uses a box, and
    // disagreeing with the OS at the corners would buy nothing.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(14, 14, 1100, kInterval, kSlop), 2);
}

TEST(ClickCounter, VerticalDistanceAlsoBreaksTheStreak) {
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 30, 1100, kInterval, kSlop), 1);
}

TEST(ClickCounter, ResetBreaksTheStreak) {
    // What a window uses on deactivation: clicking away and quickly back must not
    // resume the old streak.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    c.Reset();
    EXPECT_EQ(c.Register(10, 10, 1100, kInterval, kSlop), 1);
}

TEST(ClickCounter, StreakContinuesFromTheLatestPosition) {
    // The reference point is the PREVIOUS click, not the first of the streak, so a
    // slow drift of a few pixels per click keeps the streak alive. That matches how a
    // real hand behaves during a fast triple-click.
    ClickCounter c;
    c.Register(10, 10, 1000, kInterval, kSlop);
    EXPECT_EQ(c.Register(13, 10, 1100, kInterval, kSlop), 2);
    EXPECT_EQ(c.Register(16, 10, 1200, kInterval, kSlop), 3);
}

TEST(ClickCounter, ClockWrapMeasuresElapsedTimeCorrectly) {
    // GetMessageTime wraps every ~49 days. Unsigned subtraction is not merely tolerable
    // here, it is exactly right: 0x10 - 0xFFFFFF00 == 272 in modular arithmetic, which
    // IS the elapsed time across the wrap. So a double-click that straddles the boundary
    // still registers. Doing the subtraction in a signed type would yield a large
    // negative value and break the streak once every 49 days.
    ClickCounter c;
    c.Register(10, 10, 0xFFFFFF00u, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 10, 0x00000010u, kInterval, kSlop), 2);
}

TEST(ClickCounter, FarApartInTimeAcrossWrapStillRestarts) {
    // The companion case: a genuinely long gap that happens to cross the wrap must still
    // restart. 0x300 - 0xFFFFFF00 == 1028 ms > 500, so the streak breaks.
    ClickCounter c;
    c.Register(10, 10, 0xFFFFFF00u, kInterval, kSlop);
    EXPECT_EQ(c.Register(10, 10, 0x00000300u, kInterval, kSlop), 1);
}

TEST(ClickCounter, ZeroIntervalMeansEveryClickIsSingle) {
    // Degenerate threshold, but a user CAN drag the double-click speed slider to its
    // fastest; the type must not divide by it or otherwise misbehave.
    ClickCounter c;
    c.Register(10, 10, 1000, 0, kSlop);
    EXPECT_EQ(c.Register(10, 10, 1001, 0, kSlop), 1);
}
