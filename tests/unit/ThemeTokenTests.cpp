// ThemeTokenTests.cpp — unit tests for the Fluent token system (WP-05 Stage 0,
// roadmap §11). Pure logic: no HWND, no D2D device, no DWrite — BuildSnapshot is
// a pure function of ThemeInputs, and NearlyEqual is a plain float compare.
//
// Coverage:
//   * NearlyEqual: equal within eps, differs past eps, per-channel;
//   * the light snapshot reproduces the legacy MakeLight() palette values
//     (guards against an accidental color change during the token migration);
//   * the dark snapshot reproduces MakeDark() and carries dark = true;
//   * a custom OS accent overrides the accent family but not onAccent;
//   * ThemeManager bumps `generation` on each rebuild and keeps its snapshot
//     pointer stable across a theme change (in-place overwrite);
//   * spacing / typography / motion defaults match today's scattered constants.

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
} // namespace

TEST(ThemeToken, NearlyEqualBasics) {
    EXPECT_TRUE(NearlyEqual(Rgb(0x0067C0), Rgb(0x0067C0)));
    EXPECT_TRUE(NearlyEqual(D2D1_COLOR_F{0.5f, 0.5f, 0.5f, 1.0f},
                            D2D1_COLOR_F{0.50001f, 0.5f, 0.5f, 1.0f}));
    EXPECT_FALSE(NearlyEqual(Rgb(0x0067C0), Rgb(0x0067C1)));
    // alpha difference is caught
    EXPECT_FALSE(NearlyEqual(Rgb(0xFFFFFF, 0.70f), Rgb(0xFFFFFF, 0.85f)));
}

TEST(ThemeToken, LightSnapshotPreservesLegacyPalette) {
    ThemeSnapshot s = BuildSnapshot(ThemeInputs{/*dark*/ false}, 1);
    const ColorTokens& c = s.colors;
    EXPECT_FALSE(s.dark);
    EXPECT_TRUE(NearlyEqual(c.windowBackground, Rgb(0xF3F3F3)));
    EXPECT_TRUE(NearlyEqual(c.cardFill, Rgb(0xFFFFFF, 0.70f)));
    EXPECT_TRUE(NearlyEqual(c.layerFill, c.cardFill));
    EXPECT_TRUE(NearlyEqual(c.controlFillDefault, Rgb(0xFFFFFF, 0.70f)));
    EXPECT_TRUE(NearlyEqual(c.controlFillHover, Rgb(0xF9F9F9, 0.85f)));
    EXPECT_TRUE(NearlyEqual(c.controlFillPressed, Rgb(0xF0F0F0, 0.60f)));
    EXPECT_TRUE(NearlyEqual(c.controlStrokeDefault, Rgb(0xD0D0D0, 0.80f)));
    EXPECT_TRUE(NearlyEqual(c.textPrimary, Rgb(0x1A1A1A)));
    EXPECT_TRUE(NearlyEqual(c.textSecondary, Rgb(0x888888)));
    EXPECT_TRUE(NearlyEqual(c.accent, Rgb(0x0067C0)));
    EXPECT_TRUE(NearlyEqual(c.accentHover, Rgb(0x1975C5)));
    EXPECT_TRUE(NearlyEqual(c.accentPressed, Rgb(0x3183CB)));
    EXPECT_TRUE(NearlyEqual(c.onAccent, Rgb(0xFFFFFF)));
    EXPECT_TRUE(NearlyEqual(c.focusStroke, c.accent));
}

TEST(ThemeToken, DarkSnapshotPreservesLegacyPalette) {
    ThemeSnapshot s = BuildSnapshot(ThemeInputs{/*dark*/ true}, 1);
    const ColorTokens& c = s.colors;
    EXPECT_TRUE(s.dark);
    EXPECT_TRUE(NearlyEqual(c.windowBackground, Rgb(0x202020)));
    EXPECT_TRUE(NearlyEqual(c.cardFill, Rgb(0x2B2B2B, 0.65f)));
    EXPECT_TRUE(NearlyEqual(c.controlFillDefault, Rgb(0xFFFFFF, 0.06f)));
    EXPECT_TRUE(NearlyEqual(c.controlFillHover, Rgb(0xFFFFFF, 0.08f)));
    EXPECT_TRUE(NearlyEqual(c.controlFillPressed, Rgb(0xFFFFFF, 0.03f)));
    EXPECT_TRUE(NearlyEqual(c.controlStrokeDefault, Rgb(0x3A3A3A, 0.80f)));
    EXPECT_TRUE(NearlyEqual(c.textPrimary, Rgb(0xF0F0F0)));
    EXPECT_TRUE(NearlyEqual(c.textSecondary, Rgb(0x9A9A9A)));
    EXPECT_TRUE(NearlyEqual(c.accent, Rgb(0x4CC2FF)));
    EXPECT_TRUE(NearlyEqual(c.onAccent, Rgb(0x000000)));
}

TEST(ThemeToken, CustomAccentOverridesAccentFamilyOnly) {
    ThemeInputs in{};
    in.dark = false;
    in.useCustomAccent = true;
    in.accent = Rgb(0x00AA55);
    ThemeSnapshot s = BuildSnapshot(in, 1);
    EXPECT_TRUE(NearlyEqual(s.colors.accent, Rgb(0x00AA55)));
    EXPECT_TRUE(NearlyEqual(s.colors.accentHover, Rgb(0x00AA55)));
    EXPECT_TRUE(NearlyEqual(s.colors.focusStroke, Rgb(0x00AA55)));
    // onAccent stays mode-driven, not accent-driven
    EXPECT_TRUE(NearlyEqual(s.colors.onAccent, Rgb(0xFFFFFF)));
}

TEST(ThemeToken, SpacingTypographyMotionDefaults) {
    ThemeSnapshot s = BuildSnapshot(ThemeInputs{}, 1);
    EXPECT_NEAR(s.spacing.controlHeightNormal, 32.0f, 0.001f);
    EXPECT_NEAR(s.spacing.spacingMedium, 10.0f, 0.001f);  // kLabelGap
    EXPECT_NEAR(s.spacing.cornerRadiusSmall, 4.0f, 0.001f);  // kCorner
    EXPECT_NEAR(s.spacing.borderWidth, 1.0f, 0.001f);
    EXPECT_NEAR(s.typography.captionSize, 12.0f, 0.001f);
    EXPECT_NEAR(s.typography.bodySize, 14.0f, 0.001f);
    EXPECT_NEAR(s.typography.subtitleSize, 20.0f, 0.001f);
    EXPECT_NEAR(s.typography.titleSize, 28.0f, 0.001f);
    EXPECT_NEAR(s.motion.tintTau, 0.05f, 0.0001f);  // Button kTintTau
    EXPECT_NEAR(s.motion.fadeTau, 0.05f, 0.0001f);  // CheckBox kFadeTau
}

TEST(ThemeToken, ManagerBumpsGenerationAndKeepsStablePointer) {
    ThemeManager mgr;
    const ThemeSnapshot* p0 = &mgr.Snapshot();
    uint32_t g0 = mgr.Generation();
    EXPECT_FALSE(mgr.Snapshot().dark);

    mgr.SetDark(true);
    const ThemeSnapshot* p1 = &mgr.Snapshot();
    EXPECT_TRUE(mgr.Snapshot().dark);
    EXPECT_TRUE(mgr.Generation() > g0);
    // In-place overwrite: the snapshot pointer parked on UIContext.theme must not
    // move across a theme change.
    EXPECT_EQ(p0, p1);

    mgr.SetDark(false);
    EXPECT_EQ(p0, &mgr.Snapshot());
    EXPECT_FALSE(mgr.Snapshot().dark);
    EXPECT_TRUE(mgr.Generation() > g0 + 1);
}

TEST(ThemeToken, GenerationStampFlowsIntoSnapshot) {
    ThemeSnapshot s = BuildSnapshot(ThemeInputs{}, 42);
    EXPECT_EQ(s.generation, 42u);
}
