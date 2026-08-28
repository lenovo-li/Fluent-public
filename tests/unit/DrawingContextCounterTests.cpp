// DrawingContextCounterTests.cpp — headless tests for the WP-07 DrawingContext
// additions: ClipHint plumbing and DpiScale accessor. The drawOps increment
// requires a real GPU device context and is verified by code inspection + the
// manual smoke test; only GPU-free behaviour is tested here.

#include "../framework/Test.h"
#include "../../FluentUI/graphics/DrawingContext.h"

using namespace fluent;

// The default ClipHint is effectively unbounded — any reasonably-sized rect
// intersects it, so every Panel child is visited on a full-frame pass.
TEST(DrawingContext, DefaultClipHintIsUnbounded) {
    DrawingContext dc{nullptr, nullptr, 1.0f};
    const RectDip& hint = dc.ClipHint();
    // A small rect anywhere in a large window must intersect the default hint.
    RectDip a{0,   0, 100, 100};
    RectDip b{500, 300, 50,  50};
    RectDip c{-200, -100, 50, 50};  // negative coords still inside the huge hint
    EXPECT_TRUE(a.intersects(hint));
    EXPECT_TRUE(b.intersects(hint));
    EXPECT_TRUE(c.intersects(hint));
}

// A custom ClipHint is stored exactly.
TEST(DrawingContext, CustomClipHintIsStored) {
    RectDip hint{10.0f, 20.0f, 300.0f, 200.0f};
    DrawingContext dc{nullptr, nullptr, 1.5f, nullptr, &hint};
    EXPECT_NEAR(dc.ClipHint().x,  10.0f, 0.001f);
    EXPECT_NEAR(dc.ClipHint().y,  20.0f, 0.001f);
    EXPECT_NEAR(dc.ClipHint().w, 300.0f, 0.001f);
    EXPECT_NEAR(dc.ClipHint().h, 200.0f, 0.001f);
}

// The DpiScale accessor returns what the constructor received.
TEST(DrawingContext, DpiScaleRoundTrips) {
    DrawingContext dc{nullptr, nullptr, 2.0f};
    EXPECT_NEAR(dc.DpiScale(), 2.0f, 0.0001f);
}

// A custom ClipHint is narrower than the default: a rect outside the hint does
// NOT intersect (the culling path).
TEST(DrawingContext, NarrowHintExcludesDistantRect) {
    RectDip hint{0, 0, 100, 50};  // only the top-left corner
    DrawingContext dc{nullptr, nullptr, 1.0f, nullptr, &hint};
    RectDip distant{500, 400, 100, 100};
    EXPECT_FALSE(distant.intersects(dc.ClipHint()));
}
