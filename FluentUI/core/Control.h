// Control.h — the interactive-control layer (roadmap §5.4, chrome implemented 2026-08-08).
//
// Control is the base for controls that have appearance (chrome) of their own:
// Foreground, Background, BorderBrush, AccentColor, CornerRadius, BorderThickness,
// FontSize, FontWeight. Every one is OPTIONAL — unset means "use the theme".
//
//   priority:  control instance property  >  theme token  >  library default
//
// This is the WPF model: a user can restyle one control without touching the theme,
// and a control that was never touched keeps following theme changes (including
// light/dark flips) automatically.
//
//   button->SetForeground(Rgb(0xFF0000));   // this button's label is red
//   button->SetCornerRadius(12.0f);         // and rounder than the theme
//   otherButton                              // still fully theme-driven
//
// Each property has three members:
//   SetXxx(v)      set the override (Render-dirty, or Measure-dirty when it can
//                  change desired size — font size and weight can)
//   ClearXxx()     drop the override, revert to the theme
//   EffectiveXxx() the resolved value; THIS is what a control's Render/Measure calls
//
// A control must never read Theme() for something that has an Effective accessor —
// doing so silently ignores the user's override. `has_value()` on the member is the
// one legitimate reason to look at the raw optional: a control whose default is
// state-dependent (Button's fill differs per hover/press/Accent-kind) checks whether
// the user pinned a value before running its own state machine.
//
// FontWeight is the odd one out: its fallback is a control-semantics decision, not a
// theme token (Button::Accent is SEMI_BOLD while Standard is NORMAL; a selected tab is
// SEMI_BOLD while an unselected one is NORMAL). So EffectiveFontWeight takes the
// control's own default as an argument instead of reading one global value. Same for
// EffectiveAccentColor's overload — a control that needs the hover/pressed accent
// passes that token in and still honours a user override.
//
// Implementations live in Control.cpp, not here: resolving a fallback needs the
// complete ThemeSnapshot, and UIElement.h only forward-declares it.
#pragma once

#include "FrameworkElement.h"
#include <optional>
#include <dwrite.h>

namespace fluent {

class Control : public FrameworkElement {
public:
    // --- Foreground: text / glyph color. Falls back to colors.textPrimary. -----
    std::optional<D2D1_COLOR_F> Foreground() const { return foreground_; }
    void SetForeground(const D2D1_COLOR_F& c);
    void ClearForeground();
    bool HasForeground() const { return foreground_.has_value(); }
    D2D1_COLOR_F EffectiveForeground() const;
    // Overload for a control whose own default is not textPrimary (e.g. a label on
    // an accent fill wants onAccent, a disabled label wants textSecondary).
    D2D1_COLOR_F EffectiveForeground(const D2D1_COLOR_F& fallback) const;

    // --- Background: control fill. Falls back to colors.controlFillDefault. ----
    std::optional<D2D1_COLOR_F> Background() const { return background_; }
    void SetBackground(const D2D1_COLOR_F& c);
    void ClearBackground();
    bool HasBackground() const { return background_.has_value(); }
    D2D1_COLOR_F EffectiveBackground() const;
    D2D1_COLOR_F EffectiveBackground(const D2D1_COLOR_F& fallback) const;

    // --- BorderBrush: control stroke. Falls back to colors.controlStrokeDefault. -
    std::optional<D2D1_COLOR_F> BorderBrush() const { return borderBrush_; }
    void SetBorderBrush(const D2D1_COLOR_F& c);
    void ClearBorderBrush();
    bool HasBorderBrush() const { return borderBrush_.has_value(); }
    D2D1_COLOR_F EffectiveBorderBrush() const;
    // For a control whose semantic stroke is not controlStrokeDefault (hover/pressed
    // states, or a specialized ring color).
    D2D1_COLOR_F EffectiveBorderBrush(const D2D1_COLOR_F& fallback) const;

    // --- AccentColor: selection / primary-action fill. Falls back to colors.accent.
    std::optional<D2D1_COLOR_F> AccentColor() const { return accentColor_; }
    void SetAccentColor(const D2D1_COLOR_F& c);
    void ClearAccentColor();
    bool HasAccentColor() const { return accentColor_.has_value(); }
    D2D1_COLOR_F EffectiveAccentColor() const;
    // For the hover / pressed accents: pass the token, keep the override winning.
    D2D1_COLOR_F EffectiveAccentColor(const D2D1_COLOR_F& fallback) const;

    // --- Interaction-state colours (hover / pressed) --------------------------
    //
    // WHY THESE EXIST. Before them, setting Background or AccentColor gave the control
    // ONE colour for every pointer state, so a custom-coloured Button stopped answering
    // the pointer: no hover lift, no press feedback. The theme ships three shades
    // (accent / accentHover / accentPressed) and the override slot held one.
    //
    // The rule, and it is the same rule the rest of Control follows: if the caller sets
    // a state colour they get exactly that; if they do not, the control DERIVES one from
    // the base override; if there is no override either, the theme's own ramp applies.
    // So a caller can opt into full control without being forced to specify three
    // colours to get working feedback from one.
    //
    // Derivation shifts toward white for a dark base and toward black for a light one,
    // keyed on luminance -- a fixed "add white" is invisible on a near-white button and
    // a fixed darken is invisible on a near-black one.
    std::optional<D2D1_COLOR_F> BackgroundHover() const { return backgroundHover_; }
    void SetBackgroundHover(const D2D1_COLOR_F& c);
    void ClearBackgroundHover();
    bool HasBackgroundHover() const { return backgroundHover_.has_value(); }

    std::optional<D2D1_COLOR_F> BackgroundPressed() const { return backgroundPressed_; }
    void SetBackgroundPressed(const D2D1_COLOR_F& c);
    void ClearBackgroundPressed();
    bool HasBackgroundPressed() const { return backgroundPressed_.has_value(); }

    std::optional<D2D1_COLOR_F> AccentColorHover() const { return accentHover_; }
    void SetAccentColorHover(const D2D1_COLOR_F& c);
    void ClearAccentColorHover();
    bool HasAccentColorHover() const { return accentHover_.has_value(); }

    std::optional<D2D1_COLOR_F> AccentColorPressed() const { return accentPressed_; }
    void SetAccentColorPressed(const D2D1_COLOR_F& c);
    void ClearAccentColorPressed();
    bool HasAccentColorPressed() const { return accentPressed_.has_value(); }

    // --- CornerRadius: falls back to spacing.cornerRadiusSmall. ---------------
    std::optional<float> CornerRadius() const { return cornerRadius_; }
    void SetCornerRadius(float dip);
    void ClearCornerRadius();
    bool HasCornerRadius() const { return cornerRadius_.has_value(); }
    float EffectiveCornerRadius() const;
    // For a control whose default is the larger radius (popups, cards).
    float EffectiveCornerRadius(float fallback) const;

    // --- BorderThickness: falls back to spacing.borderWidth. ------------------
    std::optional<float> BorderThickness() const { return borderThickness_; }
    void SetBorderThickness(float dip);
    void ClearBorderThickness();
    bool HasBorderThickness() const { return borderThickness_.has_value(); }
    float EffectiveBorderThickness() const;
    // For a control whose rest stroke is not the theme's border width (CheckBox and
    // RadioButton draw a 1.2 DIP ring, ToggleSwitch a 1.0 DIP track edge).
    float EffectiveBorderThickness(float fallback) const;

    // --- FontSize: falls back to typography.bodySize. Measure-dirty. ----------
    std::optional<float> FontSize() const { return fontSize_; }
    void SetFontSize(float dip);
    void ClearFontSize();
    bool HasFontSize() const { return fontSize_.has_value(); }
    float EffectiveFontSize() const;
    // For a control whose default is not body size (caption strips, menu items).
    float EffectiveFontSize(float fallback) const;

    // --- FontWeight: no theme token; the control supplies its own default. ----
    // Measure-dirty because a bolder face measures wider.
    std::optional<DWRITE_FONT_WEIGHT> FontWeight() const { return fontWeight_; }
    void SetFontWeight(DWRITE_FONT_WEIGHT w);
    void ClearFontWeight();
    bool HasFontWeight() const { return fontWeight_.has_value(); }
    DWRITE_FONT_WEIGHT EffectiveFontWeight(DWRITE_FONT_WEIGHT fallback) const;

protected:
    std::optional<D2D1_COLOR_F> foreground_;
    std::optional<D2D1_COLOR_F> background_;
    std::optional<D2D1_COLOR_F> borderBrush_;
    std::optional<D2D1_COLOR_F> accentColor_;
    // Optional per-state overrides; unset means "derive from the base, or follow the
    // theme when there is no base override either".
    std::optional<D2D1_COLOR_F> backgroundHover_;
    std::optional<D2D1_COLOR_F> backgroundPressed_;
    std::optional<D2D1_COLOR_F> accentHover_;
    std::optional<D2D1_COLOR_F> accentPressed_;
    std::optional<float> cornerRadius_;
    std::optional<float> borderThickness_;
    std::optional<float> fontSize_;
    std::optional<DWRITE_FONT_WEIGHT> fontWeight_;
};

} // namespace fluent
