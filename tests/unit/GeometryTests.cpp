// GeometryTests.cpp — pure-logic tests for the DIP geometry / layout value
// types (RectDip, Thickness, GridLength, kAuto). No GPU, no window.

#include "../framework/Test.h"
#include "../../FluentUI/fl_common.h"
#include "../../FluentUI/core/Layout.h"

using namespace fluent;

TEST(Geometry, RectContainsIsHalfOpen) {
    RectDip r{10.0f, 20.0f, 100.0f, 50.0f};
    EXPECT_EQ(r.right(), 110.0f);
    EXPECT_EQ(r.bottom(), 70.0f);
    // Top-left corner is inside; bottom-right edge is exclusive.
    EXPECT_TRUE(r.contains(10.0f, 20.0f));
    EXPECT_TRUE(r.contains(109.9f, 69.9f));
    EXPECT_FALSE(r.contains(110.0f, 70.0f));   // right/bottom edge excluded
    EXPECT_FALSE(r.contains(9.9f, 20.0f));     // left of rect
    EXPECT_FALSE(r.contains(10.0f, 19.9f));    // above rect
}

TEST(Geometry, ThicknessSums) {
    Thickness t{2.0f, 4.0f, 6.0f, 8.0f};
    EXPECT_EQ(t.horizontal(), 8.0f);   // left + right
    EXPECT_EQ(t.vertical(), 12.0f);    // top + bottom

    Thickness all{5.0f};
    EXPECT_EQ(all.left, 5.0f);
    EXPECT_EQ(all.right, 5.0f);
    EXPECT_EQ(all.horizontal(), 10.0f);

    Thickness hv{3.0f, 7.0f};  // (horizontal, vertical)
    EXPECT_EQ(hv.left, 3.0f);
    EXPECT_EQ(hv.right, 3.0f);
    EXPECT_EQ(hv.top, 7.0f);
    EXPECT_EQ(hv.bottom, 7.0f);
}

TEST(Geometry, AutoSentinelIsNaN) {
    EXPECT_TRUE(IsAuto(kAuto));
    EXPECT_FALSE(IsAuto(0.0f));
    EXPECT_FALSE(IsAuto(-1.0f));
    EXPECT_FALSE(IsAuto(100.0f));
}

TEST(Geometry, GridLengthKinds) {
    GridLength px = GridLength::Pixels(120.0f);
    EXPECT_TRUE(px.isPixel());
    EXPECT_FALSE(px.isStar());
    EXPECT_FALSE(px.isAuto());
    EXPECT_EQ(px.value, 120.0f);

    GridLength au = GridLength::Auto();
    EXPECT_TRUE(au.isAuto());

    GridLength st = GridLength::Star(2.0f);
    EXPECT_TRUE(st.isStar());
    EXPECT_EQ(st.value, 2.0f);

    // Default-constructed is a 1-weight star.
    GridLength def;
    EXPECT_TRUE(def.isStar());
    EXPECT_EQ(def.value, 1.0f);
}
