// SeparatorTests.cpp — headless tests for Separator orientation, thickness, and
// default stretch behavior.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Separator.h"

using namespace fluent;

// --- Orientation affects measured size ----------------------------------------

TEST(Separator, HorizontalSeparatorIsThinInHeight) {
    Separator sep;
    sep.SetOrientation(Separator::Orientation::Horizontal);
    sep.Measure(500.0f, 200.0f);

    // Horizontal: stretches width to avail, thin in height (default 1 DIP).
    EXPECT_NEAR(sep.Desired().w, 500.0f, 0.01f);
    EXPECT_NEAR(sep.Desired().h, 1.0f, 0.01f);
}

TEST(Separator, VerticalSeparatorIsThinInWidth) {
    Separator sep;
    sep.SetOrientation(Separator::Orientation::Vertical);
    sep.Measure(500.0f, 200.0f);

    // Vertical: thin in width, stretches height to avail.
    EXPECT_NEAR(sep.Desired().w, 1.0f, 0.01f);
    EXPECT_NEAR(sep.Desired().h, 200.0f, 0.01f);
}

// --- Thickness is configurable ---------------------------------------------

TEST(Separator, ThicknessAffectsThinDimension) {
    Separator sep;
    sep.SetThickness(3.0f);

    // Horizontal: thickness shows up in height.
    sep.SetOrientation(Separator::Orientation::Horizontal);
    sep.Measure(500.0f, 200.0f);
    EXPECT_NEAR(sep.Desired().h, 3.0f, 0.01f);

    // Vertical: thickness shows up in width.
    sep.SetOrientation(Separator::Orientation::Vertical);
    sep.Measure(500.0f, 200.0f);
    EXPECT_NEAR(sep.Desired().w, 3.0f, 0.01f);
}

TEST(Separator, ThicknessChangeInvalidatesMeasure) {
    Separator sep;
    sep.Measure(500.0f, 200.0f);
    sep.ClearDirtySubtree();
    EXPECT_FALSE(sep.NeedsRemeasure());

    sep.SetThickness(2.0f);
    EXPECT_TRUE(sep.NeedsRemeasure());
}

// --- Explicit size overrides the default ----------------------------------

TEST(Separator, ExplicitWidthOverridesStretch) {
    Separator sep;
    sep.SetOrientation(Separator::Orientation::Horizontal);
    sep.SetWidth(100.0f);
    sep.Measure(500.0f, 200.0f);

    EXPECT_NEAR(sep.Desired().w, 100.0f, 0.01f);
}

TEST(Separator, ExplicitHeightOverridesThickness) {
    Separator sep;
    sep.SetOrientation(Separator::Orientation::Horizontal);
    sep.SetHeight(5.0f);
    sep.Measure(500.0f, 200.0f);

    EXPECT_NEAR(sep.Desired().h, 5.0f, 0.01f);
}

// --- Orientation change is observable --------------------------------------

TEST(Separator, OrientationChangeInvalidatesMeasure) {
    Separator sep;
    sep.SetOrientation(Separator::Orientation::Horizontal);
    sep.Measure(500.0f, 200.0f);
    sep.ClearDirtySubtree();
    EXPECT_FALSE(sep.NeedsRemeasure());

    sep.SetOrientation(Separator::Orientation::Vertical);
    EXPECT_TRUE(sep.NeedsRemeasure());
}

TEST(Separator, OrientationChangeSwapsDimensions) {
    Separator sep;
    sep.SetThickness(2.0f);

    sep.SetOrientation(Separator::Orientation::Horizontal);
    sep.Measure(500.0f, 200.0f);
    const SizeDip horiz = sep.Desired();
    EXPECT_NEAR(horiz.w, 500.0f, 0.01f);
    EXPECT_NEAR(horiz.h, 2.0f, 0.01f);

    sep.SetOrientation(Separator::Orientation::Vertical);
    sep.Measure(500.0f, 200.0f);
    const SizeDip vert = sep.Desired();
    EXPECT_NEAR(vert.w, 2.0f, 0.01f);
    EXPECT_NEAR(vert.h, 200.0f, 0.01f);
}

// --- Default alignment is Stretch -------------------------------------------

TEST(Separator, DefaultsToStretchAlignment) {
    Separator sep;
    // The default is Stretch in both dimensions, so a parent that respects that
    // will stretch the separator to fill. A subsequent explicit SetHAlign can
    // override it. We verify the initial state is Stretch.
    EXPECT_EQ(sep.GetHAlign(), HAlign::Stretch);
    EXPECT_EQ(sep.GetVAlign(), VAlign::Stretch);
}
