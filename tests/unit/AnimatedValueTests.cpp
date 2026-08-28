// AnimatedValueTests.cpp — unit tests for the AnimatedValue easing helper.
//
// AnimatedValue is pure math: no GPU, no window, no D2D. The tests verify the
// exponential-approach convergence, the Animating() predicate, immediate mode,
// and that the float conversion operator works for downstream render code.

#include "../framework/Test.h"
#include "../../FluentUI/animation/AnimatedValue.h"

using namespace fluent;

// A freshly constructed AnimatedValue is at 0 and reports not animating
// (target would also be 0 in the typical "start at false" case).
TEST(AnimatedValue, DefaultIsZeroAndNotAnimating) {
    AnimatedValue av;
    EXPECT_NEAR(static_cast<float>(av), 0.0f, 0.0001f);
    EXPECT_FALSE(av.Animating(0.0f));
}

// After a single large tick the value moves toward the target but doesn't
// overshoot (exponential decay is bounded).
TEST(AnimatedValue, ApproachMovesTowardTarget) {
    AnimatedValue av{0.0f};
    av.Approach(1.0f, 0.016f, 0.05f);
    float v = static_cast<float>(av);
    EXPECT_TRUE(v > 0.0f);  // moved toward target
    EXPECT_TRUE(v < 1.0f);  // did not overshoot
}

// After enough ticks with a small tau the value snaps to the target and
// Animating() returns false.
TEST(AnimatedValue, ConvergesAndSettles) {
    AnimatedValue av{0.0f};
    int guard = 0;
    while (av.Animating(1.0f) && guard++ < 2000)
        av.Approach(1.0f, 0.016f, 0.05f);
    EXPECT_FALSE(av.Animating(1.0f));
    EXPECT_NEAR(static_cast<float>(av), 1.0f, 0.0001f);
}

// Animating() uses the supplied epsilon; a tighter epsilon keeps it animating
// longer than a loose one.
TEST(AnimatedValue, AnimatingRespectsEpsilon) {
    AnimatedValue av{0.0f};
    // Do one tick so the value is partway there.
    av.Approach(1.0f, 0.016f, 0.05f);
    float v = static_cast<float>(av);
    // With tight epsilon (0.0001) it is still animating.
    EXPECT_TRUE(av.Animating(1.0f, 0.0001f));
    // With loose epsilon equal to the remaining gap it is settled.
    float gap = 1.0f - v + 0.001f;
    EXPECT_FALSE(av.Animating(1.0f, gap));
}

// SetImmediate jumps directly without easing and terminates animation.
TEST(AnimatedValue, SetImmediateJumps) {
    AnimatedValue av{0.0f};
    av.SetImmediate(0.75f);
    EXPECT_NEAR(static_cast<float>(av), 0.75f, 0.0001f);
    EXPECT_FALSE(av.Animating(0.75f));
}

// tau <= 0 means instantaneous: Approach snaps immediately.
TEST(AnimatedValue, ZeroTauSnapsImmediately) {
    AnimatedValue av{0.0f};
    av.Approach(1.0f, 0.016f, 0.0f);
    EXPECT_NEAR(static_cast<float>(av), 1.0f, 0.0001f);
    EXPECT_FALSE(av.Animating(1.0f));
}

// The float conversion operator reads the current value unchanged so render
// code that does  float p = std::clamp(static_cast<float>(anim_), 0,1)  is correct.
TEST(AnimatedValue, FloatConversionIsTransparent) {
    AnimatedValue av{0.42f};
    float v = static_cast<float>(av);
    EXPECT_NEAR(v, 0.42f, 0.0001f);
}
