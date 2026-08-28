// Expander.h — a collapsible section: a clickable header row over a content area
// that shows or hides. WPF's Expander / WinUI's Expander.
//
// WHY THIS DERIVES FROM FrameworkElement RATHER THAN Panel OR Control.
//
// Panel would give it a children_ vector, but an Expander has exactly one content
// child and a header that is NOT a child — the header is drawn by Expander itself
// (a chevron glyph plus a text run), the same call Border makes for its chrome.
// Making the header a real Button child would put a second focusable stop in the
// Tab order for something that is conceptually one control, which is the mistake
// TabControl's header comment already documents for its strip.
//
// Control would give it Background/Padding/theme chrome, which is closer, but
// Control does not own a child. So this follows Border's shape: a single-child
// decorator that forwards the whole element contract down, plus its own header
// painting and click handling on top.
//
// COLLAPSED CONTENT IS DETACHED, NOT MERELY HIDDEN.
//
// When collapsed, the content subtree is detached from the live tree (no context,
// no subscriptions, no animation registration) exactly as TabControl does for an
// unselected tab. SetVisible(false) would have been simpler but leaves the subtree
// attached and ticking. The trade-off is the same one TabControl documents: state
// that must survive a collapse belongs in the control's own members, not in a
// context-scoped resource acquired in OnAttachedToTree.
//
// THE EXPAND ANIMATION IS HEIGHT-ONLY AND UI-THREAD.
//
// Expanding eases the content's clip height from 0 to its desired height via the
// per-frame animation tick, the same mechanism Button's tint and ToggleSwitch's
// knob use. It is deliberately NOT composited: the animation is short (~150ms),
// runs once per user click rather than continuously, and a DComp surface per
// Expander would violate the "no Surface per small control" rule. WantsAnimationTick
// goes false the moment the ease finishes, so an idle Expander costs nothing.
#pragma once

#include "../core/FrameworkElement.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"
#include "../input/RoutedEvent.h"
#include "../styling/FocusVisual.h"   // FocusRingPadDip / FocusRingSpec (VisualOverflowDip)
#include <memory>
#include <string>

namespace fluent {

class Expander : public FrameworkElement {
public:
    Expander() {
        SetFocusable(true);
        SetClickable(false);  // we handle press ourselves: only the header is hot
    }

    // Take ownership of the content; returns a borrowed pointer to configure.
    // Replaces any existing content. Attaches immediately only if this Expander is
    // attached AND expanded — a collapsed Expander keeps its content detached.
    template <typename T>
    T* SetContent(std::unique_ptr<T> child) {
        T* raw = child.get();
        if (content_ && content_->IsAttached()) content_->DetachFromContext();
        if (content_) content_->SetParent(nullptr);
        content_ = std::move(child);
        if (content_) {
            content_->SetParent(this);
            SyncContentAttachment();
        }
        InvalidateDirty(DirtyFlags::Measure);
        return raw;
    }
    FrameworkElement* Content() const { return content_.get(); }

    // The header text drawn beside the chevron.
    void SetHeader(std::wstring text) {
        header_ = std::move(text);
        InvalidateDirty(DirtyFlags::Measure);
    }
    const std::wstring& Header() const { return header_; }

    // How a change of expanded state should reach the screen.
    //
    // The reveal machinery (reveal_, OnAnimationTick, kRevealTau) has existed since
    // this control was written, but nothing ever ran it: SetExpanded called
    // reveal_.SetImmediate(...), which JUMPS the value to its target, so
    // WantsAnimationTick() was already false on the line after any state change and
    // the ease was dead code. Rather than switch every caller to an animation they
    // did not ask for, the choice is explicit and the default preserves the
    // behaviour that shipped.
    //
    // Named values rather than a bare bool: `SetExpanded(true, true)` at a call site
    // says nothing about what the second argument means. Same reasoning as
    // ScrollViewer keeping SetOffset and AnimateTo as separate spellings.
    enum class Transition {
        Instant,   // snap to the new state on this frame (the historical behaviour)
        Animate,   // ease over kRevealTau, driven by the frame loop
    };

    // Expanded state. Attaches/detaches the content and moves the reveal toward the
    // new state. Silent when unchanged, so a redundant set never schedules a frame.
    //
    // Transition::Animate requires the element to be in a live tree whose host ticks
    // animations — the frame loop drives this through WantsAnimationTick() /
    // OnAnimationTick(), exactly like ToggleSwitch's knob. On a host with no
    // animation registry the ease simply never advances, so a caller that cannot
    // guarantee a ticking host should ask for Instant. (Detached is not a problem for
    // correctness: the state is already correct, only the interpolation is missing.)
    void SetExpanded(bool expanded, Transition transition = Transition::Instant);
    bool IsExpanded() const { return expanded_; }

    // Whether a HEADER CLICK (or Space/Enter on the header) animates. Programmatic
    // SetExpanded is unaffected — it takes its own Transition argument.
    //
    // Separate from SetExpanded's parameter because the two have different callers:
    // app code picks a transition per call, whereas the response to a user gesture is
    // a property of the control. Defaults to Instant so enabling the animation is an
    // opt-in decision rather than something that changes on upgrade.
    void SetUserToggleTransition(Transition t) { userToggle_ = t; }
    Transition UserToggleTransition() const { return userToggle_; }

    // Fired when the user toggles the header (click, Space, or Enter). Payload is
    // the new expanded state. NOT raised by a programmatic SetExpanded — same
    // convention as ToggleButton, where the notification means "the user did this".
    Event<Expander, bool>& ExpandedChanged() { return expandedChanged_; }

    // Whether the content is attached to the live tree. Exposed because
    // "collapsed means detached" is the design and a test has no other way to see it.
    bool IsContentAttached() const;

    // The header row's rect in window DIPs. Public so a headless test can check
    // hit-testing without a device, matching TabControl::HeaderRect.
    RectDip HeaderRect() const;

    // --- Element contract -------------------------------------------------
    void Render(const DrawingContext& dc) override;
    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    UIElement* HitTestDropTarget(float dipX, float dipY) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;
    void CollectDirtyBounds(std::vector<RectDip>& out) override;
    void CollectAnimations(std::vector<UIElement*>& out) override;
    void OnVisibilityChanged(bool visible) override;
    void OnAncestorVisibilityChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnDeviceLost() override;
    void OnDeviceRestored() override;
    bool AnyDirtyInSubtree(DirtyFlags flags) const override;
    void ClearDirtySubtree() override;
    bool NeedsRemeasure() const override { return AnyDirtyInSubtree(DirtyFlags::Measure); }

    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    // The focus ring is stroked OUTSIDE the header row, so it does spill.
    //
    // This used to return 0.0 with the comment "stroked just inside the header row,
    // so nothing spills". That was wrong: Render calls DrawFocusRing(hdrRect, ...),
    // HeaderRect() is {bounds_.x, bounds_.y, bounds_.w, HeaderHeight()} — i.e. it
    // shares the control's left, top and right edges — and FocusRingRect INFLATES
    // its rect by spec.inset (2.0) before stroking with a 1.5 centered stroke. So
    // the ring's outer edge sits 2.75 DIP outside bounds_ on three sides, and a
    // partial redraw clipped it there and left the outer edge behind as residue
    // once focus moved on.
    //
    // Using FocusRingPadDip with the same spec Render passes keeps the declaration
    // tied to the drawing: the spec's inset/stroke defaults can change in one place
    // without this silently disagreeing. Only cornerRadius differs from the default
    // (Render supplies the theme's), and the pad does not depend on it.
    //
    // SECOND CONSUMER, do not narrow this without reading Render: the content clip
    // is inflated by this SAME pad so a focused child flush against the content edge
    // keeps its ring (see the long comment at the clip in Expander.cpp). That makes
    // the widened clip's outer edge coincide exactly with what this declares, so the
    // dirty region already covers it. Returning less here would reintroduce residue
    // — this time from the CHILD's ring rather than the header's.
    float VisualOverflowDip() const override {
        return FocusRingPadDip(FocusRingSpec{});
    }

protected:
    // Propagate the tree context to the content — but only while expanded. A
    // collapsed Expander's content must stay detached even when the Expander itself
    // attaches, which is why this filters rather than forwarding unconditionally
    // (the same reason TabControl overrides AttachChildren).
    void AttachChildren(const UIContext& ctx) override {
        if (content_ && expanded_) content_->AttachToContext(ctx);
    }
    void DetachChildren() override {
        if (content_ && content_->IsAttached()) content_->DetachFromContext();
    }
    void UpdateChildrenContextDpi(float dpiScale) override {
        if (content_) content_->UpdateContextDpi(dpiScale);
    }
    void UpdateChildrenContextModalResize(bool inModalResize) override {
        if (content_) content_->UpdateContextModalResize(inModalResize);
    }

private:
    // Attach the content iff expanded, detach it otherwise. Called on every state
    // change and whenever the content is replaced.
    void SyncContentAttachment();
    // Flip the state, raise the user-driven event, start the ease.
    void ToggleFromUser();
    // Height of the header row: one line of body text plus vertical padding.
    float HeaderHeight() const;
    // The content's desired height, or 0 when there is none.
    float ContentDesiredHeight() const;

    std::unique_ptr<FrameworkElement> content_;
    std::wstring header_;
    bool expanded_ = false;
    bool headerHovered_ = false;
    // What a header click does. Instant by default — see SetUserToggleTransition.
    Transition userToggle_ = Transition::Instant;
    // Reveal progress: 0 fully collapsed, 1 fully expanded. Drives the content clip
    // height and the chevron rotation.
    AnimatedValue reveal_{0.0f};
    Event<Expander, bool> expandedChanged_;
};

} // namespace fluent
