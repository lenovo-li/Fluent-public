// RangeBaseTests.cpp — unit tests for RangeBase value management (WP-06 Stage 2).

#include "../framework/Test.h"
#include "../../FluentUI/controls/Slider.h"
#include "../../FluentUI/controls/ProgressBar.h"

using namespace fluent;

// ---------------------------------------------------------------------------
// RangeBase via Slider
// ---------------------------------------------------------------------------

TEST(RangeBase, DefaultRange) {
    Slider s;
    EXPECT_NEAR(s.Minimum(), 0.0f, 0.001f);
    EXPECT_NEAR(s.Maximum(), 100.0f, 0.001f);
    EXPECT_NEAR(s.Value(), 0.0f, 0.001f);
}

TEST(RangeBase, SetValueClampsToRange) {
    Slider s;
    s.SetValue(-10.0f);
    EXPECT_NEAR(s.Value(), 0.0f, 0.001f);
    s.SetValue(200.0f);
    EXPECT_NEAR(s.Value(), 100.0f, 0.001f);
}

TEST(RangeBase, SetMinimumClampsExistingValue) {
    Slider s;
    s.SetValue(30.0f);
    s.SetMinimum(50.0f); // value below new min should be clamped
    EXPECT_NEAR(s.Value(), 50.0f, 0.001f);
}

TEST(RangeBase, SetMaximumClampsExistingValue) {
    Slider s;
    s.SetValue(80.0f);
    s.SetMaximum(60.0f); // value above new max should be clamped
    EXPECT_NEAR(s.Value(), 60.0f, 0.001f);
}

TEST(RangeBase, SetValueSnapsToStep) {
    Slider s;
    s.SetMin(0.0f);
    s.SetMax(10.0f);
    s.SetStep(2.0f);
    s.SetValue(3.4f); // nearest step: 4
    EXPECT_NEAR(s.Value(), 4.0f, 0.01f);
    s.SetValue(2.9f); // nearest step: 2
    EXPECT_NEAR(s.Value(), 2.0f, 0.01f);
}

TEST(RangeBase, ValueChangedEventFires) {
    Slider s;
    float last = -1.0f;
    struct H { static void OnChange(void* ctx, Slider&, float& v) { *static_cast<float*>(ctx) = v; } };
    auto sub = s.ValueChanged().Subscribe(&last, &H::OnChange);
    s.SetValue(42.0f);
    EXPECT_NEAR(last, 42.0f, 0.001f);
}

TEST(RangeBase, ValueChangedNotFiredWhenUnchanged) {
    Slider s;
    s.SetValue(50.0f);
    int count = 0;
    struct H { static void OnChange(void* ctx, Slider&, float&) { (*static_cast<int*>(ctx))++; } };
    auto sub = s.ValueChanged().Subscribe(&count, &H::OnChange);
    s.SetValue(50.0f); // same value — no event
    EXPECT_EQ(count, 0);
}

// ---------------------------------------------------------------------------
// ProgressBar — fixed 0..1 range via RangeBase
// ---------------------------------------------------------------------------

TEST(RangeBase, ProgressBarRange01) {
    ProgressBar pb;
    EXPECT_NEAR(pb.Minimum(), 0.0f, 0.001f);
    EXPECT_NEAR(pb.Maximum(), 1.0f, 0.001f);
}

TEST(RangeBase, ProgressBarClampsTo01) {
    ProgressBar pb;
    pb.SetValue(2.0f);
    EXPECT_NEAR(pb.Value(), 1.0f, 0.001f);
    pb.SetValue(-0.5f);
    EXPECT_NEAR(pb.Value(), 0.0f, 0.001f);
}

TEST(RangeBase, ProgressBarIndeterminate) {
    ProgressBar pb;
    EXPECT_FALSE(pb.IsIndeterminate());
    pb.SetIndeterminate(true);
    EXPECT_TRUE(pb.IsIndeterminate());
    // Setting a value exits indeterminate mode.
    pb.SetValue(0.5f);
    EXPECT_FALSE(pb.IsIndeterminate());
}

TEST(RangeBase, ProgressBarAnimationTick) {
    ProgressBar pb;
    pb.SetIndeterminate(true);
    EXPECT_TRUE(pb.WantsAnimationTick());  // always wants tick in indeterminate
    pb.SetIndeterminate(false);
    pb.SetValue(1.0f);
    // tick converges the fill toward 1.0
    int guard = 0;
    while (pb.WantsAnimationTick() && guard++ < 2000)
        pb.OnAnimationTick(0.016f);
    EXPECT_FALSE(pb.WantsAnimationTick());
}
