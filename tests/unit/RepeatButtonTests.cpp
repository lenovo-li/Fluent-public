// RepeatButtonTests.cpp — unit tests for RepeatButton's auto-repeat state machine.
//
// Headless: the repeat is driven by OnAnimationTick(dtSec), so a test can advance
// virtual time by calling it directly with the dt it wants. No timer, no window, no
// GPU — which is exactly why the repeat was built on the frame callback rather than
// on a Win32 timer.
//
// The press/release gesture is delivered through the real UIElement pointer path
// (OnPointerPressed / OnPointerReleased) rather than by poking state, so these
// tests exercise the same capture + pointerInside_ + UpdateState chain the app does.

#include "../framework/Test.h"
#include "../../FluentUI/controls/RepeatButton.h"

using namespace fluent;

namespace {
void CountClicks(void* ctx, Button&, RoutedEventArgs&) { (*static_cast<int*>(ctx))++; }

// Press inside the control's bounds. Mirrors what InputManager delivers.
void PressAt(RepeatButton& b, float x, float y) {
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position = {x, y};
    b.OnPointerPressed(e);
}

void ReleaseAt(RepeatButton& b, float x, float y) {
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position = {x, y};
    b.OnPointerReleased(e);
}
} // namespace

// A press fires Click exactly once immediately — a single quick click on a
// RepeatButton must behave like a plain Button, not fire twice.
TEST(RepeatButton, PressFiresOnceImmediately) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(b.IsRepeating());
}

// Ticking for less than InitialDelay must NOT produce a second fire. This is the
// guard against a plain click double-firing.
TEST(RepeatButton, NoRepeatBeforeInitialDelay) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetInitialDelay(0.25f);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);
    EXPECT_EQ(fired, 1);

    b.OnAnimationTick(0.10f);   // total 0.10 < 0.25
    b.OnAnimationTick(0.10f);   // total 0.20 < 0.25
    EXPECT_EQ(fired, 1);        // still just the press
}

// Once InitialDelay elapses, the first repeat fires.
TEST(RepeatButton, FiresAfterInitialDelay) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetInitialDelay(0.25f);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);
    b.OnAnimationTick(0.30f);   // crosses the initial delay
    EXPECT_EQ(fired, 2);        // press + first repeat
}

// After the initial delay, repeats come at Interval. Two intervals of dt produce
// two more fires.
TEST(RepeatButton, RepeatsAtInterval) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetInitialDelay(0.10f);
    b.SetInterval(0.05f);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);        // fire 1
    b.OnAnimationTick(0.10f);  // initial delay done -> fire 2
    EXPECT_EQ(fired, 2);

    b.OnAnimationTick(0.05f);  // one interval -> fire 3
    EXPECT_EQ(fired, 3);
    b.OnAnimationTick(0.05f);  // another -> fire 4
    EXPECT_EQ(fired, 4);
}

// Releasing stops the burst: no further ticks fire.
TEST(RepeatButton, ReleaseStopsRepeat) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetInitialDelay(0.05f);
    b.SetInterval(0.05f);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);
    b.OnAnimationTick(0.05f);
    const int afterHold = fired;
    EXPECT_TRUE(afterHold >= 2);

    ReleaseAt(b, 20, 20);       // release inside also raises the click gesture
    EXPECT_FALSE(b.IsRepeating());

    const int afterRelease = fired;
    b.OnAnimationTick(1.0f);    // a full second of ticks
    EXPECT_EQ(fired, afterRelease);   // nothing more fired
}

// Releasing OUTSIDE the bounds (dragged off the arrow) must also stop the burst.
// The base gesture reports not-inside, so State() leaves Pressed and the burst ends.
TEST(RepeatButton, ReleaseOutsideStopsRepeat) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetInitialDelay(0.05f);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);
    b.OnAnimationTick(0.05f);
    ReleaseAt(b, 500, 500);     // far outside
    EXPECT_FALSE(b.IsRepeating());

    const int afterRelease = fired;
    b.OnAnimationTick(1.0f);
    EXPECT_EQ(fired, afterRelease);
}

// A long stall must not deliver an unbounded burst. The per-frame cap means a 10s
// frozen frame produces a few clicks, not three hundred.
TEST(RepeatButton, LongStallIsCapped) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetInitialDelay(0.01f);
    b.SetInterval(0.01f);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);         // fire 1
    b.OnAnimationTick(0.01f);   // initial delay -> fire 2
    const int before = fired;

    b.OnAnimationTick(10.0f);   // a 10-second stall: 1000 intervals' worth
    // Capped at a handful per frame, NOT 1000.
    EXPECT_TRUE(fired - before <= 4);
}

// Interval has a floor so a caller cannot request a busy-loop.
TEST(RepeatButton, IntervalIsClampedToFloor) {
    RepeatButton b;
    b.SetInterval(0.0f);
    EXPECT_TRUE(b.Interval() >= 0.008f);

    b.SetInterval(-5.0f);
    EXPECT_TRUE(b.Interval() >= 0.008f);
}

// A disabled RepeatButton takes no press at all (the base gesture requires
// enabled), so it must never start repeating.
TEST(RepeatButton, DisabledDoesNotRepeat) {
    RepeatButton b;
    b.SetBounds({0, 0, 40, 40});
    b.SetEnabled(false);
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);

    PressAt(b, 20, 20);
    EXPECT_EQ(fired, 0);
    EXPECT_FALSE(b.IsRepeating());

    b.OnAnimationTick(1.0f);
    EXPECT_EQ(fired, 0);
}
