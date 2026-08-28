// Hyperlink.h — clickable underlined text.
//
// A one-line text control that raises Click on activation and, when a URI is set
// and Ctrl is held, hands the URI to the shell instead. Focusable and clickable,
// so Space/Enter activate it the same way they activate a Button — a link that
// only responds to the mouse is unreachable from the keyboard.
//
// Deliberately NOT a ButtonBase subclass: ButtonBase brings ContentControl's text
// plus the standard button chrome, and a link wants neither the chrome nor the
// button's measure behavior (it sizes to its glyphs, with no minimum height).
// What it does share is the activation funnel, which is small enough to restate.
//
// Text is measured through the shared ResourceCache-free path: a layout is built
// on demand in Measure and reused for Render, and dropped when the text, font
// size, or theme generation changes. For a link INSIDE a paragraph of text, this
// is the wrong control — that needs inline ranges in one layout (P3 RichTextBox).
#pragma once

#include "../core/Control.h"
#include "../core/UIContext.h"  // BehaviorSettings
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../animation/AnimatedValue.h"
#include <string>

struct IDWriteTextLayout;

namespace fluent {

// How a Hyperlink with a URI activates. Mirrors BehaviorSettings::hyperlinkRequireCtrl
// but per-instance, and adds the "unset" state so an instance can fall back to
// the tree-wide BehaviorSettings.
enum class HyperlinkActivation {
    Default,     // follow UIContext.behavior->hyperlinkRequireCtrl (library default: CtrlClick)
    Click,       // plain click on a URI goes to the shell (WinUI behavior)
    CtrlClick,   // only Ctrl+click goes to the shell; plain click raises Click
};

class Hyperlink : public Control {
public:
    Hyperlink() {
        SetFocusable(true);
        SetClickable(true);
    }

    void SetText(std::wstring text);
    const std::wstring& Text() const { return text_; }

    // The URI opened on activation (subject to the activation policy below).
    // Empty (the default) means the control only raises Click and never
    // touches the shell.
    void SetUri(std::wstring uri) { uri_ = std::move(uri); }
    const std::wstring& Uri() const { return uri_; }

    // Font size comes from Control::SetFontSize / EffectiveFontSize (the shared
    // property layer). The old per-control SetFontSize/FontSize pair was removed
    // when that layer landed — this control kept a duplicate fontSize_ that could
    // silently disagree with the inherited one.

    // Per-instance activation policy. Default follows the tree-wide
    // BehaviorSettings::hyperlinkRequireCtrl; set to Click or CtrlClick to
    // override for this one link.
    void SetActivation(HyperlinkActivation a) { activation_ = a; }
    HyperlinkActivation Activation() const { return activation_; }

    // Raised on click / Space / Enter. NOT raised when the activation opened the
    // URI — the two are alternatives, so a handler that navigates does not also
    // run for a shell-handled activation.
    Event<Hyperlink, RoutedEventArgs>& Click() { return click_; }

    // Whether the last activation was routed to the shell rather than to Click.
    // Exposed because ShellExecute cannot run in a test, so this is the only way
    // to assert that the Ctrl branch was taken.
    bool LastActivationOpenedUri() const { return lastOpenedUri_; }

    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;
    HCURSOR Cursor() const override;

    // Hyperlink paints outside bounds_ on TWO counts, and declared neither, so a
    // partial redraw clipped both and left residue behind once the link stopped
    // painting them (see FocusVisual.h's note — the redraw region is exactly this
    // frame's dirty union, nothing widens it afterwards).
    //
    // 1. The focus ring is stroked at `bounds_ - 2.0` with a 1.0 centered stroke,
    //    so its outer edge sits 2.5 DIP outside on the left and top. Unlike
    //    ListBox / TreeView / TabControl / DatePicker — which all inset their ring
    //    INWARD by exactly half the stroke and genuinely spill nothing — this one
    //    is built outward from the bounds edge.
    // 2. The underline is drawn at `bounds_.y + textH + kUnderlineGap`, and when
    //    Height is explicit rather than Auto, Measure's textH (which includes the
    //    gap and thickness) has no say in bounds_.h — so a link with a Height
    //    smaller than its glyphs puts the underline below its own bottom edge,
    //    with no focus involved at all.
    //
    // Declared unconditionally, not `IsFocused() ? pad : 0`: the frame on which
    // focus LEAVES still has to clear the pixels the ring occupied on the previous
    // frame, and by then IsFocused() is already false. That is the whole reason the
    // contract in project documentation says unconditionally.
    float VisualOverflowDip() const override;

    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    // Public, matching the base declarations. Narrowing access on an override of
    // a public virtual is a mistake in its own right, and it also puts the two
    // activation paths out of a test's reach — which is where all the behavior is.
    void OnClickRouted(PointerEventArgs& e) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;

protected:
    void OnStateChanged() override { Invalidate(); }
    void OnThemeChanged() override;
    void OnAttachedToTree() override;

private:
    // Build layout_ for the current text/font, or clear it when there is nothing
    // to lay out. No-op while detached (no DWrite).
    void EnsureLayout();
    // Run the activation: shell when the resolved policy says "this gesture
    // opens the URI", else raise Click.
    void Activate(bool ctrlHeld);
    // Resolve the effective policy: instance override wins; otherwise the
    // tree-wide BehaviorSettings; otherwise the library default (CtrlClick).
    bool RequiresCtrl() const;

    std::wstring text_;
    std::wstring uri_;
    HyperlinkActivation activation_ = HyperlinkActivation::Default;
    // Hover/press underline + color fade, so the link does not snap between
    // states (matches Button's tint easing).
    AnimatedValue hoverFade_{0.0f};
    bool lastOpenedUri_ = false;

    ComPtr<IDWriteTextLayout> layout_;
    // Inputs the cached layout was built from; a mismatch forces a rebuild.
    std::wstring layoutText_;
    float layoutFontSize_ = 0.0f;  // cached EffectiveFontSize()
    uint32_t layoutThemeGen_ = 0;

    Event<Hyperlink, RoutedEventArgs> click_;
};

} // namespace fluent
