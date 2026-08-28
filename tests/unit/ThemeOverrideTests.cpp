// ThemeOverrideTests.cpp — unit tests for the A0 user override layer
// (win11-polish-and-roadmap.md phase A0). Pure logic: BuildSnapshot layers the
// override fields over the Win11 defaults; nothing here needs a window.
//
// Coverage:
//   * default inputs reproduce the current snapshot exactly (the "unset" case
//     is a no-op — this guards "user sets nothing => Win11 default theme");
//   * one spacing override lands while every other field stays default;
//   * overrides survive a light/dark flip, layered over the NEW mode's
//     defaults (the color groups switch, the override stays);
//   * a color accent override mirrors the useCustomAccent behavior exactly
//     (hover/pressed/focusStroke follow the same value — a known, kept
//     simplification).

#include "../framework/Test.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/styling/ThemeManager.h"

using namespace fluent;

namespace {
D2D1_COLOR_F Rgb(unsigned hex, float a = 1.0f) {
    return D2D1_COLOR_F{
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8) & 0xFF) / 255.0f,
        (hex & 0xFF) / 255.0f, a};
}

bool SameColors(const ColorTokens& a, const ColorTokens& b) {
    return NearlyEqual(a.windowBackground, b.windowBackground) &&
           NearlyEqual(a.cardFill, b.cardFill) &&
           NearlyEqual(a.layerFill, b.layerFill) &&
           NearlyEqual(a.controlFillDefault, b.controlFillDefault) &&
           NearlyEqual(a.controlFillHover, b.controlFillHover) &&
           NearlyEqual(a.controlFillPressed, b.controlFillPressed) &&
           NearlyEqual(a.controlStrokeDefault, b.controlStrokeDefault) &&
           NearlyEqual(a.textPrimary, b.textPrimary) &&
           NearlyEqual(a.textSecondary, b.textSecondary) &&
           NearlyEqual(a.accent, b.accent) &&
           NearlyEqual(a.accentHover, b.accentHover) &&
           NearlyEqual(a.accentPressed, b.accentPressed) &&
           NearlyEqual(a.onAccent, b.onAccent) &&
           NearlyEqual(a.focusStroke, b.focusStroke);
}

bool SameSpacing(const SpacingTokens& a, const SpacingTokens& b) {
    return a.controlHeightSmall == b.controlHeightSmall &&
           a.controlHeightNormal == b.controlHeightNormal &&
           a.controlHeightLarge == b.controlHeightLarge &&
           a.spacingXSmall == b.spacingXSmall &&
           a.spacingSmall == b.spacingSmall &&
           a.spacingMedium == b.spacingMedium &&
           a.spacingLarge == b.spacingLarge &&
           a.cornerRadiusSmall == b.cornerRadiusSmall &&
           a.cornerRadiusNormal == b.cornerRadiusNormal &&
           a.borderWidth == b.borderWidth;
}

bool SameTypography(const TypographyTokens& a, const TypographyTokens& b) {
    return a.fontFamily == b.fontFamily &&
           a.captionSize == b.captionSize &&
           a.bodySize == b.bodySize &&
           a.subtitleSize == b.subtitleSize &&
           a.titleSize == b.titleSize;
}
} // namespace

TEST(ThemeOverride, DefaultInputsReproduceCurrentSnapshot) {
    // An all-unset override layer must be an exact no-op: "user sets nothing
    // => Win11 default theme". Compare against a fresh default build so this
    // test can't drift from the defaults.
    ThemeInputs def{};
    def.dark = true;
    ThemeSnapshot a = BuildSnapshot(def, 1);

    ThemeInputs withOverrides{};
    withOverrides.dark = true;
    // spacing/typography/colors members are all default-constructed = unset
    ThemeSnapshot b = BuildSnapshot(withOverrides, 1);

    EXPECT_TRUE(SameColors(a.colors, b.colors));
    EXPECT_TRUE(SameSpacing(a.spacing, b.spacing));
    EXPECT_TRUE(SameTypography(a.typography, b.typography));
    EXPECT_TRUE(a.dark == b.dark);
}

TEST(ThemeOverride, CornerRadiusOverrideLeavesOtherFieldsDefault) {
    ThemeInputs in{};
    in.spacing.cornerRadiusSmall = 2.0f;
    ThemeSnapshot s = BuildSnapshot(in, 1);

    EXPECT_TRUE(s.spacing.cornerRadiusSmall == 2.0f);
    // Everything else is still the Win11 default.
    EXPECT_TRUE(s.spacing.cornerRadiusNormal == 8.0f);
    EXPECT_TRUE(s.spacing.controlHeightNormal == 32.0f);
    EXPECT_TRUE(s.spacing.borderWidth == 1.0f);
    EXPECT_TRUE(s.typography.bodySize == 14.0f);
    EXPECT_TRUE(NearlyEqual(s.colors.accent, Rgb(0x0067C0)));
}

TEST(ThemeOverride, OverridesSurviveLightDarkFlip) {
    // Overrides layer over the NEW mode's defaults: the color groups switch
    // to dark values, but the user's corner radius stays.
    ThemeInputs in{};
    in.dark = false;
    in.spacing.cornerRadiusSmall = 12.0f;
    in.colors.textPrimary = Rgb(0x112233);
    ThemeSnapshot light = BuildSnapshot(in, 1);
    EXPECT_TRUE(light.spacing.cornerRadiusSmall == 12.0f);
    EXPECT_TRUE(NearlyEqual(light.colors.textPrimary, Rgb(0x112233)));
    EXPECT_TRUE(NearlyEqual(light.colors.windowBackground, Rgb(0xF3F3F3)));

    in.dark = true;
    ThemeSnapshot dark = BuildSnapshot(in, 2);
    EXPECT_TRUE(dark.spacing.cornerRadiusSmall == 12.0f);         // override kept
    EXPECT_TRUE(NearlyEqual(dark.colors.textPrimary, Rgb(0x112233)));  // override kept
    EXPECT_TRUE(NearlyEqual(dark.colors.windowBackground, Rgb(0x202020)));  // new default
    EXPECT_TRUE(NearlyEqual(dark.colors.accent, Rgb(0x4CC2FF)));    // new default
}

TEST(ThemeOverride, AccentOverrideMirrorsCustomAccentPath) {
    // Deliberate parity with useCustomAccent: hover/pressed/focusStroke all
    // follow the accent value. Do not "improve" this — both routes must
    // behave identically (see plan A0 note).
    ThemeInputs viaOverrides{};
    viaOverrides.colors.accent = Rgb(0x00AA55);
    ThemeSnapshot a = BuildSnapshot(viaOverrides, 1);

    ThemeInputs viaCustom{};
    viaCustom.useCustomAccent = true;
    viaCustom.accent = Rgb(0x00AA55);
    ThemeSnapshot b = BuildSnapshot(viaCustom, 1);

    EXPECT_TRUE(SameColors(a.colors, b.colors));
    EXPECT_TRUE(NearlyEqual(a.colors.accent, Rgb(0x00AA55)));
    EXPECT_TRUE(NearlyEqual(a.colors.accentHover, Rgb(0x00AA55)));
    EXPECT_TRUE(NearlyEqual(a.colors.accentPressed, Rgb(0x00AA55)));
    EXPECT_TRUE(NearlyEqual(a.colors.focusStroke, Rgb(0x00AA55)));
    // onAccent stays palette-driven in both routes.
    EXPECT_TRUE(NearlyEqual(a.colors.onAccent, Rgb(0xFFFFFF)));
}

TEST(ThemeOverride, AccentThreeStateIndependentOverrides) {
    // Setting accent + explicit hover/pressed should produce three distinct colors.
    // The explicit hover/pressed should WIN over what accent would have propagated.
    ThemeInputs in{};
    in.colors.accent = Rgb(0xFF0000);          // red
    in.colors.accentHover = Rgb(0x00FF00);     // green
    in.colors.accentPressed = Rgb(0x0000FF);   // blue
    ThemeSnapshot s = BuildSnapshot(in, 1);

    EXPECT_TRUE(NearlyEqual(s.colors.accent, Rgb(0xFF0000)));
    EXPECT_TRUE(NearlyEqual(s.colors.accentHover, Rgb(0x00FF00)));
    EXPECT_TRUE(NearlyEqual(s.colors.accentPressed, Rgb(0x0000FF)));
    // focusStroke follows accent (no explicit override for it)
    EXPECT_TRUE(NearlyEqual(s.colors.focusStroke, Rgb(0xFF0000)));
}

TEST(ThemeOverride, AccentAloneStillSpills) {
    // Setting ONLY accent (no explicit hover/pressed) should spill into all three.
    ThemeInputs in{};
    in.colors.accent = Rgb(0xABCDEF);
    ThemeSnapshot s = BuildSnapshot(in, 1);

    EXPECT_TRUE(NearlyEqual(s.colors.accent, Rgb(0xABCDEF)));
    EXPECT_TRUE(NearlyEqual(s.colors.accentHover, Rgb(0xABCDEF)));
    EXPECT_TRUE(NearlyEqual(s.colors.accentPressed, Rgb(0xABCDEF)));
    EXPECT_TRUE(NearlyEqual(s.colors.focusStroke, Rgb(0xABCDEF)));
}

TEST(ThemeOverride, MotionOverrides) {
    ThemeInputs in{};
    in.motion.fastMs = 60;
    in.motion.tintTau = 0.08f;
    ThemeSnapshot s = BuildSnapshot(in, 1);

    EXPECT_EQ(s.motion.fastMs, 60);
    EXPECT_EQ(s.motion.tintTau, 0.08f);
    EXPECT_EQ(s.motion.normalMs, 150);  // default, untouched
    EXPECT_EQ(s.motion.fadeTau, 0.05f); // default, untouched
}

TEST(ThemeOverride, TypographyFontFamilyOverride) {
    ThemeInputs in{};
    in.typography.fontFamily = L"Microsoft YaHei UI";
    in.typography.bodySize = 15.0f;
    ThemeSnapshot s = BuildSnapshot(in, 1);
    EXPECT_TRUE(s.typography.fontFamily == L"Microsoft YaHei UI");
    EXPECT_TRUE(s.typography.bodySize == 15.0f);
    EXPECT_TRUE(s.typography.captionSize == 12.0f);  // untouched
}

// --- ThemeInputsFromSnapshot: the "load a whole theme at once" round trip ----
//
// The property that matters: snapshot -> inputs -> snapshot is the identity on
// every token field. If it is not, NativeWindowHost::SetTheme silently drops part
// of the theme the app handed it.

TEST(ThemeFromSnapshot, RoundTripPreservesEveryColor) {
    // A snapshot that differs from BOTH built-in palettes in every color, so a
    // field that failed to round-trip cannot accidentally match a default.
    ThemeSnapshot src = BuildSnapshot(ThemeInputs{}, 7);
    src.colors.windowBackground = Rgb(0x102030);
    src.colors.layerFill        = Rgb(0x112233, 0.55f);
    src.colors.cardFill         = Rgb(0x223344, 0.44f);
    src.colors.controlFillDefault = Rgb(0x334455, 0.33f);
    src.colors.controlFillHover   = Rgb(0x445566, 0.22f);
    src.colors.controlFillPressed = Rgb(0x556677, 0.11f);
    src.colors.controlStrokeDefault = Rgb(0x667788, 0.66f);
    src.colors.textPrimary   = Rgb(0x778899);
    src.colors.textSecondary = Rgb(0x8899AA);
    src.colors.accent        = Rgb(0x99AABB);
    src.colors.accentHover   = Rgb(0xAABBCC);
    src.colors.accentPressed = Rgb(0xBBCCDD);
    src.colors.onAccent      = Rgb(0xCCDDEE);
    src.colors.focusStroke   = Rgb(0xDDEEFF);

    ThemeSnapshot out = BuildSnapshot(ThemeInputsFromSnapshot(src), 99);
    EXPECT_TRUE(SameColors(out.colors, src.colors));
    // The three accent states must stay DISTINCT — this is the case the old
    // accent-spill-only override layer could not express.
    EXPECT_TRUE(NearlyEqual(out.colors.accent, Rgb(0x99AABB)));
    EXPECT_TRUE(NearlyEqual(out.colors.accentHover, Rgb(0xAABBCC)));
    EXPECT_TRUE(NearlyEqual(out.colors.accentPressed, Rgb(0xBBCCDD)));
}

TEST(ThemeFromSnapshot, RoundTripPreservesSpacingTypographyMotion) {
    ThemeSnapshot src = BuildSnapshot(ThemeInputs{}, 1);
    src.spacing.controlHeightSmall = 21.0f;
    src.spacing.controlHeightNormal = 29.0f;
    src.spacing.controlHeightLarge = 37.0f;
    src.spacing.spacingXSmall = 3.0f;
    src.spacing.spacingSmall = 7.0f;
    src.spacing.spacingMedium = 9.0f;
    src.spacing.spacingLarge = 13.0f;
    src.spacing.cornerRadiusSmall = 5.0f;
    src.spacing.cornerRadiusNormal = 11.0f;
    src.spacing.borderWidth = 2.0f;
    src.typography.fontFamily = L"Cascadia Mono";
    src.typography.captionSize = 11.0f;
    src.typography.bodySize = 15.0f;
    src.typography.subtitleSize = 19.0f;
    src.typography.titleSize = 27.0f;
    src.motion.fastMs = 55;
    src.motion.normalMs = 111;
    src.motion.slowMs = 222;
    src.motion.tintTau = 0.07f;
    src.motion.fadeTau = 0.09f;
    src.motion.bezierX1 = 0.25f;
    src.motion.bezierY1 = 0.1f;
    src.motion.bezierX2 = 0.25f;
    src.motion.bezierY2 = 1.0f;

    ThemeSnapshot out = BuildSnapshot(ThemeInputsFromSnapshot(src), 2);

    EXPECT_EQ(out.spacing.controlHeightSmall, 21.0f);
    EXPECT_EQ(out.spacing.controlHeightNormal, 29.0f);
    EXPECT_EQ(out.spacing.controlHeightLarge, 37.0f);
    EXPECT_EQ(out.spacing.spacingXSmall, 3.0f);
    EXPECT_EQ(out.spacing.spacingSmall, 7.0f);
    EXPECT_EQ(out.spacing.spacingMedium, 9.0f);
    EXPECT_EQ(out.spacing.spacingLarge, 13.0f);
    EXPECT_EQ(out.spacing.cornerRadiusSmall, 5.0f);
    EXPECT_EQ(out.spacing.cornerRadiusNormal, 11.0f);
    EXPECT_EQ(out.spacing.borderWidth, 2.0f);

    EXPECT_TRUE(out.typography.fontFamily == L"Cascadia Mono");
    EXPECT_EQ(out.typography.captionSize, 11.0f);
    EXPECT_EQ(out.typography.bodySize, 15.0f);
    EXPECT_EQ(out.typography.subtitleSize, 19.0f);
    EXPECT_EQ(out.typography.titleSize, 27.0f);

    EXPECT_EQ(out.motion.fastMs, 55);
    EXPECT_EQ(out.motion.normalMs, 111);
    EXPECT_EQ(out.motion.slowMs, 222);
    EXPECT_EQ(out.motion.tintTau, 0.07f);
    EXPECT_EQ(out.motion.fadeTau, 0.09f);
    EXPECT_EQ(out.motion.bezierX1, 0.25f);
    EXPECT_EQ(out.motion.bezierY1, 0.1f);
    EXPECT_EQ(out.motion.bezierX2, 0.25f);
    EXPECT_EQ(out.motion.bezierY2, 1.0f);
}

TEST(ThemeFromSnapshot, RoundTripPreservesDarkAndHighContrastFlags) {
    ThemeInputs darkIn{};
    darkIn.dark = true;
    darkIn.highContrast = true;
    ThemeSnapshot src = BuildSnapshot(darkIn, 1);

    ThemeSnapshot out = BuildSnapshot(ThemeInputsFromSnapshot(src), 2);
    EXPECT_TRUE(out.dark);
    EXPECT_TRUE(out.highContrast);
    // And the dark palette's colors came along, not the light ones.
    EXPECT_TRUE(SameColors(out.colors, src.colors));
}

TEST(ThemeFromSnapshot, GenerationIsTheCallersNotTheSnapshots) {
    // The stale generation on an offline-built snapshot must not leak through:
    // the manager/caller stamps it, because that is what cache invalidation keys on.
    ThemeSnapshot src = BuildSnapshot(ThemeInputs{}, 12345);
    ThemeSnapshot out = BuildSnapshot(ThemeInputsFromSnapshot(src), 6);
    EXPECT_EQ(out.generation, 6u);
}
