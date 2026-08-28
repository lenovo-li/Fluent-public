// Button.h — Fluent rounded button.
//
// The base (rounded fill + label) and the hover/press highlight are both painted
// into the window content surface via Render(). The highlight tint fades in
// and out on the UI thread through the per-frame animation tick (the same
// mechanism as ToggleSwitch's knob), so a small control does not own its own
// DComp surface (roadmap §13.4/§26 forbid a Surface per small control). DWrite is
// injected through the tree UIContext (roadmap §6.2), not a manual call.
//
// Button is the canonical ButtonBase (roadmap §WP-06): ButtonBase supplies the
// text label (via ContentControl), the focusable+clickable setup, and the shared
// click / Space / Enter activation funnelled to OnActivate(); Button just raises
// its Click event there and paints the accent/standard chrome + tint.
#pragma once

#include "primitives/ButtonBase.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"
#include "../core/IContextMenu.h"
#include "../input/RoutedEvent.h"
#include "../graphics/DrawingContext.h"
#include "../styling/ThemeTokens.h"
#include <optional>
#include <string>
#include <vector>

namespace fluent {

class Button : public ButtonBase {
public:
    // Standard: filled + bordered (the default chrome). Accent: theme-accent fill
    // for the primary action. Subtle: transparent and borderless at rest, a fill
    // appears only on hover/press — the WinUI CommandBar/ToolBar button look.
    enum class Kind { Standard, Accent, Subtle };

    // ButtonBase() already sets focusable + clickable.
    Button() = default;

    // Text lives in ContentControl (SetText dirties Measure).
    //
    // Kind is MEASURE-level, not Render-level as the old comment here claimed ("only
    // swaps the fill palette ... so it is Render-only"). It does swap the palette, but
    // it also selects the font WEIGHT: Kind::Accent draws SemiBold, everything else
    // Normal. SemiBold is wider for the same string, so the desired width genuinely
    // changes with the kind, and a Render-only flag left the label measured at the old
    // weight until something unrelated forced a re-measure.
    //
    // The general rule from Invalidation.h applies both directions: pick the LOWEST
    // level that is actually sufficient — but a property feeding Measure must say so,
    // or the frame silently uses a stale desired size.
    void SetKind(Kind k) { SetProperty(kind_, k, DirtyFlags::Measure); }

    // Fired when the button is activated (click or Space/Enter). Replaces
    // SetOnClick(void(*)(void*),ctx): app does Click().Subscribe(this, &thunk).
    Event<Button, RoutedEventArgs>& Click() { return click_; }

    // P1-16: Button → Flyout integration. When set, clicking the button opens the
    // flyout anchored below it instead of firing Click. The app wires
    // `btn->SetFlyout(menu)` once rather than `btn->Click().Subscribe` +
    // `menu->ShowBelow(...)`. Ownership: the Button holds a non-owning pointer; the
    // caller keeps the flyout alive.
    void SetFlyout(IContextMenu* flyout) { flyout_ = flyout; }
    IContextMenu* Flyout() const { return flyout_; }

    void Render(const DrawingContext& dc) override;

    // Natural size: 32 DIP tall (WinUI default), width = text width + horizontal
    // padding. Without this Button falls back to FrameworkElement::Measure,
    // which reports the available size — in an unconstrained (infinity) cross
    // axis that collapses to zero, and the button disappears inside auto-sized
    // containers (the StackPanel star-removal exposed this).
    void Measure(float availW, float availH) override;

    // The focus ring is stroked OUTSIDE bounds_ (WP-07 §S4) — see FocusRingPadDip.
    float VisualOverflowDip() const override;

    // Highlight tint animates on the UI thread: run a tick while the current
    // opacity has not yet reached the state-driven target.
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:
    // ButtonBase routes click + Space/Enter here; Button raises Click.
    void OnActivate() override;

private:
    // Open the flyout_ anchored below this button. Helper extracted from
    // OnActivate so the positioning logic has a name.
    void OpenFlyout();

    // Target highlight opacity for the current visual state.
    float TintTarget() const;

    Kind kind_ = Kind::Standard;
    // Current highlight-tint opacity [0..1], eased toward TintTarget() each tick.
    AnimatedValue tintOpacity_{0.0f};

    Event<Button, RoutedEventArgs> click_;
    IContextMenu* flyout_ = nullptr;
};

// --- Testable color decisions (阶段 4 交叉发现 #4) -------------------------
// The fill/text color logic extracted as pure functions so tests can verify "Button
// in state X with override Y uses color Z" without needing a window, device, or a
// recording fake (see motivation in ControlPropertyTests.cpp where these are used).
//
// Why not a fake DrawingContext? That would require virtualizing the 11 draw
// methods, which puts a vtable dispatch on every painted primitive in every frame
// of every app — rejected per project documentation's "small and fast" criteria. Extracting
// the decision is zero-cost: Render calls these and paints the result.
//
// Each takes the overrides + theme as separate args so a test constructs only
// the relevant data, not a whole Control. `std::nullopt` means "not set, use theme".

struct ButtonAppearance {
    std::optional<D2D1_COLOR_F> background;
    std::optional<D2D1_COLOR_F> foreground;
    std::optional<D2D1_COLOR_F> accent;
    // Optional per-state overrides. Unset means ButtonFillColor derives the shade from
    // the base override above; with no base override either, the theme ramp wins. This
    // is what lets one SetBackground call still produce real hover/press feedback.
    std::optional<D2D1_COLOR_F> backgroundHover;
    std::optional<D2D1_COLOR_F> backgroundPressed;
    std::optional<D2D1_COLOR_F> accentHover;
    std::optional<D2D1_COLOR_F> accentPressed;
};

// The fill color Button paints behind its label. Alpha may be 0 (Subtle at rest);
// caller should skip the draw when a <= 0 rather than issuing a transparent fill.
D2D1_COLOR_F ButtonFillColor(Button::Kind kind, VisualState state,
                              const ButtonAppearance& appearance,
                              const ColorTokens& colors);

// The color Button paints its label in. Accent uses onAccent (light text for
// contrast); Standard/Subtle use textPrimary. Disabled drops to textSecondary.
D2D1_COLOR_F ButtonTextColor(Button::Kind kind, VisualState state,
                              const ButtonAppearance& appearance,
                              const ColorTokens& colors);

// Whether Button strokes a border. Subtle is borderless; Accent/Standard are not.
inline bool ButtonDrawsBorder(Button::Kind kind) {
    return kind != Button::Kind::Subtle;
}

} // namespace fluent
