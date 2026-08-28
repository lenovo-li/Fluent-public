// InfoBar.h — an inline status message: severity colour, title, message, optional close.
//
// WHY THIS CONTROL EXISTS. Every application needs "tell the user something went wrong /
// finished / needs attention" in the page flow rather than in a modal dialog. Without it
// each app reinvents the same Border + TextBlock + hard-coded colour, and the hard-coded
// colour is the part that goes wrong: a warning painted 0xFFF4CE is invisible in dark
// mode. The severity tokens this reads (ThemeTokens ColorTokens severity*) exist so the
// mapping from meaning to colour lives in the theme, once.
//
// It is INLINE, not a dialog. A ContentDialog interrupts and demands a decision; an
// InfoBar sits in the layout and reports. That distinction is why this is a plain
// FrameworkElement in the visual tree and takes part in normal layout, unlike
// MessageDialog which owns a popup HWND.
//
// SIZING. Height is content-driven: the message wraps at the available width and the
// control reports whatever height that needs, so a long message in a narrow column grows
// taller instead of being clipped. That makes it usable inside a ScrollPanel page without
// the caller having to guess a height. A single-line message with no title collapses to
// roughly one control height.
//
// WHAT IT DOES NOT DO (deliberately, so nobody is surprised):
//   * No action button. WinUI's InfoBar has one; adding it here means hit-testing,
//     focus order and a second click target, and no consumer needs it yet. Put a Button
//     next to the InfoBar in the parent panel instead.
//   * No open/close animation. Appearing instantly is correct for an error message, and
//     an ease would be a Measure-level animation (height changes), which is the most
//     expensive shape in this framework.
//   * No icon glyph font dependency. The severity mark is drawn geometrically (a filled
//     circle with a stroke) so it works with no icon font installed, which the headless
//     tests also rely on.

#pragma once
#include "../core/Control.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include <string>

namespace fluent {

class InfoBar : public Control {
public:
    // Maps 1:1 onto the four severity token pairs, and onto the four Streamlit-style
    // calls (st.info / st.success / st.warning / st.error) that motivated the control.
    enum class Severity { Informational, Success, Warning, Error };

    InfoBar();

    // The bold first line. Optional: an InfoBar with only a message is a valid,
    // common shape (a one-line notice), and then no vertical space is reserved for a
    // title at all.
    void SetTitle(std::wstring text);
    const std::wstring& Title() const { return title_; }

    // The body. Wraps at the available width; this is what drives the control's height.
    void SetMessage(std::wstring text);
    const std::wstring& Message() const { return message_; }

    void SetSeverity(Severity s);
    Severity GetSeverity() const { return severity_; }

    // Show a close affordance. Closing is the APP's decision, so this control only
    // reports the click through Closed(); it does not remove itself from the tree or
    // hide itself. A control that silently vanished would leave a hole in a layout the
    // parent still sizes, and "what happens on dismiss" (remove, collapse, remember)
    // differs per app.
    void SetClosable(bool closable);
    bool IsClosable() const { return closable_; }

    // Raised when the close affordance is clicked. RoutedEventArgs (rather than a bare
    // sender) to match Button::Click, so a handler written for one works for the other.
    Event<InfoBar, RoutedEventArgs>& Closed() { return closed_; }

    // Geometry, public for the same reason TabControl::HeaderRect is: a headless test
    // has no other way to check the close button's hit region.
    RectDip CloseButtonRect() const;

    // --- Element contract -------------------------------------------------
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;

    // Painted outside bounds_ when focused, like every other focusable control here.
    float VisualOverflowDip() const override;

private:
    // Resolved fill + stroke for the current severity. One place, so Render and the
    // tests cannot disagree about which token a severity uses.
    void SeverityColors(D2D1_COLOR_F& fill, D2D1_COLOR_F& stroke) const;

    std::wstring title_;
    std::wstring message_;
    Severity severity_ = Severity::Informational;
    bool closable_ = false;
    bool closeHovered_ = false;
    Event<InfoBar, RoutedEventArgs> closed_;
};

}  // namespace fluent
