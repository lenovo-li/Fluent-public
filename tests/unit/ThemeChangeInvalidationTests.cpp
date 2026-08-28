// ThemeChangeInvalidationTests.cpp — the WP-05 acceptance rule (roadmap §26
// rule 7): a THEME (color) change must invalidate Render only, never Measure —
// the desktop layout does not change when the palette flips light/dark, so
// forcing a whole-tree re-Measure would be wrong (and expensive). A TYPOGRAPHY
// change (font size / role) genuinely changes desired sizes, so it DOES set
// Measure. Pure logic: dirty flags are plain bits, no DWrite/D2D needed.
//
// Coverage:
//   * a color-only setter (TextBlock::SetDimmed) sets Render, not Measure;
//   * OnThemeChanged on a control that repaints (MenuBar) sets Render, not
//     Measure — this is what the host calls for every element on a theme flip;
//   * a typography-role change (TextBlock::SetTypographyRole) DOES set Measure;
//   * an explicit font-size change (SetFontSize) sets Measure.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/MenuBar.h"

using namespace fluent;

TEST(ThemeChangeInvalidation, ColorSetterIsRenderOnly) {
    // A fresh element (dirty_ starts None); a color-only setter adds Render only.
    TextBlock tb;
    tb.SetDimmed(true);          // color-only: Render, not Measure
    EXPECT_TRUE(Has(tb.Dirty(), DirtyFlags::Render));
    EXPECT_FALSE(Has(tb.Dirty(), DirtyFlags::Measure));
}

TEST(ThemeChangeInvalidation, MenuBarOnThemeChangedIsRenderOnly) {
    // MenuBar::OnThemeChanged() calls Invalidate() (Render). This is exactly what
    // NativeWindowHost::ApplyTheme dispatches to every element on a theme flip.
    MenuBar mb;
    mb.OnThemeChanged();
    EXPECT_TRUE(Has(mb.Dirty(), DirtyFlags::Render));
    EXPECT_FALSE(Has(mb.Dirty(), DirtyFlags::Measure));
    EXPECT_FALSE(Has(mb.Dirty(), DirtyFlags::Arrange));
}

TEST(ThemeChangeInvalidation, TypographyRoleChangeSetsMeasure) {
    TextBlock tb;
    tb.SetTypographyRole(TypographyRole::Title);  // size may change → remeasure
    EXPECT_TRUE(Has(tb.Dirty(), DirtyFlags::Measure));
}

TEST(ThemeChangeInvalidation, ExplicitFontSizeChangeSetsMeasure) {
    TextBlock tb;
    tb.SetFontSize(28.0f);
    EXPECT_TRUE(Has(tb.Dirty(), DirtyFlags::Measure));
}

TEST(ThemeChangeInvalidation, RedundantColorSetIsNoOp) {
    // SetProperty short-circuits an equal write, so re-setting the same dimmed
    // value schedules no frame at all (no dirty bit) — a theme flip that doesn't
    // actually change a value costs nothing.
    TextBlock tb;
    tb.SetDimmed(false);  // default is already false
    EXPECT_FALSE(Has(tb.Dirty(), DirtyFlags::Render));
    EXPECT_FALSE(Has(tb.Dirty(), DirtyFlags::Measure));
}
