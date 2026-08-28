// UIElement.cpp — see UIElement.h. Input, enabled/focus state, the visual state
// machine, and the tree attachment lifecycle.

#include "UIElement.h"
#include "../animation/AnimationRegistry.h"
#include "../styling/ThemeManager.h"
#include "../input/InputManager.h"

namespace fluent {

const ThemeSnapshot& UIElement::Theme() const {
    // A control's Render must never dereference null: while attached, return the
    // host's stable snapshot; otherwise return a shared default (light, no OS
    // accent) built once. Value-preserving so a headless-rendered control matches
    // an attached one for the light theme.
    if (context_.theme) return *context_.theme;
    static const ThemeSnapshot kDefault = BuildSnapshot(ThemeInputs{}, 0);
    return kDefault;
}

UIElement::~UIElement() {
    // Self-healing teardown for *every* destruction path (an element deleted
    // while still attached, e.g. a Panel::Clear or the whole tree going away).
    // The derived layers have already been destroyed by the time a base dtor
    // runs, so we must NOT dispatch OnDetachedFromTree / DetachChildren here
    // (those are virtual and would either no-op or hit a sliced object). Do only
    // the base-owned cleanup: drop from the host animation set and release the
    // context-scoped subscriptions. contextSubs_ would release on its own during
    // member destruction anyway; doing it before we clear the animation pointer
    // keeps the order explicit.
    if (context_.animations) context_.animations->Remove(this);
    contextSubs_.clear();
}

void UIElement::AttachToContext(const UIContext& ctx) {
    // Re-parenting: if already attached (possibly to a different context), detach
    // cleanly first so subscriptions/animation registration from the old tree do
    // not leak into the new one.
    if (attached_) DetachFromContext();

    context_ = ctx;
    attached_ = true;
    // Feed the context menu the tree services so it can render/position without any
    // manual wiring (same "attach injects services" rule, §6.2). Covers both
    // ownership modes: the owned instance and a shared reference (P1-21). With a
    // shared menu the last-attached element's context wins, which is correct —
    // ShowAt re-feeds the context at open time (see Button::OpenFlyout), so the menu
    // always positions against whichever element actually invoked it.
    if (IContextMenu* menu = ContextMenu()) menu->SetOwnerContext(context_);
    // §6.3 attach order: self first, then recurse to children so a parent's
    // OnAttachedToTree runs before its descendants'.
    OnAttachedToTree();
    AttachChildren(ctx);
}

void UIElement::DetachFromContext() {
    if (!attached_) return;

    // §6.3 detach order (reverse of attach): children first, then self, then
    // release this element's own registrations and drop it from the animation set,
    // finally clear the context so Context() reads empty while detached.
    DetachChildren();
    OnDetachedFromTree();
    contextSubs_.clear();
    if (context_.animations) context_.animations->Remove(this);
    context_ = UIContext{};
    attached_ = false;
}

void UIElement::SetEnabled(bool e) {
    if (enabled_ == e) return;
    enabled_ = e;
    if (!enabled_) {
        pointerInside_ = false;
        pointerDown_ = false;
    }
    UpdateState();
}

void UIElement::SetFocused(bool f) {
    if (focused_ == f) return;
    focused_ = f;
    OnFocusChanged();
    Invalidate();
}

void UIElement::OnPointerEnter() {
    if (!enabled_ || pointerInside_) return;
    pointerInside_ = true;
    UpdateState();
}

void UIElement::OnPointerLeave() {
    if (!pointerInside_ && !pointerDown_) return;
    pointerInside_ = false;
    pointerDown_ = false;
    OnPointerLeft();
    UpdateState();
}

// --- Routed click gesture (WP-03) ------------------------------------------
// The base pressed/released pair implements the shared click behavior for
// `clickable_` controls (Button/CheckBox/Radio/Toggle/ComboBox header/...): on a
// left-press we enter the Pressed state and capture the pointer so a drag beyond
// our bounds still delivers the matching up; on up we fire OnClickRouted only if
// released inside, then release capture. args.handled is set so the event does
// not keep bubbling to a parent once a clickable element consumes it. A control
// with custom press/drag semantics overrides these without chaining to the base.

void UIElement::OnPointerPressed(PointerEventArgs& e) {
    if (!clickable_ || !enabled_) return;
    if (e.button != PointerButton::Left) return;
    pointerDown_ = true;
    pointerInside_ = true;
    if (context_.input) context_.input->CapturePointer(this);
    UpdateState();
    e.handled = true;
}

void UIElement::OnPointerReleased(PointerEventArgs& e) {
    if (!clickable_) return;
    if (e.button != PointerButton::Left) return;
    bool wasDown = pointerDown_;
    pointerDown_ = false;
    bool inside = bounds_.contains(e.position.x, e.position.y);
    pointerInside_ = inside;
    if (context_.input && context_.input->Captured() == this)
        context_.input->ReleaseCapture(this);
    UpdateState();
    if (enabled_ && wasDown && inside) {
        OnClickRouted(e);
        e.handled = true;
    }
}

void UIElement::UpdateState() {
    VisualState next;
    if (!enabled_)
        next = VisualState::Disabled;
    else if (pointerDown_ && pointerInside_)
        next = VisualState::Pressed;
    else if (pointerInside_)
        next = VisualState::Hover;
    else
        next = VisualState::Normal;

    if (next != state_) {
        state_ = next;
        OnStateChanged();
        Invalidate();
    }
}

UIElement* UIElement::HitTestDropTarget(float dipX, float dipY) {
    // Base version: if this element accepts drops and the point is inside, return
    // this. Panels override to recurse to children (deepest-first).
    if (!IsVisible() || !hitTestVisible_) return nullptr;
    if (AcceptsDrop() && bounds_.contains(dipX, dipY)) return this;
    return nullptr;
}

} // namespace fluent
