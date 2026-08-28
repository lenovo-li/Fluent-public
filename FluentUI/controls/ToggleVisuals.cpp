// ToggleVisuals.cpp

#include "ToggleVisuals.h"

namespace fluent {

namespace {
// Disabled treatment for the whole toggle family: keep the hue, drop the alpha.
//
// 0.4 is not a new number -- Button.cpp already fades its disabled fills by exactly this,
// so a disabled CheckBox now looks as disabled as a disabled Button next to it. Fading
// rather than substituting a grey token is what lets an explicitly themed control stay
// recognisable while inert (a red "delete" toggle greys out as faded red, not as generic
// grey), matching the precedence rule Button documents: an explicit colour wins in EVERY
// state, Disabled included.
constexpr float kDisabledAlpha = 0.4f;

D2D1_COLOR_F Fade(D2D1_COLOR_F c) { c.a *= kDisabledAlpha; return c; }

// Derive hover / pressed shades from a caller-supplied colour, mirroring the identical
// helper in Button.cpp. Same reason it exists there: the theme has three accent shades
// and the override slot has one, so returning the override flat left a custom-coloured
// toggle with no pointer feedback at all. The shift direction keys off luminance so a
// near-white and a near-black override both move by a visible amount.
D2D1_COLOR_F ShiftForState(const D2D1_COLOR_F& base, float amount) {
    const float lum = 0.2126f * base.r + 0.7152f * base.g + 0.0722f * base.b;
    const float target = (lum < 0.5f) ? 1.0f : 0.0f;
    D2D1_COLOR_F out = base;
    out.r = base.r + (target - base.r) * amount;
    out.g = base.g + (target - base.g) * amount;
    out.b = base.b + (target - base.b) * amount;
    return out;   // alpha untouched
}
constexpr float kHoverShift = 0.06f;
constexpr float kPressShift = 0.12f;
}  // namespace

D2D1_COLOR_F ToggleCheckedFill(VisualState state,
                                const ToggleAppearance& appearance,
                                const ColorTokens& colors)
{
    // The explicit accent wins in all states; without one the theme's ramp applies.
    // Note the asymmetry: the theme has three variants (`accent`, `accentHover`,
    // `accentPressed`); the override has one. The user sets one colour and gets it
    // in all states; setting a hover variant separately was not exposed, because
    // there is no way to communicate it via the base `Control::SetAccentColor` API.
    // Disabled is checked FIRST so it wins over Hover/Pressed: pointing at an inert
    // control must not make it look pressable. Before this, Disabled fell through to
    // `default:` and a disabled toggle painted at full accent strength -- visually
    // identical to a live one.
    if (appearance.accent) {
        const D2D1_COLOR_F& a = *appearance.accent;
        switch (state) {
            case VisualState::Disabled: return Fade(a);
            // Derived rather than flat, so a custom-coloured toggle answers the pointer
            // like a themed one. At rest the caller still gets their exact colour.
            case VisualState::Hover:
                return appearance.accentHover.value_or(ShiftForState(a, kHoverShift));
            case VisualState::Pressed:
                return appearance.accentPressed.value_or(ShiftForState(a, kPressShift));
            default:                    return a;
        }
    }
    switch (state) {
        case VisualState::Disabled: return Fade(colors.accent);
        case VisualState::Pressed:
            return appearance.accentPressed.value_or(colors.accentPressed);
        case VisualState::Hover:
            return appearance.accentHover.value_or(colors.accentHover);
        default:                    return colors.accent;
    }
}

D2D1_COLOR_F ToggleUncheckedFill(VisualState state,
                                  const ToggleAppearance& appearance,
                                  const ColorTokens& colors)
{
    // Background override wins; without one, hover lifts to `controlFillHover`.
    // Pressed reads as hover rather than getting a third intermediate shade, because
    // it is transient — the press is one frame from flipping the toggle, at which
    // point the accent fill is what will show. An extra shade between unchecked and
    // checked reads as a flicker rather than a state transition.
    // An explicit BACKGROUND wins in every state, Disabled included, and is NOT faded.
    // This is Button's documented rule and the reason is the same: a caller who sets an
    // exact brand colour and watches it shift has no way to say "I meant this one".
    // Contrast the accent override in ToggleCheckedFill, which Button does fade -- accent
    // is a semantic role the control derives states from, background is a literal value.
    if (appearance.background) {
        return *appearance.background;
    }
    // Again Disabled before Hover, for the same reason.
    if (state == VisualState::Disabled) {
        return Fade(colors.controlFillDefault);
    }
    if (state == VisualState::Pressed && appearance.backgroundPressed) {
        return *appearance.backgroundPressed;
    }
    if (ToggleIsHovered(state)) {
        return appearance.backgroundHover.value_or(colors.controlFillHover);
    }
    return colors.controlFillDefault;
}

D2D1_COLOR_F ToggleMarkColor(VisualState state,
                              const ToggleAppearance& appearance,
                              const ColorTokens& colors)
{
    // Foreground override wins; without one falls back to `onAccent` because the
    // mark sits on the accent fill (CheckBox tick and RadioButton dot) or has the
    // accent track behind it (ToggleSwitch knob) and needs to contrast with that,
    // not with the window background.
    //
    // An explicit foreground is NOT faded when disabled -- same rule as Button's
    // explicit background: the caller gave an exact value. The fill under it fades
    // either way, so the control still reads as inert.
    if (appearance.foreground) return *appearance.foreground;
    return state == VisualState::Disabled ? Fade(colors.onAccent) : colors.onAccent;
}

D2D1_COLOR_F ToggleBorderColor(VisualState state,
                                const ToggleAppearance& appearance,
                                const D2D1_COLOR_F& themed)
{
    // No borderBrush slot on ToggleAppearance: the three controls resolve their own
    // override through Control::EffectiveBorderBrush and pass the result in as
    // `themed`, so this only decides the disabled treatment. Widening the struct just
    // to move that lookup here would give two places that answer "what is the border".
    UNREFERENCED_PARAMETER(appearance);
    return state == VisualState::Disabled ? Fade(themed) : themed;
}

D2D1_COLOR_F ToggleLabelColor(VisualState state,
                               const ToggleAppearance& appearance,
                               const ColorTokens& colors)
{
    // appearance is taken but intentionally unused: see the header for why the label
    // must not adopt the mark's foreground override. Kept in the signature so the
    // three controls call all four resolvers with the same arguments, which is what
    // stops one of them drifting.
    UNREFERENCED_PARAMETER(appearance);
    return state == VisualState::Disabled ? colors.textSecondary : colors.textPrimary;
}

}  // namespace fluent
