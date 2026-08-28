// Button.cpp

#include "Button.h"
#include "../animation/Animation.h"
#include "../services/PopupGeometry.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include "../window/WindowServices.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float a) { c.a = a; return c; }

// Derive hover / pressed shades from a colour the CALLER supplied.
//
// WHY THIS EXISTS. The theme ships three accent shades and the override slot holds one,
// so an explicit AccentColor or Background used to return that single colour in Normal,
// Hover AND Pressed -- a custom-coloured button stopped answering the pointer entirely
// and felt dead compared to a themed one next to it. Deriving is the only option that
// keeps one override slot: there is no way for the caller to express "and this is my
// hover variant" through Control::SetAccentColor.
//
// The shift is TOWARD or AWAY from white depending on the colour's own luminance, not a
// fixed "add white". A fixed lift is invisible on a near-white button (already clipped
// at 1.0) and a fixed darken is invisible on a near-black one; keying off luminance
// means both ends move by the same perceived amount.
//
// Magnitudes mirror the theme's own ramp: its accent -> accentHover is roughly a 6%
// step and -> accentPressed roughly 12%, so a derived shade lands in the same
// neighbourhood as a themed one and the two feel consistent side by side.
D2D1_COLOR_F ShiftForState(const D2D1_COLOR_F& base, float amount) {
    const float lum = 0.2126f * base.r + 0.7152f * base.g + 0.0722f * base.b;
    // Toward white when dark, toward black when light. The 0.5 split is on perceived
    // luminance, so a mid-grey goes darker -- consistent with how the light theme's
    // controlFill ramp behaves.
    const float target = (lum < 0.5f) ? 1.0f : 0.0f;
    D2D1_COLOR_F out = base;
    out.r = base.r + (target - base.r) * amount;
    out.g = base.g + (target - base.g) * amount;
    out.b = base.b + (target - base.b) * amount;
    return out;   // alpha untouched: opacity is the caller's, not a state signal
}

// The two steps, named so the intent survives at the call site.
constexpr float kHoverShift = 0.06f;
constexpr float kPressShift = 0.12f;
// Gap between the button's bottom edge and the flyout's top edge, matching the
// 4 DIP ToolBar/ComboBox dropdown offset.
constexpr float kFlyoutGapDip = 4.0f;
} // namespace

// --- Testable color decisions (declared in Button.h) ----------------------

D2D1_COLOR_F ButtonFillColor(Button::Kind kind, VisualState state,
                              const ButtonAppearance& appearance,
                              const ColorTokens& colors)
{
    // An explicit background is used in EVERY state, Disabled included -- but the
    // pointer states are DERIVED from it rather than returning it flat. Returning it
    // flat is what made custom-coloured buttons feel dead: no hover, no press, no
    // feedback that the thing was even clickable. At rest the caller still gets exactly
    // the colour they asked for, which is what the override is for.
    if (appearance.background) {
        const D2D1_COLOR_F& bg = *appearance.background;
        switch (state) {
            // Explicit state colour wins; otherwise derive one from the base so the
            // control still responds to the pointer.
            case VisualState::Hover:
                return appearance.backgroundHover.value_or(ShiftForState(bg, kHoverShift));
            case VisualState::Pressed:
                return appearance.backgroundPressed.value_or(ShiftForState(bg, kPressShift));
            case VisualState::Disabled: return bg;   // exact value survives; text dims
            default:                    return bg;
        }
    }

    // The accent triplet falls back to the theme's accent when not overridden. Note
    // the asymmetry with hover/pressed: an accent override replaces all three states
    // (there is no separate "my accent hover"), which matches how the theme's own
    // accentHover/accentPressed derive from accent.
    const D2D1_COLOR_F accent = appearance.accent.value_or(colors.accent);

    if (kind == Button::Kind::Accent) {
        switch (state) {
            // With an override, derive the two pointer shades from it (see ShiftForState);
            // without one, use the theme's own ramp.
            case VisualState::Hover:
                if (appearance.accentHover) return *appearance.accentHover;
                return appearance.accent ? ShiftForState(accent, kHoverShift) : colors.accentHover;
            case VisualState::Pressed:
                if (appearance.accentPressed) return *appearance.accentPressed;
                return appearance.accent ? ShiftForState(accent, kPressShift) : colors.accentPressed;
            case VisualState::Disabled: return WithAlpha(accent, 0.4f);
            default:                    return accent;
        }
    }
    if (kind == Button::Kind::Subtle) {
        // CommandBar-style: nothing at rest so the strip behind shows through, a
        // quiet fill on hover/press. The rest color is alpha 0 rather than "skip
        // the draw" so the caller has one uniform value to test against.
        switch (state) {
            case VisualState::Hover:
                return appearance.backgroundHover.value_or(colors.controlFillHover);
            case VisualState::Pressed:
                return appearance.backgroundPressed.value_or(colors.controlFillPressed);
            default:                   return WithAlpha(colors.controlFillDefault, 0.0f);
        }
    }
    switch (state) {
        case VisualState::Hover:
            return appearance.backgroundHover.value_or(colors.controlFillHover);
        case VisualState::Pressed:
            return appearance.backgroundPressed.value_or(colors.controlFillPressed);
        case VisualState::Disabled: return WithAlpha(colors.controlFillDefault, 0.4f);
        default:                    return colors.controlFillDefault;
    }
}

D2D1_COLOR_F ButtonTextColor(Button::Kind kind, VisualState state,
                              const ButtonAppearance& appearance,
                              const ColorTokens& colors)
{
    // Same precedence rule as the fill: an explicit foreground survives Disabled.
    if (appearance.foreground) return *appearance.foreground;
    if (state == VisualState::Disabled) return colors.textSecondary;
    return (kind == Button::Kind::Accent) ? colors.onAccent : colors.textPrimary;
}

float Button::TintTarget() const {
    switch (State()) {
        case VisualState::Hover:   return 1.0f;
        case VisualState::Pressed: return 0.6f;
        default:                   return 0.0f;
    }
}

void Button::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    // WinUI Button metrics: MinHeight 32, horizontal padding 11+11, vertical 5+5.
    // The text is single-line body-size; use the shared label measurer so the
    // result matches CheckBox/RadioButton.
    const float fontSize = Theme().typography.bodySize;
    constexpr float kMinH = 32.0f;
    constexpr float kPadX = 11.0f * 2.0f;
    constexpr float kPadY = 5.0f * 2.0f;

    // Measure at the weight Render will actually draw: Accent buttons are SemiBold
    // (see the DrawText call below), which is WIDER than Normal for the same string.
    // Measuring Normal under-reported the width, so an Accent button's label could
    // overflow the box its own Measure asked for. Must stay in sync with Render.
    const DWRITE_FONT_WEIGHT weight = EffectiveFontWeight(
        (kind_ == Kind::Accent) ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                : DWRITE_FONT_WEIGHT_NORMAL);

    float labelW = MeasureLabelWidth(fontSize, availW, kMinH, weight);
    float w = labelW > 0.0f ? labelW + kPadX : 88.0f;  // empty label: sensible min
    float h = std::max(kMinH, fontSize + kPadY);

    SetDesired({IsAuto(width_) ? w : width_, IsAuto(height_) ? h : height_});
}

bool Button::WantsAnimationTick() const {
    return tintOpacity_.Animating(TintTarget());
}

void Button::OnAnimationTick(float dtSec) {
    tintOpacity_.Approach(TintTarget(), dtSec, Theme().motion.tintTau);
    // A state change re-targets the tint; the host re-collects animations on the
    // same input trigger (ButtonBase::OnStateChanged Invalidates), so the fade
    // runs. Keep invalidating each tick so the frame is scheduled while easing.
    Invalidate();
}

void Button::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& c = th.colors;
    const float corner = EffectiveCornerRadius();

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);

    // Base fill — delegate the color resolution to the testable pure function.
    ButtonAppearance appearance{
        HasBackground() ? std::optional(EffectiveBackground()) : std::nullopt,
        HasForeground() ? std::optional(EffectiveForeground()) : std::nullopt,
        HasAccentColor() ? std::optional(EffectiveAccentColor()) : std::nullopt,
        BackgroundHover(), BackgroundPressed(),
        AccentColorHover(), AccentColorPressed()
    };
    D2D1_COLOR_F fill = ButtonFillColor(kind_, State(), appearance, c);

    if (fill.a > 0.0f)
        dc.FillRoundedRect(rr, fill);

    // Animated highlight tint on top of the base fill (was a DComp overlay; now a
    // UI-thread fade). Light themes darken slightly; dark themes lighten.
    float tint01 = std::clamp(static_cast<float>(tintOpacity_), 0.0f, 1.0f);
    if (tint01 > 0.0f) {
        D2D1_COLOR_F tint = th.dark ? D2D1::ColorF(1, 1, 1, 0.10f)
                                    : D2D1::ColorF(0, 0, 0, 0.06f);
        tint.a *= tint01;
        dc.FillRoundedRect(rr, tint);
    }

    // Border (subtle for accent, visible for standard, absent for Subtle).
    if (ButtonDrawsBorder(kind_))
        dc.DrawRoundedRect(rr, EffectiveBorderBrush(), EffectiveBorderThickness());

    // Label. Win11: only the Accent button carries SemiBold; Standard and
    // Subtle use Normal weight. Foreground color delegated to the pure function.
    DWriteContext* dwrite = Dwrite();
    if (!text_.empty() && dwrite) {
        D2D1_COLOR_F textColor = ButtonTextColor(kind_, State(), appearance, c);
        const DWRITE_FONT_WEIGHT weight = EffectiveFontWeight(
            (kind_ == Kind::Accent) ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL
        );
        const float fontSize = EffectiveFontSize();
        if (auto* fmt = dwrite->Format(fontSize, weight)) {
            dc.DrawText(text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(),
                                    bounds_.bottom()),
                        textColor);
        }
    }

    // Focus ring: an accent outline just outside the button when focused.
    if (IsFocused()) {
        FocusRingSpec spec;
        spec.cornerRadius = corner;  // inset/stroke keep the historical defaults
        DrawFocusRing(dc, bounds_, c, spec);
    }
}

float Button::VisualOverflowDip() const {
    // The focus ring is stroked outside bounds_ with the default spec; the base fill's
    // own border is centered on the bounds edge and so falls well inside that pad.
    return FocusRingPadDip(FocusRingSpec{});
}

void Button::OnActivate() {
    // P1-16: a Button with a flyout is a menu button — activation opens the menu
    // instead of raising Click. Click stays unraised deliberately: a handler
    // subscribed to Click would otherwise run on every menu open, and the app has
    // no way to tell "the user picked me" from "the user opened my menu". The
    // per-item onInvoke callbacks are the flyout's activation signal.
    if (flyout_) {
        OpenFlyout();
        return;
    }

    RoutedEventArgs args;
    args.source = this;
    args.originalSource = this;
    click_.Raise(*this, args);
}

void Button::OpenFlyout() {
    if (!flyout_) return;

    // The flyout needs the tree context to reach the popup registries and DWrite.
    // UIElement::SetContextMenu does this for a right-click menu on attach, but a
    // Flyout is set independently and may have been assigned before attach — so
    // (re)feed the context at open time. Same lesson as ToolBar's overflow flyout,
    // where deferring this to OnAttachedToTree meant the very first click opened
    // nothing.
    if (!IsAttached()) return;
    flyout_->SetOwnerContext(Context());

    WindowServices* win = Window();
    if (!win || !win->Hwnd()) return;

    // Anchor at the button's bottom-left, converted from window DIPs to physical
    // screen pixels. IContextMenu exposes only ShowAt (a point), not MenuFlyout's
    // ShowBelow (a rect) — a point is enough here and keeps the core interface
    // from growing a second positioning virtual. The concrete flyout still flips
    // and clamps to the work area itself.
    RECT rcWindow;
    GetWindowRect(win->Hwnd(), &rcWindow);
    const RECT anchor =
        AnchorScreenRect(rcWindow.left, rcWindow.top, bounds_.x,
                         bounds_.y + bounds_.h, bounds_.w, kFlyoutGapDip,
                         win->DpiScale());
    flyout_->ShowAt(static_cast<int>(anchor.left), static_cast<int>(anchor.bottom));
}

} // namespace fluent
