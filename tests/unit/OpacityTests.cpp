// OpacityTests.cpp — the opacity interface added to close two real bugs:
//
//   1. On Windows 10 the WHOLE WINDOW was see-through. ResolveBackdrop correctly
//      reported useSolidFallback (build < 22000), but nothing consumed it: the
//      content surface cleared to transparent every frame and, with no Mica and no
//      redirection bitmap, the desktop showed through. ResolveBaseFill + the
//      window's BaseFill() now supply that missing opaque base.
//   2. On Windows 11 a popup MENU was see-through. cardFill is authored at alpha
//      0.70 FOR a Mica base, but a popup HWND has no system backdrop on any OS
//      version — so the raw token let the parent window through. FlattenOver turns
//      it into the solid color that translucency was designed to look like.
//
// Everything here is pure math / pure policy, so it is all headless-testable.

#include "../framework/Test.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/styling/ThemeManager.h"
#include "../../FluentUI/window/WindowAppearance.h"
#include "../../FluentUI/graphics/DrawingContext.h"

using namespace fluent;

namespace {
constexpr D2D1_COLOR_F kOpaqueWhite{1.0f, 1.0f, 1.0f, 1.0f};
constexpr D2D1_COLOR_F kHalfWhite{1.0f, 1.0f, 1.0f, 0.5f};
constexpr D2D1_COLOR_F kOpaqueBlack{0.0f, 0.0f, 0.0f, 1.0f};
}  // namespace

// --- WithAlphaScale ------------------------------------------------------

// Scaling alpha touches ONLY alpha: the RGB must survive untouched, because these
// are straight-alpha colors (D2D premultiplies internally for a premultiplied
// target). Pre-multiplying here would darken every faded control.
TEST(Opacity, WithAlphaScaleLeavesRgbAlone) {
    const D2D1_COLOR_F c{0.25f, 0.5f, 0.75f, 0.8f};
    const D2D1_COLOR_F out = WithAlphaScale(c, 0.5f);
    EXPECT_NEAR(out.r, 0.25f, 0.0001f);
    EXPECT_NEAR(out.g, 0.5f,  0.0001f);
    EXPECT_NEAR(out.b, 0.75f, 0.0001f);
    EXPECT_NEAR(out.a, 0.4f,  0.0001f);  // 0.8 * 0.5
}

// Opacity 1 is the identity — the default path must not perturb any pixel.
TEST(Opacity, WithAlphaScaleFullIsIdentity) {
    const D2D1_COLOR_F out = WithAlphaScale(kHalfWhite, 1.0f);
    EXPECT_TRUE(NearlyEqual(out, kHalfWhite));
}

// Out-of-range input is clamped rather than producing a negative or >1 alpha
// (D2D would render those unpredictably).
TEST(Opacity, WithAlphaScaleClampsRange) {
    EXPECT_NEAR(WithAlphaScale(kOpaqueWhite, -0.5f).a, 0.0f, 0.0001f);
    EXPECT_NEAR(WithAlphaScale(kOpaqueWhite,  3.0f).a, 1.0f, 0.0001f);
}

// --- FlattenOver ---------------------------------------------------------

// The bug-2 fix in one assertion: white @ 0.70 over #F3F3F3 must come out FULLY
// OPAQUE and lighter than the base — the solid color a Mica-backed card looks
// like. If the result kept alpha < 1 the menu would still be see-through.
TEST(Opacity, FlattenOverProducesOpaqueResult) {
    const D2D1_COLOR_F base{0.953f, 0.953f, 0.953f, 1.0f};  // #F3F3F3
    const D2D1_COLOR_F card{1.0f, 1.0f, 1.0f, 0.70f};       // cardFill (light)
    const D2D1_COLOR_F out = FlattenOver(card, base);
    EXPECT_NEAR(out.a, 1.0f, 0.0001f);
    // 1.0*0.7 + 0.953*0.3 = 0.9859
    EXPECT_NEAR(out.r, 0.9859f, 0.001f);
    EXPECT_TRUE(out.r > base.r);  // lighter than the base, as the card should be
}

// A fully opaque source ignores the base entirely.
TEST(Opacity, FlattenOverOpaqueSourceWins) {
    const D2D1_COLOR_F out = FlattenOver(kOpaqueBlack, kOpaqueWhite);
    EXPECT_NEAR(out.r, 0.0f, 0.0001f);
    EXPECT_NEAR(out.a, 1.0f, 0.0001f);
}

// A fully transparent source yields the base, still opaque.
TEST(Opacity, FlattenOverTransparentSourceYieldsBase) {
    const D2D1_COLOR_F clear{1.0f, 0.0f, 0.0f, 0.0f};
    const D2D1_COLOR_F out = FlattenOver(clear, kOpaqueBlack);
    EXPECT_NEAR(out.r, 0.0f, 0.0001f);
    EXPECT_NEAR(out.a, 1.0f, 0.0001f);
}

// The real theme tokens must actually flatten OPAQUE in both modes — this is the
// property the popup relies on, asserted against the shipped values rather than
// hand-written ones.
TEST(Opacity, ThemeCardFillFlattensOpaqueInBothModes) {
    for (bool dark : {false, true}) {
        ThemeInputs in;
        in.dark = dark;
        const ThemeSnapshot s = BuildSnapshot(in, 0);
        // The token is deliberately translucent (that is what caused the bug)...
        EXPECT_TRUE(s.colors.cardFill.a < 1.0f);
        // ...and flattening over the window background makes it opaque.
        const D2D1_COLOR_F flat =
            FlattenOver(s.colors.cardFill, s.colors.windowBackground);
        EXPECT_NEAR(flat.a, 1.0f, 0.0001f);
    }
}

// --- ResolveBaseFill: the §12.2 ladder's missing half --------------------

// Bug 1, directly: a pre-Win11 build resolves to a solid fallback, and the window
// must then paint an OPAQUE base. Before the fix nothing was painted at all.
TEST(Opacity, BaseFillIsOpaqueOnSolidFallback) {
    AppearanceEnv env;
    env.osBuild = 19045;  // Windows 10 22H2 — the machine that showed the bug
    const ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, env);
    EXPECT_TRUE(r.useSolidFallback);
    EXPECT_TRUE(r.reason == FallbackReason::OsTooOld);

    const D2D1_COLOR_F bg{0.953f, 0.953f, 0.953f, 1.0f};
    const BaseFillPlan p = ResolveBaseFill(r, bg, 1.0f);
    EXPECT_TRUE(p.fill);
    EXPECT_NEAR(p.color.a, 1.0f, 0.0001f);
    EXPECT_NEAR(p.color.r, bg.r, 0.0001f);
}

// With Mica actually active, painting a base would ERASE the material — so at full
// opacity the plan must be "draw nothing". This is what keeps Win11 looking right.
TEST(Opacity, BaseFillSkippedWhenMaterialActive) {
    AppearanceEnv env;
    env.osBuild = 22621;  // Win11 22H2
    const ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, env);
    EXPECT_FALSE(r.useSolidFallback);
    EXPECT_TRUE(r.effective == BackdropKind::Mica);

    const BaseFillPlan p = ResolveBaseFill(r, kOpaqueWhite, 1.0f);
    EXPECT_FALSE(p.fill);
}

// An app that explicitly dials the background down gets translucency even with a
// material present — the material is tinted rather than left alone.
TEST(Opacity, BaseFillTintsMaterialBelowFullOpacity) {
    AppearanceEnv env;
    env.osBuild = 22621;
    const ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, env);
    const BaseFillPlan p = ResolveBaseFill(r, kOpaqueWhite, 0.4f);
    EXPECT_TRUE(p.fill);
    EXPECT_NEAR(p.color.a, 0.4f, 0.0001f);
}

// On a material-less system the app can still ask for a translucent window; the
// requested alpha is honored instead of being forced opaque.
TEST(Opacity, BaseFillHonorsRequestedAlphaOnFallback) {
    AppearanceEnv env;
    env.osBuild = 19045;
    const ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, env);
    const BaseFillPlan p = ResolveBaseFill(r, kOpaqueWhite, 0.25f);
    EXPECT_TRUE(p.fill);
    EXPECT_NEAR(p.color.a, 0.25f, 0.0001f);
}

// Every degradation path in the ladder must produce an opaque base, not just the
// old-OS one: composition off, RDP, transparency off, high contrast, explicit
// None. Each of these leaves the window with no material to sit on.
TEST(Opacity, BaseFillOpaqueForEveryFallbackReason) {
    struct Case { const char* name; AppearanceEnv env; BackdropKind req; };
    AppearanceEnv compOff;      compOff.osBuild = 22621; compOff.dwmComposition = false;
    AppearanceEnv remote;       remote.osBuild = 22621;  remote.remoteSession = true;
    AppearanceEnv transpOff;    transpOff.osBuild = 22621; transpOff.transparency = false;
    AppearanceEnv highContrast; highContrast.osBuild = 22621; highContrast.highContrast = true;
    AppearanceEnv ok;           ok.osBuild = 22621;

    const Case cases[] = {
        {"compositionOff", compOff,      BackdropKind::Auto},
        {"remoteSession",  remote,       BackdropKind::Auto},
        {"transparencyOff",transpOff,    BackdropKind::Auto},
        {"highContrast",   highContrast, BackdropKind::Auto},
        {"requestedNone",  ok,           BackdropKind::None},
    };
    for (const Case& c : cases) {
        const ResolvedBackdrop r = ResolveBackdrop(c.req, c.env);
        EXPECT_TRUE(r.useSolidFallback);
        const BaseFillPlan p = ResolveBaseFill(r, kOpaqueWhite, 1.0f);
        EXPECT_TRUE(p.fill);
        EXPECT_NEAR(p.color.a, 1.0f, 0.0001f);
    }
}

// Opacity is clamped at the policy layer too, so a caller cannot smuggle an
// out-of-range alpha into a brush.
TEST(Opacity, BaseFillClampsOpacity) {
    AppearanceEnv env;
    env.osBuild = 19045;
    const ResolvedBackdrop r = ResolveBackdrop(BackdropKind::Auto, env);
    EXPECT_NEAR(ResolveBaseFill(r, kOpaqueWhite, -1.0f).color.a, 0.0f, 0.0001f);
    EXPECT_NEAR(ResolveBaseFill(r, kOpaqueWhite,  5.0f).color.a, 1.0f, 0.0001f);
}

// --- DrawingContext opacity ---------------------------------------------

// The default context is fully opaque and Faded is the identity — the guarantee
// that adding opacity changed no existing pixel.
TEST(Opacity, DrawingContextDefaultsToOpaque) {
    DrawingContext dc{nullptr, nullptr, 1.0f};
    EXPECT_NEAR(dc.Opacity(), 1.0f, 0.0001f);
    EXPECT_TRUE(NearlyEqual(dc.Faded(kHalfWhite), kHalfWhite));
}

// Faded multiplies the color's own alpha by the context opacity, so a token that
// is already translucent fades proportionally rather than jumping to the context
// value.
TEST(Opacity, DrawingContextFadedMultipliesAlpha) {
    DrawingContext dc{nullptr, nullptr, 1.0f, nullptr, nullptr, 0.5f};
    const D2D1_COLOR_F out = dc.Faded(kHalfWhite);
    EXPECT_NEAR(out.a, 0.25f, 0.0001f);  // 0.5 * 0.5
    EXPECT_NEAR(out.r, 1.0f,  0.0001f);  // RGB untouched
}

// WithOpacity composes multiplicatively (nested opacities behave like WPF/WinUI)
// and never mutates the original context.
TEST(Opacity, DrawingContextWithOpacityComposes) {
    DrawingContext parent{nullptr, nullptr, 1.0f, nullptr, nullptr, 0.5f};
    const DrawingContext child = parent.WithOpacity(0.5f);
    EXPECT_NEAR(child.Opacity(), 0.25f, 0.0001f);
    EXPECT_NEAR(parent.Opacity(), 0.5f, 0.0001f);  // unchanged
}

// WithOpacity carries the rest of the context across: losing the ClipHint here
// would silently disable Panel's dirty-region culling for any faded subtree.
TEST(Opacity, DrawingContextWithOpacityPreservesClipHintAndDpi) {
    RectDip hint{10.0f, 20.0f, 30.0f, 40.0f};
    DrawingContext dc{nullptr, nullptr, 2.0f, nullptr, &hint};
    const DrawingContext faded = dc.WithOpacity(0.5f);
    EXPECT_NEAR(faded.DpiScale(), 2.0f, 0.0001f);
    EXPECT_NEAR(faded.ClipHint().x, 10.0f, 0.001f);
    EXPECT_NEAR(faded.ClipHint().w, 30.0f, 0.001f);
}

// The constructor clamps, so a bad value cannot reach a brush.
TEST(Opacity, DrawingContextClampsOpacity) {
    DrawingContext lo{nullptr, nullptr, 1.0f, nullptr, nullptr, -2.0f};
    DrawingContext hi{nullptr, nullptr, 1.0f, nullptr, nullptr, 7.0f};
    EXPECT_NEAR(lo.Opacity(), 0.0f, 0.0001f);
    EXPECT_NEAR(hi.Opacity(), 1.0f, 0.0001f);
}
