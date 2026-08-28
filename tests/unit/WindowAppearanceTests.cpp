// WindowAppearanceTests.cpp — unit tests for the backdrop decision core (WP-05
// Stage 4, roadmap §12.2). ResolveBackdrop is a PURE function of the environment,
// so the whole degradation ladder is a truth table — no HWND, no DWM, no OS reads
// (those live in the Win32 half, WindowAppearance::ReadEnvironment/Apply*, which
// needs a real desktop and is covered by manual testing).
//
// The ladder (highest-priority suppressor first):
//   high contrast > explicit None > remote session > composition off >
//   OS too old > transparency off > (else) honor the material.

#include "../framework/Test.h"
#include "../../FluentUI/window/WindowAppearance.h"

using namespace fluent;

TEST(WindowAppearance, CornerRadiusMapsToNativePresets) {
    EXPECT_EQ(CornerPreferenceForRadius(0.0f), CornerPreference::DoNotRound);
    EXPECT_EQ(CornerPreferenceForRadius(-4.0f), CornerPreference::DoNotRound);
    EXPECT_EQ(CornerPreferenceForRadius(4.0f), CornerPreference::RoundSmall);
    EXPECT_EQ(CornerPreferenceForRadius(8.0f), CornerPreference::RoundSmall);
    EXPECT_EQ(CornerPreferenceForRadius(12.0f), CornerPreference::Round);
}

namespace {
// A modern, capable environment: Win11 22H2, composition on, transparency on,
// no high contrast, not remote. The baseline every test tweaks one field of.
AppearanceEnv Modern() {
    AppearanceEnv e;
    e.osBuild = 22621;
    e.dwmComposition = true;
    e.transparency = true;
    e.highContrast = false;
    e.remoteSession = false;
    return e;
}
} // namespace

TEST(WindowAppearance, AutoResolvesToMicaOnModernWin11) {
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, Modern());
    EXPECT_TRUE(r.effective == BackdropKind::Mica);
    EXPECT_FALSE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::None);
    EXPECT_FALSE(r.suppressDarkTitleBar);
}

TEST(WindowAppearance, ExplicitMaterialHonoredOnModern) {
    EXPECT_TRUE(ResolveBackdrop(BackdropKind::MicaAlt, Modern()).effective ==
                BackdropKind::MicaAlt);
    EXPECT_TRUE(ResolveBackdrop(BackdropKind::Acrylic, Modern()).effective ==
                BackdropKind::Acrylic);
}

TEST(WindowAppearance, ExplicitNoneIsSolidNoDowngrade) {
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::None, Modern());
    EXPECT_TRUE(r.effective == BackdropKind::None);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::RequestedNone);
}

TEST(WindowAppearance, HighContrastSuppressesMaterialAndDarkTitleBar) {
    AppearanceEnv e = Modern();
    e.highContrast = true;
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, e);
    EXPECT_TRUE(r.effective == BackdropKind::None);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::HighContrast);
    EXPECT_TRUE(r.suppressDarkTitleBar);
}

TEST(WindowAppearance, HighContrastWinsOverEverything) {
    // Even with an old OS + composition off, high contrast is reported as the
    // reason (it is checked first, and it is the one that also gates the title bar).
    AppearanceEnv e = Modern();
    e.highContrast = true;
    e.osBuild = 19041;       // Windows 10
    e.dwmComposition = false;
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Mica, e);
    EXPECT_TRUE(r.reason == FallbackReason::HighContrast);
}

TEST(WindowAppearance, RemoteSessionFallsBackToSolid) {
    AppearanceEnv e = Modern();
    e.remoteSession = true;
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, e);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::RemoteSession);
    EXPECT_FALSE(r.suppressDarkTitleBar);  // dark caption still allowed on RDP
}

TEST(WindowAppearance, CompositionOffFallsBackToSolid) {
    AppearanceEnv e = Modern();
    e.dwmComposition = false;
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Mica, e);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::CompositionOff);
}

TEST(WindowAppearance, OldOsFallsBackToSolid) {
    AppearanceEnv e = Modern();
    e.osBuild = 19045;  // Windows 10 22H2 — no DWMWA_SYSTEMBACKDROP_TYPE
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, e);
    EXPECT_TRUE(r.effective == BackdropKind::None);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::OsTooOld);
}

TEST(WindowAppearance, MinMicaBuildIsExactBoundary) {
    AppearanceEnv e = Modern();
    e.osBuild = kMinMicaBuild;  // exactly 22000 → supported
    EXPECT_FALSE(ResolveBackdrop(BackdropKind::Auto, e).useSolidFallback);
    e.osBuild = kMinMicaBuild - 1;  // 21999 → too old
    EXPECT_TRUE(ResolveBackdrop(BackdropKind::Auto, e).useSolidFallback);
}

TEST(WindowAppearance, TransparencyOffFallsBackToSolid) {
    AppearanceEnv e = Modern();
    e.transparency = false;
    ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, e);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::TransparencyOff);
}
