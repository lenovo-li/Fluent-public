// ThemeManager.cpp — value-preserving token defaults + snapshot builder.
//
// The color defaults are the legacy MakeLight()/MakeDark() palettes copied
// verbatim (roadmap §11 mapping): windowBg->windowBackground, panelBg->cardFill,
// layerFill init = cardFill, controlFill->controlFillDefault, text->textPrimary,
// textDim->textSecondary, border->controlStrokeDefault, accent* same names,
// onAccent same, focusStroke init = accent. So a control migrated to a token
// draws the exact same pixels it did reading Palette. Spacing/typography/motion
// defaults are the constants pulled from the controls' anonymous namespaces
// (kCorner 4, kFontSize 14, kLabelGap 10, kTintTau/kFadeTau 0.05, sizes 12/14/
// 20/28, control height 32).

#include "ThemeManager.h"
#include <cmath>

namespace fluent {

namespace {
// 0xRRGGBB -> D2D1_COLOR_F at the given alpha (matches legacy Theme.cpp Rgb).
constexpr D2D1_COLOR_F Rgb(unsigned hex, float a = 1.0f) {
    return D2D1_COLOR_F{
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8) & 0xFF) / 255.0f,
        (hex & 0xFF) / 255.0f,
        a,
    };
}

ColorTokens LightColors() {
    ColorTokens c{};
    c.windowBackground = Rgb(0xF3F3F3);
    c.cardFill = Rgb(0xFFFFFF, 0.70f);   // semi-transparent card over Mica
    c.layerFill = c.cardFill;            // init = cardFill (roadmap §11 mapping)
    c.controlFillDefault = Rgb(0xFFFFFF, 0.70f);
    c.controlFillHover = Rgb(0xF9F9F9, 0.85f);
    c.controlFillPressed = Rgb(0xF0F0F0, 0.60f);
    c.controlStrokeDefault = Rgb(0xD0D0D0, 0.80f);
    c.textPrimary = Rgb(0x1A1A1A);
    c.textSecondary = Rgb(0x888888);
    c.accent = Rgb(0x0067C0);
    c.accentHover = Rgb(0x1975C5);
    c.accentPressed = Rgb(0x3183CB);
    c.onAccent = Rgb(0xFFFFFF);
    c.focusStroke = c.accent;            // init = accent (today's focus ring color)

    // Severity fills are tinted washes rather than saturated blocks: an InfoBar is a
    // background behind body text, so it has to keep textPrimary legible. The stroke of
    // each pair is the saturated version, used for the border and the glyph.
    c.severityInfoFill      = Rgb(0xF0F6FC, 0.90f);
    c.severityInfoStroke    = Rgb(0x0F6CBD);
    c.severitySuccessFill   = Rgb(0xF1FAF1, 0.90f);
    c.severitySuccessStroke = Rgb(0x0E700E);
    c.severityWarningFill   = Rgb(0xFFF9F0, 0.90f);
    c.severityWarningStroke = Rgb(0x9D5D00);
    c.severityErrorFill     = Rgb(0xFDF3F4, 0.90f);
    c.severityErrorStroke   = Rgb(0xC42B1C);

    // CJK convention: red = up, green = down. See the comment on these fields.
    c.dataPositive = Rgb(0xD13438);
    c.dataNegative = Rgb(0x0E7A0B);
    c.dataNeutral  = Rgb(0x8A8886);
    c.gridLine        = Rgb(0xE1E1E1, 0.85f);
    c.rowFillAlternate = Rgb(0x000000, 0.025f);   // barely-there zebra
    return c;
}

ColorTokens DarkColors() {
    ColorTokens c{};
    c.windowBackground = Rgb(0x202020);
    c.cardFill = Rgb(0x2B2B2B, 0.65f);
    c.layerFill = c.cardFill;
    c.controlFillDefault = Rgb(0xFFFFFF, 0.06f);
    c.controlFillHover = Rgb(0xFFFFFF, 0.08f);
    c.controlFillPressed = Rgb(0xFFFFFF, 0.03f);
    c.controlStrokeDefault = Rgb(0x3A3A3A, 0.80f);
    c.textPrimary = Rgb(0xF0F0F0);
    c.textSecondary = Rgb(0x9A9A9A);
    c.accent = Rgb(0x4CC2FF);
    c.accentHover = Rgb(0x47B1E8);
    c.accentPressed = Rgb(0x42A1D2);
    c.onAccent = Rgb(0x000000);
    c.focusStroke = c.accent;

    // Dark severity: the fill is a low-alpha wash of the hue over the dark surface
    // (a light tint would glare), and the stroke is BRIGHTENED rather than reused from
    // the light palette — 0x0E700E on 0x202020 is nearly invisible, which is exactly the
    // failure mode these tokens exist to prevent.
    c.severityInfoFill      = Rgb(0x4CC2FF, 0.10f);
    c.severityInfoStroke    = Rgb(0x6CB8F6);
    c.severitySuccessFill   = Rgb(0x6CCB6C, 0.10f);
    c.severitySuccessStroke = Rgb(0x6CCB6C);
    c.severityWarningFill   = Rgb(0xFCE100, 0.10f);
    c.severityWarningStroke = Rgb(0xFCE100);
    c.severityErrorFill     = Rgb(0xFF99A4, 0.10f);
    c.severityErrorStroke   = Rgb(0xFF99A4);

    c.dataPositive = Rgb(0xFF6B6B);
    c.dataNegative = Rgb(0x5DD55D);
    c.dataNeutral  = Rgb(0x9A9A9A);
    c.gridLine        = Rgb(0xFFFFFF, 0.10f);
    c.rowFillAlternate = Rgb(0xFFFFFF, 0.03f);
    return c;
}

SpacingTokens DefaultSpacing() {
    SpacingTokens s{};
    s.controlHeightSmall = 24.0f;
    s.controlHeightNormal = 32.0f;   // ComboBox / Button default row height
    s.controlHeightLarge = 40.0f;
    s.spacingXSmall = 4.0f;
    s.spacingSmall = 8.0f;
    s.spacingMedium = 10.0f;         // CheckBox/RadioButton kLabelGap
    s.spacingLarge = 12.0f;          // MenuBar kItemPadX
    s.cornerRadiusSmall = 4.0f;      // kCorner / kBoxCorner / kCornerRadius
    s.cornerRadiusNormal = 8.0f;
    s.borderWidth = 1.0f;
    return s;
}

TypographyTokens DefaultTypography() {
    TypographyTokens t{};
    t.fontFamily = L"Segoe UI Variable Text";  // informational; DWrite picks live
    t.captionSize = 12.0f;
    t.bodySize = 14.0f;
    t.subtitleSize = 20.0f;
    t.titleSize = 28.0f;
    return t;
}

MotionTokens DefaultMotion() {
    MotionTokens m{};
    m.fastMs = 90;
    m.normalMs = 150;
    m.slowMs = 300;
    m.tintTau = 0.05f;   // Button kTintTau
    m.fadeTau = 0.05f;   // CheckBox kFadeTau
    m.bezierX1 = 0.0f; m.bezierY1 = 0.0f; m.bezierX2 = 1.0f; m.bezierY2 = 1.0f;
    return m;
}
} // namespace

namespace {

// Layer the user overrides over the defaults. Sentinels: NaN floats are
// unset (same idiom as FrameworkElement::kAuto), optional colors are unset
// when empty, and an empty fontFamily is unset. Motion int durations use -1
// as the unset sentinel (NaN is float-only).
//
// Accent override ordering is LOAD-BEARING: `accent` is applied first (which
// spills into accentHover/Pressed/focusStroke), then the explicit hover/pressed/
// focusStroke are applied — so an explicit accentHover WINS over what `accent`
// would have propagated. That is what makes a real three-state accent expressible:
// set all three and the states differ. Without this ordering, an accentHover
// override would be silently ignored whenever `accent` is also set.
void ApplyOverrides(ThemeSnapshot& s, const ThemeInputs& in) {
    const SpacingOverrides& o = in.spacing;
    if (!std::isnan(o.controlHeightSmall)) s.spacing.controlHeightSmall = o.controlHeightSmall;
    if (!std::isnan(o.controlHeightNormal)) s.spacing.controlHeightNormal = o.controlHeightNormal;
    if (!std::isnan(o.controlHeightLarge)) s.spacing.controlHeightLarge = o.controlHeightLarge;
    if (!std::isnan(o.spacingXSmall)) s.spacing.spacingXSmall = o.spacingXSmall;
    if (!std::isnan(o.spacingSmall)) s.spacing.spacingSmall = o.spacingSmall;
    if (!std::isnan(o.spacingMedium)) s.spacing.spacingMedium = o.spacingMedium;
    if (!std::isnan(o.spacingLarge)) s.spacing.spacingLarge = o.spacingLarge;
    if (!std::isnan(o.cornerRadiusSmall)) s.spacing.cornerRadiusSmall = o.cornerRadiusSmall;
    if (!std::isnan(o.cornerRadiusNormal)) s.spacing.cornerRadiusNormal = o.cornerRadiusNormal;
    if (!std::isnan(o.borderWidth)) s.spacing.borderWidth = o.borderWidth;

    const TypographyOverrides& t = in.typography;
    if (!t.fontFamily.empty()) s.typography.fontFamily = t.fontFamily;
    if (!std::isnan(t.captionSize)) s.typography.captionSize = t.captionSize;
    if (!std::isnan(t.bodySize)) s.typography.bodySize = t.bodySize;
    if (!std::isnan(t.subtitleSize)) s.typography.subtitleSize = t.subtitleSize;
    if (!std::isnan(t.titleSize)) s.typography.titleSize = t.titleSize;

    const ColorOverrides& c = in.colors;
    // accent → accentHover/Pressed/focusStroke first (the "spill")
    if (c.accent) {
        s.colors.accent = *c.accent;
        s.colors.accentHover = *c.accent;
        s.colors.accentPressed = *c.accent;
        s.colors.focusStroke = *c.accent;
    }
    // explicit hover/pressed/focusStroke AFTER, so they win
    if (c.accentHover) s.colors.accentHover = *c.accentHover;
    if (c.accentPressed) s.colors.accentPressed = *c.accentPressed;
    if (c.focusStroke) s.colors.focusStroke = *c.focusStroke;

    if (c.onAccent) s.colors.onAccent = *c.onAccent;
    if (c.cardFill) s.colors.cardFill = *c.cardFill;
    if (c.layerFill) s.colors.layerFill = *c.layerFill;
    if (c.controlFillDefault) s.colors.controlFillDefault = *c.controlFillDefault;
    if (c.controlFillHover) s.colors.controlFillHover = *c.controlFillHover;
    if (c.controlFillPressed) s.colors.controlFillPressed = *c.controlFillPressed;
    if (c.textPrimary) s.colors.textPrimary = *c.textPrimary;
    if (c.textSecondary) s.colors.textSecondary = *c.textSecondary;
    if (c.windowBackground) s.colors.windowBackground = *c.windowBackground;
    if (c.controlStrokeDefault) s.colors.controlStrokeDefault = *c.controlStrokeDefault;
    if (c.dataPositive) s.colors.dataPositive = *c.dataPositive;
    if (c.dataNegative) s.colors.dataNegative = *c.dataNegative;
    if (c.dataNeutral) s.colors.dataNeutral = *c.dataNeutral;
    if (c.gridLine) s.colors.gridLine = *c.gridLine;
    if (c.rowFillAlternate) s.colors.rowFillAlternate = *c.rowFillAlternate;

    const MotionOverrides& m = in.motion;
    if (m.fastMs >= 0) s.motion.fastMs = m.fastMs;
    if (m.normalMs >= 0) s.motion.normalMs = m.normalMs;
    if (m.slowMs >= 0) s.motion.slowMs = m.slowMs;
    if (!std::isnan(m.tintTau)) s.motion.tintTau = m.tintTau;
    if (!std::isnan(m.fadeTau)) s.motion.fadeTau = m.fadeTau;
    if (!std::isnan(m.bezierX1)) s.motion.bezierX1 = m.bezierX1;
    if (!std::isnan(m.bezierY1)) s.motion.bezierY1 = m.bezierY1;
    if (!std::isnan(m.bezierX2)) s.motion.bezierX2 = m.bezierX2;
    if (!std::isnan(m.bezierY2)) s.motion.bezierY2 = m.bezierY2;
}

} // namespace

bool SystemUsesDarkMode() {
    // HKCU\...\Themes\Personalize\AppsUseLightTheme: 0 => dark.
    DWORD value = 1, size = sizeof(value);
    LSTATUS st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (st != ERROR_SUCCESS) return false;
    return value == 0;
}

ThemeInputs ThemeInputsFromSnapshot(const ThemeSnapshot& snap) {
    ThemeInputs in{};
    in.dark = snap.dark;
    in.highContrast = snap.highContrast;

    // Every color goes into the override layer. useCustomAccent stays false on
    // purpose: that flag's spill (accent -> hover/pressed/focusStroke) would
    // flatten the three accent states, and here all three are already known
    // exactly. The explicit-after-accent ordering in ApplyOverrides then keeps
    // them distinct.
    in.colors.accent = snap.colors.accent;
    in.colors.accentHover = snap.colors.accentHover;
    in.colors.accentPressed = snap.colors.accentPressed;
    in.colors.onAccent = snap.colors.onAccent;
    in.colors.focusStroke = snap.colors.focusStroke;
    in.colors.cardFill = snap.colors.cardFill;
    in.colors.layerFill = snap.colors.layerFill;
    in.colors.controlFillDefault = snap.colors.controlFillDefault;
    in.colors.controlFillHover = snap.colors.controlFillHover;
    in.colors.controlFillPressed = snap.colors.controlFillPressed;
    in.colors.textPrimary = snap.colors.textPrimary;
    in.colors.textSecondary = snap.colors.textSecondary;
    in.colors.windowBackground = snap.colors.windowBackground;
    in.colors.controlStrokeDefault = snap.colors.controlStrokeDefault;
    in.colors.dataPositive = snap.colors.dataPositive;
    in.colors.dataNegative = snap.colors.dataNegative;
    in.colors.dataNeutral = snap.colors.dataNeutral;
    in.colors.gridLine = snap.colors.gridLine;
    in.colors.rowFillAlternate = snap.colors.rowFillAlternate;

    in.spacing.controlHeightSmall = snap.spacing.controlHeightSmall;
    in.spacing.controlHeightNormal = snap.spacing.controlHeightNormal;
    in.spacing.controlHeightLarge = snap.spacing.controlHeightLarge;
    in.spacing.spacingXSmall = snap.spacing.spacingXSmall;
    in.spacing.spacingSmall = snap.spacing.spacingSmall;
    in.spacing.spacingMedium = snap.spacing.spacingMedium;
    in.spacing.spacingLarge = snap.spacing.spacingLarge;
    in.spacing.cornerRadiusSmall = snap.spacing.cornerRadiusSmall;
    in.spacing.cornerRadiusNormal = snap.spacing.cornerRadiusNormal;
    in.spacing.borderWidth = snap.spacing.borderWidth;

    in.typography.fontFamily = snap.typography.fontFamily;
    in.typography.captionSize = snap.typography.captionSize;
    in.typography.bodySize = snap.typography.bodySize;
    in.typography.subtitleSize = snap.typography.subtitleSize;
    in.typography.titleSize = snap.typography.titleSize;

    in.motion.fastMs = snap.motion.fastMs;
    in.motion.normalMs = snap.motion.normalMs;
    in.motion.slowMs = snap.motion.slowMs;
    in.motion.tintTau = snap.motion.tintTau;
    in.motion.fadeTau = snap.motion.fadeTau;
    in.motion.bezierX1 = snap.motion.bezierX1;
    in.motion.bezierY1 = snap.motion.bezierY1;
    in.motion.bezierX2 = snap.motion.bezierX2;
    in.motion.bezierY2 = snap.motion.bezierY2;

    return in;
}

ThemeSnapshot BuildSnapshot(const ThemeInputs& in, uint32_t generation) {
    ThemeSnapshot s{};
    s.colors = in.dark ? DarkColors() : LightColors();
    // A custom (OS-resolved) accent overrides the palette accent; the derived
    // hover/pressed follow the accent for now (host may refine later). onAccent
    // stays palette-driven (contrast is mode-, not accent-, specific).
    if (in.useCustomAccent) {
        s.colors.accent = in.accent;
        s.colors.accentHover = in.accent;
        s.colors.accentPressed = in.accent;
        s.colors.focusStroke = in.accent;
    }
    s.spacing = DefaultSpacing();
    s.typography = DefaultTypography();
    s.motion = DefaultMotion();
    s.dark = in.dark;
    s.highContrast = in.highContrast;
    s.generation = generation;
    ApplyOverrides(s, in);
    return s;
}

} // namespace fluent
