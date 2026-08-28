// DisabledAndInteractionStateTests.cpp — do the shared state colours actually respond
// to Hover / Pressed / Disabled?
//
// WHY THIS FILE EXISTS. Four defects were spotted from screenshots and all four are in
// the pure colour-resolution functions rather than in any control's Render:
//
//   1. An explicit AccentColor / Background killed hover and press feedback. Button's
//      ButtonFillColor returned the SAME colour for Normal, Hover and Pressed once the
//      caller set one ("自定义背景" / "自定义 Accent" in the demo felt dead to the
//      pointer). The theme derives three shades from its own accent; an override got
//      one flat colour, so the control stopped answering the pointer entirely.
//
//   2. ToggleMarkColor took no VisualState at all, so a disabled CheckBox's tick,
//      RadioButton's dot and ToggleSwitch's knob painted at FULL strength on a faded
//      fill — a crisp white tick on a washed-out box, which reads as a rendering
//      glitch rather than as a disabled control.
//
//   3. The toggle labels resolved to textPrimary unconditionally, so a disabled
//      control's TEXT was as black as a live one's while its box was faded.
//
// These assertions are phrased as relationships ("pressed differs from normal",
// "disabled alpha is lower") plus the specific 0.4 convention, so an implementation
// that merely returns *something* different cannot satisfy them.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/ToggleVisuals.h"
#include "../../FluentUI/styling/ThemeTokens.h"

#include <cmath>
#include <cstdio>
#include <optional>

using namespace fluent;

namespace {

ColorTokens MakeTokens() {
    ColorTokens t{};
    t.accent        = D2D1::ColorF(0.00f, 0.40f, 0.75f, 1.0f);
    t.accentHover   = D2D1::ColorF(0.10f, 0.46f, 0.77f, 1.0f);
    t.accentPressed = D2D1::ColorF(0.19f, 0.51f, 0.80f, 1.0f);
    t.onAccent      = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    t.textPrimary   = D2D1::ColorF(0.10f, 0.10f, 0.10f, 1.0f);
    t.textSecondary = D2D1::ColorF(0.53f, 0.53f, 0.53f, 1.0f);
    t.controlFillDefault = D2D1::ColorF(0.98f, 0.98f, 0.98f, 0.80f);
    t.controlFillHover   = D2D1::ColorF(0.96f, 0.96f, 0.96f, 0.90f);
    t.controlFillPressed = D2D1::ColorF(0.94f, 0.94f, 0.94f, 0.60f);
    return t;
}

// Perceptual-ish distance; enough to say "these two are not the same colour".
float Dist(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b, da = a.a - b.a;
    return std::sqrt(dr * dr + dg * dg + db * db + da * da);
}

constexpr float kNoticeable = 0.02f;   // below this a user cannot see the change

}  // namespace

// --- 1. Explicit colours must still respond to the pointer --------------------

TEST(InteractionState, ExplicitAccentStillShiftsOnHoverAndPress) {
    ColorTokens c = MakeTokens();
    ButtonAppearance app{};
    app.accent = D2D1::ColorF(0.55f, 0.36f, 0.72f, 1.0f);   // the demo's purple

    const D2D1_COLOR_F normal  = ButtonFillColor(Button::Kind::Accent, VisualState::Normal,  app, c);
    const D2D1_COLOR_F hover   = ButtonFillColor(Button::Kind::Accent, VisualState::Hover,   app, c);
    const D2D1_COLOR_F pressed = ButtonFillColor(Button::Kind::Accent, VisualState::Pressed, app, c);

    std::printf("  explicit accent: normal->hover %.3f, normal->pressed %.3f\n",
                Dist(normal, hover), Dist(normal, pressed));

    EXPECT_TRUE(Dist(normal, hover) > kNoticeable);      // hover must be visible
    EXPECT_TRUE(Dist(normal, pressed) > kNoticeable);    // press must be visible
    EXPECT_TRUE(Dist(hover, pressed) > 0.0f);            // and they must differ
}

// The caller's exact colour must still be what they get AT REST. The whole reason the
// override existed was "I mean this colour" — deriving states must not move the base.
TEST(InteractionState, ExplicitAccentIsExactAtRest) {
    ColorTokens c = MakeTokens();
    ButtonAppearance app{};
    app.accent = D2D1::ColorF(0.55f, 0.36f, 0.72f, 1.0f);

    const D2D1_COLOR_F normal = ButtonFillColor(Button::Kind::Accent, VisualState::Normal, app, c);
    EXPECT_TRUE(Dist(normal, *app.accent) < 0.001f);
}

TEST(InteractionState, ExplicitBackgroundStillShiftsOnHoverAndPress) {
    ColorTokens c = MakeTokens();
    ButtonAppearance app{};
    app.background = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f);   // near-black

    const D2D1_COLOR_F normal  = ButtonFillColor(Button::Kind::Standard, VisualState::Normal,  app, c);
    const D2D1_COLOR_F hover   = ButtonFillColor(Button::Kind::Standard, VisualState::Hover,   app, c);
    const D2D1_COLOR_F pressed = ButtonFillColor(Button::Kind::Standard, VisualState::Pressed, app, c);

    std::printf("  explicit background: normal->hover %.3f, normal->pressed %.3f\n",
                Dist(normal, hover), Dist(normal, pressed));

    EXPECT_TRUE(Dist(normal, hover) > kNoticeable);
    EXPECT_TRUE(Dist(normal, pressed) > kNoticeable);
    EXPECT_TRUE(Dist(normal, *app.background) < 0.001f);   // exact at rest
}

// A DARK explicit colour must get LIGHTER on hover, and a light one DARKER. A fixed
// "add 8% white" rule would make a near-black button hover correctly and a near-white
// one not move at all (it is already clipped at 1.0).
TEST(InteractionState, HoverShiftDirectionDependsOnLuminance) {
    ColorTokens c = MakeTokens();

    auto lum = [](const D2D1_COLOR_F& x) { return 0.2126f * x.r + 0.7152f * x.g + 0.0722f * x.b; };

    ButtonAppearance dark{};
    dark.background = D2D1::ColorF(0.05f, 0.05f, 0.05f, 1.0f);
    const float dn = lum(ButtonFillColor(Button::Kind::Standard, VisualState::Normal, dark, c));
    const float dh = lum(ButtonFillColor(Button::Kind::Standard, VisualState::Hover,  dark, c));

    ButtonAppearance light{};
    light.background = D2D1::ColorF(0.97f, 0.97f, 0.97f, 1.0f);
    const float ln = lum(ButtonFillColor(Button::Kind::Standard, VisualState::Normal, light, c));
    const float lh = lum(ButtonFillColor(Button::Kind::Standard, VisualState::Hover,  light, c));

    std::printf("  dark %.3f->%.3f (lighter), light %.3f->%.3f (darker)\n", dn, dh, ln, lh);
    EXPECT_TRUE(dh > dn);    // near-black lifts
    EXPECT_TRUE(lh < ln);    // near-white sinks
}

// --- 2. The toggle MARK must fade when disabled -------------------------------

TEST(DisabledState, ToggleMarkFadesWhenDisabled) {
    ColorTokens c = MakeTokens();
    ToggleAppearance app{std::nullopt, std::nullopt, std::nullopt};

    const D2D1_COLOR_F on  = ToggleMarkColor(VisualState::Normal,   app, c);
    const D2D1_COLOR_F off = ToggleMarkColor(VisualState::Disabled, app, c);

    std::printf("  mark alpha: enabled %.2f -> disabled %.2f\n", on.a, off.a);
    EXPECT_TRUE(off.a < on.a);
    EXPECT_TRUE(std::fabs(off.a - 0.4f) < 0.01f);   // same 0.4 convention as the fill
}

// An explicit Foreground is the caller's exact mark colour, so like Button's rule it
// survives Disabled unfaded — but it must not be the reason a disabled control looks
// live, so the FILL still fades underneath it.
TEST(DisabledState, ExplicitMarkColourSurvivesDisabled) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F mark = D2D1::ColorF(1.0f, 0.85f, 0.0f, 1.0f);
    ToggleAppearance app{std::nullopt, std::nullopt, mark};

    const D2D1_COLOR_F out = ToggleMarkColor(VisualState::Disabled, app, c);
    EXPECT_TRUE(std::fabs(out.r - mark.r) < 0.01f);
    EXPECT_TRUE(std::fabs(out.g - mark.g) < 0.01f);
}

// --- 3. The toggle LABEL must dim when disabled -------------------------------

TEST(DisabledState, ToggleLabelUsesSecondaryTextWhenDisabled) {
    ColorTokens c = MakeTokens();
    ToggleAppearance app{std::nullopt, std::nullopt, std::nullopt};

    const D2D1_COLOR_F live = ToggleLabelColor(VisualState::Normal,   app, c);
    const D2D1_COLOR_F dead = ToggleLabelColor(VisualState::Disabled, app, c);

    std::printf("  label: live lum %.2f, disabled lum %.2f\n",
                0.2126f * live.r + 0.7152f * live.g + 0.0722f * live.b,
                0.2126f * dead.r + 0.7152f * dead.g + 0.0722f * dead.b);

    EXPECT_TRUE(Dist(live, dead) > kNoticeable);
    EXPECT_TRUE(Dist(dead, c.textSecondary) < 0.001f);   // Button's convention
    EXPECT_TRUE(Dist(live, c.textPrimary) < 0.001f);
}

// An explicit Foreground on a toggle means the MARK, not the label (setting both equal
// is what made a tick vanish). So the label must NOT silently adopt it: a caller who
// themed the tick purple did not ask for purple text.
TEST(DisabledState, ToggleLabelIgnoresTheMarkOverride) {
    ColorTokens c = MakeTokens();
    ToggleAppearance app{std::nullopt, std::nullopt, D2D1::ColorF(0.55f, 0.36f, 0.72f, 1.0f)};

    const D2D1_COLOR_F label = ToggleLabelColor(VisualState::Normal, app, c);
    EXPECT_TRUE(Dist(label, c.textPrimary) < 0.001f);
}
