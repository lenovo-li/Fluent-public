// ToggleVisuals.h — the colour decisions a two-state toggle makes, as pure
// functions shared by CheckBox, RadioButton and ToggleSwitch.
//
// Why extracted (阶段 4 交叉发现 #4). `ControlPropertyTests` proves that
// `SetAccentColor` stores a colour and `EffectiveAccentColor()` returns it. It does
// NOT prove that a control's `Render` ever calls it. A control still reading
// `pal.accent` directly passes the whole suite, because nothing observes what was
// painted. Substituting a recording fake into `Render` is not available here: see
// the note in Button.h — `DrawingContext` is non-virtual on purpose, and making its
// 11 draw methods virtual would put a vtable dispatch on every painted primitive.
//
// Why ONE header for three controls rather than per-control functions. The three
// were checked before writing this: CheckBox and RadioButton resolve their fill
// with *identical* logic (same override precedence, same hover/pressed token
// choice, same interpolation against the check/dot animation) and differ only in
// the geometry they hand to D2D — a rounded rect versus an ellipse. Duplicating the
// resolution per control is how the two would drift; the framework already has one
// instance of that (multi-select living in TreeView instead of `Selector<T>`, noted
// in the 阶段 4 audit) and it is not worth a second.
//
// ToggleSwitch is included because its track fill answers the same question
// ("checked → accent, unchecked → control fill, honour the override"), even though
// it reads no hover state today. Sharing the function is what keeps it that way by
// accident rather than by omission.
//
// What these catch and what they do not. They catch the case the 阶段 1 property
// migration could have introduced 20 times: a state branch that ignores the user's
// override, or an override that silently stops applying in one state. They do not
// catch a `Render` that resolves the right colour and then passes a different
// variable to `FillRoundedRect` — that needs real pixels and is a hardware check.
// Keeping `Render` a thin caller of these is what holds that gap to one readable line.
#pragma once

#include "../core/UIElement.h"      // VisualState
#include "../styling/ThemeTokens.h" // ColorTokens
#include <d2d1.h>
#include <optional>

namespace fluent {

// The instance-level colour overrides a toggle carries (the 阶段 1 property layer).
//
// Passed as a value rather than as `const Control&` so a test needs no live element
// and no attached tree, and so the whole fallback chain is visible at the call site
// instead of spread across `EffectiveXxx()` calls inside a `Render` body.
// `std::nullopt` means "not set — fall back to the theme", matching what `Control`'s
// optional members mean.
struct ToggleAppearance {
    std::optional<D2D1_COLOR_F> background;  // unchecked fill
    std::optional<D2D1_COLOR_F> accent;      // checked fill
    std::optional<D2D1_COLOR_F> foreground;  // checkmark / dot / knob
    // Optional per-state overrides, same rule as ButtonAppearance: an explicit state
    // colour wins, otherwise the state is derived from the base override, otherwise
    // the theme ramp applies. This is what removes the old asymmetry noted below --
    // callers CAN now say "and this is my hover variant".
    std::optional<D2D1_COLOR_F> backgroundHover;
    std::optional<D2D1_COLOR_F> backgroundPressed;
    std::optional<D2D1_COLOR_F> accentHover;
    std::optional<D2D1_COLOR_F> accentPressed;
};

// The CHECKED fill (CheckBox box, RadioButton disc, ToggleSwitch track).
//
// Precedence, and why: an explicit accent wins in every state, hover and pressed
// included. The alternative — letting hover shift a user-set colour toward the
// theme's `accentHover` — was rejected because a caller who sets an exact brand
// colour has no way to also specify "and this is my hover variant"; there is one
// override slot, so it has to mean the colour they get. Without an override the
// theme's own three-state ramp applies, which is the historical behaviour.
D2D1_COLOR_F ToggleCheckedFill(VisualState state,
                               const ToggleAppearance& appearance,
                               const ColorTokens& colors);

// The UNCHECKED fill. Hover lifts to `controlFillHover`; Pressed deliberately reads
// as hover here rather than getting its own token, because an unchecked toggle that
// is being pressed is one frame from being checked and the checked fill is what the
// user is about to see — a third distinct shade in between reads as a flicker.
D2D1_COLOR_F ToggleUncheckedFill(VisualState state,
                                 const ToggleAppearance& appearance,
                                 const ColorTokens& colors);

// The mark drawn ON the checked fill: CheckBox's tick, RadioButton's dot,
// ToggleSwitch's knob. Falls back to `onAccent` because it sits on the accent fill
// and needs to contrast with it, not with the window background.
//
// Takes `state` because Disabled has to fade it. This signature originally omitted
// state, which is exactly why a disabled CheckBox painted a crisp white tick on a
// faded box -- half the control greyed out and half did not, which reads as a
// rendering glitch rather than as a disabled control. An explicit foreground still
// survives Disabled unfaded (Button's rule: an exact colour the caller supplied is
// not the framework's to alter); the fill underneath it fades regardless, so the
// control still reads as inert.
D2D1_COLOR_F ToggleMarkColor(VisualState state,
                             const ToggleAppearance& appearance,
                             const ColorTokens& colors);

// The disabled treatment for the outline around the box / ring / track. `themed` is
// what the control already resolved (its own BorderBrush override if set, else
// controlStrokeDefault) -- this only decides whether to fade it.
//
// The fade matters most on the UNCHECKED visual, where the outline is nearly the only
// thing drawn: a full-strength border there made a disabled empty checkbox look
// identical to a live one.
D2D1_COLOR_F ToggleBorderColor(VisualState state,
                               const ToggleAppearance& appearance,
                               const D2D1_COLOR_F& themed);

// The colour for the control's text label (CheckBox / RadioButton / ToggleSwitch all
// draw one to the right of their glyph).
//
// Deliberately NOT derived from appearance.foreground: on a toggle, Foreground means
// the MARK's colour, and a caller who themed the tick purple did not ask for purple
// text. Setting both to the same value is what made a tick vanish once already; having
// the label silently follow it would spread that confusion instead of containing it.
//
// Disabled drops to textSecondary, matching ButtonTextColor. Without this the label of
// a disabled toggle stayed full-contrast black next to its faded box.
D2D1_COLOR_F ToggleLabelColor(VisualState state,
                              const ToggleAppearance& appearance,
                              const ColorTokens& colors);

// True when `state` should render with the hover treatment. Pressed counts as hover
// for the reason given on ToggleUncheckedFill. Exposed as a named predicate because
// all three controls asked the same `State() == Hover || State() == Pressed`
// question inline, and one of them getting that expression wrong is invisible.
inline bool ToggleIsHovered(VisualState state) {
    return state == VisualState::Hover || state == VisualState::Pressed;
}

}  // namespace fluent
