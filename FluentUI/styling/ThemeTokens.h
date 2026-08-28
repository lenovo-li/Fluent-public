// ThemeTokens.h — the Fluent design-system token set (roadmap §11).
//
// This replaces the old flat `Palette` struct (12 loose colors + a dark bool)
// and the constants scattered across every control's anonymous namespace
// (kCorner, kFontSize, kLabelGap, kBoxCorner, hard-coded 12/13/14/28 sizes).
// Design values now live in ONE place as a set of typed token groups, and a
// control reads them through the ThemeSnapshot handed to it via UIContext —
// never by hard-coding a color or a size.
//
// A ThemeSnapshot is an IMMUTABLE VALUE plus a `generation` stamp (kept equal to
// the host ResourceCache epoch, so a bumped generation is exactly the signal a
// theme-derived cache entry — a text layout, a themed geometry — must rebuild).
// The host owns one stable snapshot (ThemeManager::current_); when the theme
// changes it overwrites that value in place and bumps the cache epoch, so the
// pointer parked on every UIContext stays valid and only its pointee changes
// (the same idiom as rebuilding d2d_ in place on device recovery). Controls
// must not copy a snapshot pointer past their attach period.
#pragma once

#include "../fl_common.h"
#include <d2d1.h>
#include <string>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace fluent {

// Free helper: D2D1_COLOR_F has no operator== (a known WP-02 pain point), so
// token comparisons and tests use this instead of a raw field compare.
inline bool NearlyEqual(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b,
                        float eps = 0.0001f) {
    return std::fabs(a.r - b.r) < eps && std::fabs(a.g - b.g) < eps &&
           std::fabs(a.b - b.b) < eps && std::fabs(a.a - b.a) < eps;
}

// --- Opacity helpers (pure color math, unit-tested) ----------------------
//
// Several surface tokens carry an alpha BELOW 1 on purpose: they are designed to
// sit over a window material (Mica), which supplies the opaque base. Wherever no
// material exists — a pre-Win11 window, a popup HWND with no backdrop — that
// missing base has to be supplied explicitly or the "translucency" degrades into
// see-through-to-the-desktop. These two helpers are how a caller does that.

// Scale a color's alpha by `opacity` [0..1]. Straight (non-premultiplied) alpha:
// D2D takes straight-alpha colors and premultiplies internally for a
// premultiplied target, so callers never premultiply by hand.
inline D2D1_COLOR_F WithAlphaScale(const D2D1_COLOR_F& c, float opacity) {
    const float o = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    return D2D1_COLOR_F{c.r, c.g, c.b, c.a * o};
}

// Composite straight-alpha `src` OVER OPAQUE `dst` and return the flattened,
// FULLY OPAQUE result. Use this to turn a material-designed translucent token
// into the solid color it was meant to *look* like when no material is present:
// e.g. cardFill (white @ 0.70) over windowBackground (#F3F3F3) => ~#FAFAFA,
// which is what the eye sees on Win11 anyway. `dst`'s alpha is ignored (it is
// the opaque base by definition); the result always has alpha 1.
inline D2D1_COLOR_F FlattenOver(const D2D1_COLOR_F& src, const D2D1_COLOR_F& dst) {
    const float a = (src.a < 0.0f) ? 0.0f : (src.a > 1.0f ? 1.0f : src.a);
    return D2D1_COLOR_F{
        src.r * a + dst.r * (1.0f - a),
        src.g * a + dst.g * (1.0f - a),
        src.b * a + dst.b * (1.0f - a),
        1.0f,
    };
}

// Semantic color roles (roadmap §11.1). Each is a resolved D2D color for the
// current mode — a control asks for the role it needs (textPrimary, accent…)
// rather than picking a light/dark literal. controlFillHover/Pressed are kept
// as explicitly named tokens (NOT renamed Secondary/Tertiary) so the light->
// token migration is value-preserving and unambiguous.
struct ColorTokens {
    D2D1_COLOR_F windowBackground;    // base behind Mica (rarely visible)
    D2D1_COLOR_F layerFill;           // large surface layer (init = cardFill)
    D2D1_COLOR_F cardFill;            // card / panel fill
    D2D1_COLOR_F controlFillDefault;  // neutral control rest fill
    D2D1_COLOR_F controlFillHover;
    D2D1_COLOR_F controlFillPressed;
    D2D1_COLOR_F controlStrokeDefault;  // thin separators / control borders
    D2D1_COLOR_F textPrimary;
    D2D1_COLOR_F textSecondary;
    D2D1_COLOR_F accent;              // selection / primary button fill
    D2D1_COLOR_F accentHover;
    D2D1_COLOR_F accentPressed;
    D2D1_COLOR_F onAccent;            // text/glyph on an accent fill
    D2D1_COLOR_F focusStroke;         // focus-ring color (init = accent)

    // --- Severity roles (InfoBar, validation text, status) --------------------
    //
    // Four severities, each a fill + a stroke/glyph colour. Added because there was no
    // way to express "this message is a warning" without a control hard-coding a literal
    // colour, which breaks the light/dark contract the rest of this struct exists to
    // enforce: a control that picks 0xFFF4CE for a warning background is invisible in
    // dark mode.
    //
    // `informational` is deliberately NOT an alias for `accent`. Accent is the user's
    // personalisation colour (it can be changed to anything, including red), so reusing
    // it for "info" would make an informational message indistinguishable from an error
    // on some machines.
    D2D1_COLOR_F severityInfoFill;
    D2D1_COLOR_F severityInfoStroke;
    D2D1_COLOR_F severitySuccessFill;
    D2D1_COLOR_F severitySuccessStroke;
    D2D1_COLOR_F severityWarningFill;
    D2D1_COLOR_F severityWarningStroke;
    D2D1_COLOR_F severityErrorFill;
    D2D1_COLOR_F severityErrorStroke;

    // --- Data / quantitative roles (DataGrid, charts, metrics) ---------------
    //
    // A financial UI needs "up" and "down" as SEMANTIC roles, not literals, because the
    // convention is regional: red means a price rose in mainland China / Japan / Korea
    // and that it fell in Europe and North America. A control that hard-codes green-up
    // is wrong for half the world's users, and the choice belongs to the app rather than
    // to the control. Defaults here follow the CJK convention (red up, green down)
    // because that is what this codebase's first consumer needs; an app flips them
    // through ThemeInputs without touching a control.
    D2D1_COLOR_F dataPositive;        // "up" series / positive delta
    D2D1_COLOR_F dataNegative;        // "down" series / negative delta
    D2D1_COLOR_F dataNeutral;         // flat / unchanged
    D2D1_COLOR_F gridLine;           // chart gridlines + DataGrid rules (subtle)
    D2D1_COLOR_F rowFillAlternate;   // zebra striping for dense tables
};

// Sizing/spacing tokens in DIPs (roadmap §11.3). All device-independent; the
// host multiplies by DPI at draw time. Replaces per-control kCorner/gap/height.
struct SpacingTokens {
    float controlHeightSmall;
    float controlHeightNormal;
    float controlHeightLarge;
    float spacingXSmall;
    float spacingSmall;
    float spacingMedium;
    float spacingLarge;
    float cornerRadiusSmall;
    float cornerRadiusNormal;
    float borderWidth;
};

// Type ramp (roadmap §11). Sizes in DIP; DirectWrite fonts are DIP-based so a
// DPI change does not require rebuilding the font object. The default values
// match today's hard-coded sizes exactly (caption 12 / body 14 / etc.).
struct TypographyTokens {
    std::wstring fontFamily;
    float captionSize;   // 12
    float bodySize;      // 14
    float subtitleSize;  // 20
    float titleSize;     // 28
};

// --- User override layers (Win11 polish phase A0) ---------------------------
//
// Partial mirrors of the token groups above: every field starts "unset" and
// BuildSnapshot layers the set fields over the Win11 defaults. Unset
// sentinels follow the framework's existing conventions: floats use NaN
// (same idiom as FrameworkElement::kAuto / IsAuto), colors use
// std::optional, and fontFamily uses an empty string.
//
// Overrides survive a light/dark flip: they layer over the NEW mode's
// defaults (that's what "user-defined theme" intuitively means). Font
// weight is deliberately NOT here — weight is control semantics, not a
// theme surface (users change it per-control via TextBlock::SetWeight).
struct SpacingOverrides {
    float controlHeightSmall = std::numeric_limits<float>::quiet_NaN();
    float controlHeightNormal = std::numeric_limits<float>::quiet_NaN();
    float controlHeightLarge = std::numeric_limits<float>::quiet_NaN();
    float spacingXSmall = std::numeric_limits<float>::quiet_NaN();
    float spacingSmall = std::numeric_limits<float>::quiet_NaN();
    float spacingMedium = std::numeric_limits<float>::quiet_NaN();
    float spacingLarge = std::numeric_limits<float>::quiet_NaN();
    float cornerRadiusSmall = std::numeric_limits<float>::quiet_NaN();
    float cornerRadiusNormal = std::numeric_limits<float>::quiet_NaN();
    float borderWidth = std::numeric_limits<float>::quiet_NaN();
};

struct TypographyOverrides {
    std::wstring fontFamily;  // empty = not set
    float captionSize = std::numeric_limits<float>::quiet_NaN();
    float bodySize = std::numeric_limits<float>::quiet_NaN();
    float subtitleSize = std::numeric_limits<float>::quiet_NaN();
    float titleSize = std::numeric_limits<float>::quiet_NaN();
};

// Only the most-commonly-tweaked roles are open.
//
// Setting `accent` alone keeps the historical behavior: accentHover, accentPressed
// and focusStroke all follow it. That is a deliberate convenience — the common case
// is "make everything my brand color" and deriving three more shades by hand is
// busywork — and it is exactly what the useCustomAccent (OS accent) route does, so
// both paths agree.
//
// accentHover / accentPressed / focusStroke are ALSO independently settable, and an
// explicit value wins over the one `accent` would have propagated. That is what makes
// a real three-state accent expressible: set all three and the states differ. The
// ordering in ApplyOverrides is what enforces "explicit beats derived" — accent is
// applied first, the specific roles after.
struct ColorOverrides {
    std::optional<D2D1_COLOR_F> accent;         // also sets accentHover/Pressed/focusStroke
    std::optional<D2D1_COLOR_F> accentHover;    // explicit; wins over accent's spill
    std::optional<D2D1_COLOR_F> accentPressed;  // explicit; wins over accent's spill
    std::optional<D2D1_COLOR_F> onAccent;       // text/glyph color on an accent fill
    std::optional<D2D1_COLOR_F> cardFill;
    std::optional<D2D1_COLOR_F> layerFill;
    std::optional<D2D1_COLOR_F> controlFillDefault;
    std::optional<D2D1_COLOR_F> controlFillHover;
    std::optional<D2D1_COLOR_F> controlFillPressed;
    std::optional<D2D1_COLOR_F> textPrimary;
    std::optional<D2D1_COLOR_F> textSecondary;
    std::optional<D2D1_COLOR_F> windowBackground;
    std::optional<D2D1_COLOR_F> controlStrokeDefault;
    std::optional<D2D1_COLOR_F> focusStroke;

    // Data roles are open because the up/down colour convention is REGIONAL, not a
    // matter of taste: an app shipping to Europe or North America has to swap
    // dataPositive and dataNegative, and it must be able to do that without editing a
    // control or maintaining a fork of the palette. The severity fills/strokes are left
    // closed for now (eight more optionals for no known caller); open them when one
    // exists rather than on speculation.
    std::optional<D2D1_COLOR_F> dataPositive;
    std::optional<D2D1_COLOR_F> dataNegative;
    std::optional<D2D1_COLOR_F> dataNeutral;
    std::optional<D2D1_COLOR_F> gridLine;
    std::optional<D2D1_COLOR_F> rowFillAlternate;
};

// Motion constants (roadmap §11). Today's animations are all exponential easing
// on a time constant (Button tint, CheckBox fade); only those time constants are
// real. Duration/bezier fields are reserved with NO consumer yet — do not build
// a motion system around them.
struct MotionTokens {
    int fastMs;
    int normalMs;
    int slowMs;
    float tintTau;   // exponential tint time constant (Button)
    float fadeTau;   // exponential fade time constant (CheckBox)
    // Reserved, no consumer yet:
    float bezierX1, bezierY1, bezierX2, bezierY2;
};

// Motion overrides. Same sentinel convention: NaN floats are unset. Duration fields
// (fastMs/normalMs/slowMs) use -1 as the unset sentinel (NaN is float-only), matching
// the convention that an int less than zero is invalid time.
struct MotionOverrides {
    int fastMs = -1;
    int normalMs = -1;
    int slowMs = -1;
    float tintTau = std::numeric_limits<float>::quiet_NaN();
    float fadeTau = std::numeric_limits<float>::quiet_NaN();
    float bezierX1 = std::numeric_limits<float>::quiet_NaN();
    float bezierY1 = std::numeric_limits<float>::quiet_NaN();
    float bezierX2 = std::numeric_limits<float>::quiet_NaN();
    float bezierY2 = std::numeric_limits<float>::quiet_NaN();
};

// The immutable per-frame theme value handed to controls. `generation` tracks
// the ResourceCache epoch: a theme change bumps it so a theme-derived cache
// entry keyed on it rebuilds. `dark` is carried through (Button reads it to pick
// a light/dark tint; TextBlock reads it to set scrollbar alpha). `highContrast`
// gates the high-contrast rendering path.
struct ThemeSnapshot {
    ColorTokens colors;
    SpacingTokens spacing;
    TypographyTokens typography;
    MotionTokens motion;
    bool dark = false;
    bool highContrast = false;
    uint32_t generation = 0;
};

} // namespace fluent
