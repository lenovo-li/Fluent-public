// UIElement.h — the input / layout / lifecycle layer (roadmap §5.2).
//
// Adds to Visual everything about *being laid out and interacted with*:
//   * bounds (the arranged rect, DIPs) + hit-testing,
//   * the two-pass Measure/Arrange protocol + the measure short-circuit cache,
//   * pointer / keyboard / wheel input, focus, enabled state, visual state,
//   * per-frame animation ticking, caret blink, IME composition,
//   * tree traversal (hit-test deepest, collect focusables, collect animations).
//
// It still knows nothing about Width/Margin/Alignment (FrameworkElement) or
// Background/Padding/theme (Control). Measure/Arrange are pure virtual here and
// implemented by FrameworkElement, which interprets the layout properties.
#pragma once

#include "Visual.h"
#include "Layout.h"
#include "UIContext.h"
#include "../diagnostics/LayoutCostProbe.h"
#include "Subscription.h"
#include "IContextMenu.h"
#include "../input/RoutedEvent.h"
#include "../input/DragDrop.h"
#include <memory>
#include <string>
#include <vector>

namespace fluent {

enum class VisualState { Normal, Hover, Pressed, Disabled };

class UIElement : public Visual {
public:
    // Detaches from any live context so a destroyed element never leaves a
    // dangling registration behind (roadmap §6.1 rule 4). RAII on contextSubs_
    // already cancels subscriptions on destruction; this also drops the element
    // from the host's animation set. Non-virtual work only — the derived layers
    // are already gone by the time this runs, so it must not dispatch the
    // OnDetachedFromTree hook (that belongs to explicit, tree-driven detach).
    ~UIElement() override;

    // The user-declared destructor suppresses the implicit move operations, and
    // the move-only contextSubs_ member makes the copy operations ill-formed. Keep
    // the element movable (it was before this step) so a control can still be
    // returned/held by value in tests and builders; copy stays disabled. Moving an
    // *attached* element is not a supported operation — this exists only for fresh
    // (unattached) objects, whose contextSubs_ is empty and which are not yet in
    // any animation set.
    UIElement() = default;
    UIElement(UIElement&&) = default;
    UIElement& operator=(UIElement&&) = default;
    UIElement(const UIElement&) = delete;
    UIElement& operator=(const UIElement&) = delete;

    // --- Bounds / hit-testing ---------------------------------------------
    void SetBounds(const RectDip& r) {
        if (bounds_.x == r.x && bounds_.y == r.y && bounds_.w == r.w && bounds_.h == r.h)
            return;
        bounds_ = r;
        // The single choke point for the arrange side, instrumented for the same
        // reason MeasureCached is: OnBoundsChanged is a virtual whose subclass bodies
        // do real work (DWrite layout rebuilds, compositor visual re-fits, scroll
        // resyncs), and none of it was visible in `arrange=NNms`. The early return
        // above is deliberately NOT counted — an unchanged rect does no work, and
        // counting it would inflate the denominator with free calls.
        LayoutCostProbe::Bump(LayoutCountKey::BoundsChanged);
        // NOTE: this scope NESTS around any subclass-specific key (TextBlock etc.),
        // so BoundsChangedTotal is a superset, not a sibling, of those rows.
        LayoutCostProbe::Scope probe(LayoutCostKey::BoundsChangedTotal);
        OnBoundsChanged();
    }
    const RectDip& Bounds() const { return bounds_; }

    bool HitTest(float dipX, float dipY) const {
        return IsVisible() && hitTestVisible_ && enabled_ && bounds_.contains(dipX, dipY);
    }

    // --- Two-pass layout --------------------------------------------------
    // Measure fills desired_ given the space the parent offers; Arrange places
    // the element into finalRect. Pure virtual: FrameworkElement implements them
    // in terms of Width/Height/Margin/Alignment.
    virtual void Measure(float availW, float availH) = 0;
    virtual void Arrange(const RectDip& finalRect) = 0;
    const SizeDip& Desired() const { return desired_; }

    // Post-Measure hook. Runs after every Measure() that goes through
    // MeasureCached or MeasureWithConstraints, so a subclass can enforce
    // size limits without every Measure override having to remember to.
    //
    // Why this is a hook here rather than a call at the end of each override:
    // FrameworkElement grew MinWidth/MaxWidth/MinHeight/MaxHeight, and the
    // original contract asked all 18 Measure overrides to call the clamp
    // themselves. Every single one of them silently did NOT — the constraints
    // worked only on elements that used the base leaf Measure, which is the
    // one case nobody writes a control for. A contract that is violated by
    // 100% of its call sites on day one is the wrong contract, so the base
    // class now guarantees it. UIElement itself knows nothing about Width or
    // Min/Max (that split is deliberate — see project documentation); it only promises
    // *when* the hook runs. FrameworkElement overrides it to clamp.
    virtual void ApplySizeConstraints() {}

    // Measure short-circuit cache (roadmap §6.2). Panels call this on children
    // instead of Measure() directly: when not Measure-dirty and the constraint
    // matches the last measure, the cached desired size is reused and the
    // (possibly expensive) virtual Measure() is skipped.
    //
    // HISTORY: this briefly carried a parallel set of async_* mirror fields so a
    // background worker could measure without racing the UI thread. Removed after
    // measurement showed it was a net loss — the UI thread blocked on the worker
    // for the full measure cost anyway, and paid ~0.8ms per WM_SIZE to spawn the
    // thread on top. Read docs/design/async-layout-postmortem.md before
    // reintroducing anything shaped like it.
    void MeasureCached(float availW, float availH) {
        if (!NeedsRemeasure() && measureCacheValid_ &&
            availW == lastAvailW_ && availH == lastAvailH_) {
            LayoutCostProbe::Bump(LayoutCountKey::MeasureCacheHits);
            desired_ = cachedDesired_;
            return;
        }
        // Counted here rather than inside Measure(): this is the one place every
        // panel child passes through, so the two branches partition the traversal
        // exactly. Instrumenting the virtual instead would miss the elements the
        // cache short-circuits — i.e. precisely the ones we want to count.
        LayoutCostProbe::Bump(LayoutCountKey::MeasureCalls);
        Measure(availW, availH);
        ApplySizeConstraints();
        cachedDesired_ = desired_;
        lastAvailW_ = availW;
        lastAvailH_ = availH;
        measureCacheValid_ = true;
    }

    // True if this element must re-run Measure regardless of a constraint match.
    // A leaf is dirty when its own Measure bit is set; a Panel is dirty when any
    // descendant is (a child's desired size feeds the panel's own measure).
    virtual bool NeedsRemeasure() const { return Has(Dirty(), DirtyFlags::Measure); }

    // --- Tree traversal ---------------------------------------------------
    // Deepest interactive descendant (or self) at the given point. Leaves return
    // themselves when hit; panels override to recurse topmost-first.
    virtual UIElement* HitTestDeep(float dipX, float dipY) {
        return HitTest(dipX, dipY) ? this : nullptr;
    }
    // Append focusable elements in tab order. Leaves add self; panels recurse.
    virtual void CollectFocusables(std::vector<UIElement*>& out) {
        if (IsFocusable()) out.push_back(this);
    }

    // Append the bounds (window DIPs) of every element in this subtree whose own
    // dirty flags are set (WP-07 §S4). The host unions these into the frame's
    // dirty region for a partial redraw. A leaf adds itself when dirty; a Panel
    // overrides to recurse (a Panel is included when its own flags are set, e.g.
    // a background change, and its dirty descendants are added independently).
    // Only visible elements contribute.
    //
    // CONTRACT: the rect must cover every pixel this element paints. The redraw
    // region is exactly this frame's union of these rects — nothing widens it — so an
    // element that paints OUTSIDE bounds_ must say so by overriding
    // VisualOverflowDip(); this then reports the inflated rect automatically.
    // Under-reporting shows up as a clipped edge, and as leftover pixels once the
    // overflow stops being drawn.
    virtual void CollectDirtyBounds(std::vector<RectDip>& out) {
        if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    }

    // How far outside bounds_ this element paints, in DIPs, on every side. Default 0
    // (the element paints strictly inside its layout bounds).
    //
    // Override for a focus ring stroked outside the bounds, a stroke centered ON the
    // bounds edge (half of it falls outside), an oversized drag thumb — anything the
    // layout rect does not account for. TWO things consume this and they must agree,
    // which is why it is one virtual rather than a hand-rolled rect in each place:
    //   * CollectDirtyBounds — so the redraw region covers the overflow;
    //   * Panel::Render's cull — so an element whose OVERFLOW falls in the dirty
    //     region still gets rendered. The clear wipes the whole dirty region, so an
    //     element culled on bounds alone would lose its overflow to that clear and
    //     never repaint it.
    virtual float VisualOverflowDip() const { return 0.0f; }

    // bounds_ grown by VisualOverflowDip(): every pixel this element may paint.
    RectDip VisualBounds() const { return bounds_.inflated(VisualOverflowDip()); }

    // --- Ancestor viewport clip (for compositor-backed children) -----------
    // A scrolling container arranges children at bounds that can extend past its own
    // viewport, and clips them when it paints. That clip is a D2D clip on the frame's
    // device context, which covers everything D2D draws — but NOT a DComp child visual,
    // whose pixels are composited from its own visual tree and are masked only by the
    // clip the owning control sets on itself. So a composited child (TextArea, TreeView)
    // arranged half-below its container painted straight over whatever sat below the
    // container. Same root cause as the "a D2D transform cannot move a DComp visual"
    // bug: the D2D-side mechanism does not reach the composition side.
    //
    // A container that clips declares it by overriding this hook (ScrollPanel does,
    // returning its bounds). AncestorViewportClip() walks the Parent() chain and
    // intersects every answer — so the clip reaches a composited descendant at ANY
    // depth (e.g. TextArea nested in a Grid inside the ScrollPanel), and nested
    // scrolling containers each tighten the clip. Querying upward instead of pushing
    // downward is deliberate: a push model (parent stamps children during Arrange)
    // silently misses anything past the first generation, which is exactly the bug
    // this replaced.
    virtual bool GetViewportClipForDescendants(RectDip& out) const {
        UNREFERENCED_PARAMETER(out);
        return false;
    }

    // The intersection of every ancestor viewport clip, in WINDOW coordinates (the
    // same space as bounds_). Returns false when no ancestor declares a clip.
    //
    // COORDINATE MATH: bounds_ is window-absolute — verified from diagnostic logs
    // (root DockPanel {0,0,1151,736}, a ScrollPanel under a 48 DIP title bar
    // {0,48,1151,656}, descendants continuing in the same space). Since
    // GetViewportClipForDescendants() returns bounds_, both are already in window
    // space and intersect directly with no transform.
    //
    // WHY THIS RETURNS THE CLIP AND NOT A PRE-CLIPPED BOUNDS RECT: a composited
    // control hands this to ScrollContentHost::SetAncestorClip, which must express
    // the compositor clip RELATIVE TO THE CONTROL'S TRUE POSITION
    // (localTop = clip.y - bounds.y). Folding the clip into the bounds destroys the
    // information needed for that subtraction, and the clip then ends up measured
    // from the wrong origin — which pinned composited content to the title bar and
    // dragged it along while scrolling, in several earlier revisions.
    bool AncestorViewportClip(RectDip& out) const {
        bool found = false;
        for (const Visual* p = Parent(); p; p = p->Parent()) {
            const auto* e = static_cast<const UIElement*>(p);
            RectDip clip;
            if (!e->GetViewportClipForDescendants(clip)) continue;
            if (!found) { out = clip; found = true; continue; }
            const float left = std::max(out.x, clip.x);
            const float top = std::max(out.y, clip.y);
            const float right = std::min(out.right(), clip.right());
            const float bottom = std::min(out.bottom(), clip.bottom());
            out.x = left;
            out.y = top;
            out.w = std::max(0.0f, right - left);
            out.h = std::max(0.0f, bottom - top);
        }
        return found;
    }

    // bounds_ intersected with every ancestor viewport clip, in window coordinates.
    // For a control that positions a compositor visual DIRECTLY from a rect
    // (ProgressBar's sweep) instead of going through ScrollContentHost.
    //
    // Returns an EMPTY rect (w or h == 0) when the control is fully scrolled out of
    // view. Callers MUST test IsEmpty() and hide their visual rather than deriving
    // pixel geometry from an empty rect: computing e.g. `y + (h - barH) / 2` on
    // h == 0 yields a plausible-looking coordinate pinned to the clip edge, which is
    // exactly how the sweep bar ended up parked inside the title bar.
    RectDip WindowClippedBounds() const {
        RectDip result = bounds_;
        for (const Visual* p = Parent(); p; p = p->Parent()) {
            const auto* e = static_cast<const UIElement*>(p);
            RectDip clip;
            if (!e->GetViewportClipForDescendants(clip)) continue;

            const float left = std::max(result.x, clip.x);
            const float top = std::max(result.y, clip.y);
            const float right = std::min(result.right(), clip.right());
            const float bottom = std::min(result.bottom(), clip.bottom());
            result.x = left;
            result.y = top;
            result.w = std::max(0.0f, right - left);
            result.h = std::max(0.0f, bottom - top);
        }
        return result;
    }

    // --- State / enabled --------------------------------------------------
    VisualState State() const { return state_; }
    void SetEnabled(bool e);

    // --- Tooltip ----------------------------------------------------------
    // Hover text shown by the window's TooltipService after a short delay.
    // Empty (the default) means no tooltip.
    void SetTooltip(std::wstring text) { tooltip_ = std::move(text); }
    const std::wstring& Tooltip() const { return tooltip_; }
    bool HasTooltip() const { return !tooltip_.empty(); }

    // --- Context menu (WP-03 follow-up) -----------------------------------
    // The menu shown when this element is right-clicked. On a right-click the host
    // walks the Parent() chain from the hit element and opens the first element's
    // menu it finds (so a child with no menu inherits its container's).
    //
    // Two ownership modes:
    //   1. SetContextMenu(unique_ptr) — this element owns the menu (P1-21 pre-reuse)
    //   2. SetContextMenuRef(raw ptr)  — menu owned elsewhere, shared by multiple
    //                                    elements (P1-21: one MenuFlyout for 3+ elements)
    //
    // If the element is already attached, the owner context is fed immediately;
    // otherwise it is fed on the next attach — no manual wiring in either mode.
    void SetContextMenu(std::unique_ptr<IContextMenu> menu) {
        contextMenu_ = std::move(menu);
        contextMenuRef_ = nullptr;  // owned mode clears any ref
        if (contextMenu_ && attached_) contextMenu_->SetOwnerContext(context_);
    }
    void SetContextMenuRef(IContextMenu* menu) {
        contextMenu_.reset();       // ref mode clears ownership
        contextMenuRef_ = menu;
        if (contextMenuRef_ && attached_) contextMenuRef_->SetOwnerContext(context_);
    }
    IContextMenu* ContextMenu() const {
        return contextMenu_ ? contextMenu_.get() : contextMenuRef_;
    }
    bool HasContextMenu() const {
        return contextMenu_ != nullptr || contextMenuRef_ != nullptr;
    }

    // Called just before this element's context menu opens, with the right-click
    // position in the element's own coordinate space (window DIPs).
    //
    // Exists because MenuItem::enabled is a plain bool COPIED into the flyout by
    // SetItems — so an item's enabled state is whatever it was when the menu was built,
    // not when it is shown. Any menu whose items depend on live state (is there a
    // selection? is the clipboard empty?) must therefore rebuild its item list here.
    // Without this hook such a menu is silently wrong: it renders, it responds, and its
    // greyed-out items are greyed according to conditions that held minutes ago.
    //
    // It is also where an element decides what a right-click does to its own state — a
    // text editor moves the caret only when the click falls OUTSIDE the selection.
    virtual void OnContextMenuOpening(float dipX, float dipY) {
        UNREFERENCED_PARAMETER(dipX);
        UNREFERENCED_PARAMETER(dipY);
    }

    // --- Focus ------------------------------------------------------------
    // Whether this element can receive keyboard focus (Tab / click). Default
    // false; interactive controls opt in via SetFocusable.
    bool IsFocusable() const { return focusable_ && enabled_; }
    bool IsFocused() const { return focused_; }
    // Set by the owning window's focus manager — do not call directly; use the
    // window's SetFocusElement so the previously focused element is cleared.
    void SetFocused(bool f);

    // --- Drag and drop ----------------------------------------------------
    // Install a drop handler. The host (NativeWindowHost) registers ONE COM
    // IDropTarget with the OS for the whole window; on each drag event it
    // hit-tests the tree and routes to the deepest element that has a target
    // installed. Passing nullptr removes the handler.
    //
    // Ownership is shared rather than unique because the natural usage is one
    // handler object serving several elements (a window that accepts the same
    // file drop on its list and its text area).
    void SetDropTarget(std::shared_ptr<IFluentDropTarget> target) {
        dropTarget_ = std::move(target);
    }
    IFluentDropTarget* DropTarget() const { return dropTarget_.get(); }
    bool AcceptsDrop() const { return dropTarget_ != nullptr && enabled_; }

    // The deepest descendant at (dipX, dipY) that accepts a drop, or null.
    // Separate from HitTest because a drop must skip elements that merely happen
    // to be on top without accepting anything — otherwise a decorative Border
    // over a TextArea would swallow every file drop. Panel overrides to recurse.
    virtual UIElement* HitTestDropTarget(float dipX, float dipY);

    // --- Hover state machine (driven by the InputManager) -----------------
    // The InputManager calls these as the pointer enters / leaves this element;
    // they flip the pointerInside_ flag and refresh the visual state (Hover). The
    // per-position move / press / release now arrive through the routed virtuals
    // below (OnPointerMoved / OnPointerPressed / OnPointerReleased).
    void OnPointerEnter();
    void OnPointerLeave();

    // --- Routed input (WP-03, roadmap §9.2) -------------------------------
    // The InputManager drives these. A pointer/key event walks the element chain
    // in two passes: Preview tunnels root->target (a parent can pre-empt), then
    // the main phase bubbles target->root (a child handles first, an unhandled
    // event rises to its parent). A handler sets args.handled to stop the rest of
    // the route. `args.source` is the element currently on the route; `.position`
    // is in window DIPs.
    //
    // OnPointerPressed / OnPointerReleased carry the shared *click gesture* (see
    // the .cpp): a `clickable_` element presses on left-down (capturing the
    // pointer so a drag-off still gets the up), and on up fires the OnClick hook
    // if released inside. A control that needs custom press/drag semantics (Slider,
    // TreeView scrollbar, text selection) overrides them without calling the base.
    virtual void OnPreviewPointerPressed(PointerEventArgs& e) { UNREFERENCED_PARAMETER(e); }
    virtual void OnPointerPressed(PointerEventArgs& e);
    virtual void OnPointerReleased(PointerEventArgs& e);
    virtual void OnPointerMoved(PointerEventArgs& e) { UNREFERENCED_PARAMETER(e); }
    virtual void OnPointerWheelChanged(PointerEventArgs& e) { UNREFERENCED_PARAMETER(e); }
    virtual void OnPreviewKeyDown(KeyEventArgs& e) { UNREFERENCED_PARAMETER(e); }
    virtual void OnKeyDownRouted(KeyEventArgs& e) { UNREFERENCED_PARAMETER(e); }
    virtual void OnTextInput(wchar_t ch) { UNREFERENCED_PARAMETER(ch); }

    // Whether this element participates in hit-testing (roadmap §9.1). A false
    // value makes the pointer pass through to whatever is behind it, even though
    // the element still renders. Enabled state is separate (a disabled element is
    // still hit — it just swallows the event) — both must hold for HitTest to pass.
    bool IsHitTestVisible() const { return hitTestVisible_; }
    void SetHitTestVisible(bool v) { hitTestVisible_ = v; }

    // Mouse cursor while the pointer is over this element. nullptr = window
    // default (arrow). Text controls return an I-beam.
    virtual HCURSOR Cursor() const { return nullptr; }

    // Caret blink: WantsBlink() runs a blink timer while focused; OnBlink()
    // toggles the caret on each tick.
    virtual bool WantsBlink() const { return false; }
    virtual void OnBlink() {}

    // Per-frame animation (e.g. smooth scrolling). WantsAnimationTick() reports
    // whether motion is left; OnAnimationTick advances it. CollectAnimations
    // appends the currently-animating elements so the host ticks only the active
    // set (roadmap §6.1), never the whole tree.
    virtual bool WantsAnimationTick() const { return false; }
    virtual void OnAnimationTick(float dtSec) { UNREFERENCED_PARAMETER(dtSec); }
    virtual void CollectAnimations(std::vector<UIElement*>& out) {
        if (WantsAnimationTick()) out.push_back(this);
    }

    // IME composition. The host forwards these to the focused element. hwnd is
    // the owning window (needed for Imm* calls). Default: not handled.
    virtual void OnImeStartComposition(HWND hwnd) { UNREFERENCED_PARAMETER(hwnd); }
    virtual void OnImeComposition(HWND hwnd, LPARAM flags) { UNREFERENCED_PARAMETER(hwnd); UNREFERENCED_PARAMETER(flags); }
    virtual void OnImeEndComposition(HWND hwnd) { UNREFERENCED_PARAMETER(hwnd); }
    // Caret rectangle in window DIPs (for IME candidate placement). Return false
    // if there is no caret.
    virtual bool CaretRectDip(RectDip& out) const { UNREFERENCED_PARAMETER(out); return false; }

    // --- Tree attachment lifecycle (roadmap §6.2 / §6.3) ------------------
    // The framework calls AttachToContext when this element joins a live tree
    // (host root registered, or panel child added under an attached parent) and
    // DetachFromContext when it leaves. An element reads services via the
    // protected Context() only while attached, and its subscriptions +
    // animation registration are released automatically on detach.
    //
    // Attach order (§6.3): store context -> OnAttachedToTree -> recurse to
    // children. Detach order is the reverse: recurse to children ->
    // OnDetachedFromTree -> release subscriptions + drop from the animation set
    // -> clear context. Re-attaching an already-attached element detaches it from
    // the old context first, so a subtree can be re-parented safely.
    void AttachToContext(const UIContext& ctx);
    void DetachFromContext();
    bool IsAttached() const { return attached_; }

    // Refresh the per-tree DPI service in place for an already attached subtree.
    // UIContext is stored by value on every element, so changing only the host's
    // source context leaves controls reading a stale startup scale. The host calls
    // this before OnDpiChanged so notification handlers and surface draw callbacks
    // observe the new scale consistently.
    void UpdateContextDpi(float dpiScale) {
        context_.dpiScale = dpiScale > 0.0f ? dpiScale : 1.0f;
        UpdateChildrenContextDpi(context_.dpiScale);
    }

    // Refresh the inModalResize flag for an already attached subtree. Called by the
    // host on WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE so controls can skip expensive surface
    // updates while the resize border is held (compositor shows stale correctly-placed
    // pixels, avoiding BeginDraw sync points that dominated worst resize frames).
    // Virtual so composited controls (TextArea/TreeView) can override and force a
    // surface refresh on the falling edge, even when bounds didn't change.
    virtual void UpdateContextModalResize(bool inModalResize) {
        context_.inModalResize = inModalResize;
        UpdateChildrenContextModalResize(inModalResize);
    }

protected:
    // Services for the currently-attached tree. Valid only between
    // OnAttachedToTree and OnDetachedFromTree; reading it while detached returns
    // the empty default. Controls use this instead of caching raw service
    // pointers (roadmap §6.2: no service pointer outlives the attach period).
    const UIContext& Context() const { return context_; }

    // Convenience accessors for the two services controls reach for most often,
    // so a control writes Dwrite() / Window() instead of Context().dwrite etc.
    // Both are null while detached (empty context).
    DWriteContext* Dwrite() const { return context_.dwrite; }
    WindowServices* Window() const { return context_.window; }

    // The current theme snapshot for this element's tree (roadmap §11, WP-05).
    // Reads design tokens (colors / spacing / typography). While attached it
    // returns the host's stable snapshot; while detached (or in a headless test
    // that wired no theme) it returns a shared static default snapshot, so a
    // control's Render never dereferences null. Never returns null.
    const ThemeSnapshot& Theme() const;

    // Attach/detach notifications for derived controls. A control acquires its
    // context-scoped resources / subscriptions in OnAttachedToTree (via
    // AddContextSubscription for anything that must auto-cancel) and may release
    // eager resources in OnDetachedFromTree. Both default to no-ops; the base
    // lifecycle handles subscription and animation cleanup regardless.
    virtual void OnAttachedToTree() {}
    virtual void OnDetachedFromTree() {}

    // Recurse attach/detach into owned children. A leaf has none (default no-op);
    // Panel overrides these to propagate the context down/up the subtree. Kept
    // separate from OnAttachedToTree so a control's own hook and its child
    // propagation are ordered correctly by the base (self first on attach,
    // children first on detach).
    virtual void AttachChildren(const UIContext& ctx) { UNREFERENCED_PARAMETER(ctx); }
    virtual void DetachChildren() {}

    // THE ONE CALL A Measure MAKES to publish its result. A single writer for
    // desired_ rather than 25 controls assigning the field directly: it keeps the
    // write in one place, which is what made removing the async mirror a one-line
    // change here instead of a 25-file edit.
    void SetDesired(const SizeDip& size) { desired_ = size; }
    virtual void UpdateChildrenContextDpi(float dpiScale) {
        UNREFERENCED_PARAMETER(dpiScale);
    }

    // Recurse modal-resize flag to children (same pattern as UpdateChildrenContextDpi).
    // Panels/Borders override to forward to their children; leaf controls use the default.
    virtual void UpdateChildrenContextModalResize(bool inModalResize) {
        UNREFERENCED_PARAMETER(inModalResize);
    }

    // Register a subscription whose lifetime is bound to this attach period: it
    // is released on the next detach (or destruction). Use for host registrations
    // acquired in OnAttachedToTree so they never dangle past the tree lifetime.
    void AddContextSubscription(Subscription sub) {
        contextSubs_.push_back(std::move(sub));
    }


    // Measure invalidation drops the cache (hook from Visual — keeps Visual from
    // needing to know about measure state).
    void OnMeasureInvalidated() override { measureCacheValid_ = false; }

    virtual void OnStateChanged() {}
    virtual void OnBoundsChanged() {}
    virtual void OnFocusChanged() {}
    // Recompute the visual state (Normal/Hover/Pressed/Disabled) from the pointer
    // flags + enabled bit and fire OnStateChanged on a change. Protected so a
    // control driving its own press (Slider drag) can refresh the state after
    // toggling pointerDown_.
    void UpdateState();
    void SetFocusable(bool f) { focusable_ = f; }
    // Opt into the base click gesture (see OnPointerPressed/Released). Off by
    // default so a container (Panel/Border) on the bubble route never presses.
    void SetClickable(bool c) { clickable_ = c; }

    // The single click hook for the routed gesture (WP-03): fired by the base
    // OnPointerReleased when a clickable element is released inside after its own
    // press. `e.position` is the up point in window DIPs (used by hit-region
    // controls like TreeView). The single click hook of the routed input model.
    virtual void OnClickRouted(PointerEventArgs& e) { UNREFERENCED_PARAMETER(e); }

    // The pointer left the element entirely (e.g. clear a scrollbar hover). Called
    // from OnPointerLeave; controls that track internal hot regions override it.
    virtual void OnPointerLeft() {}

    RectDip bounds_;
    bool enabled_ = true;
    bool focusable_ = false;
    bool focused_ = false;
    bool hitTestVisible_ = true;  // false = pointer passes through (still renders)
    bool clickable_ = false;      // opt-in to the base routed click gesture
    bool pointerInside_ = false;
    bool pointerDown_ = false;
    VisualState state_ = VisualState::Normal;
    std::wstring tooltip_;  // hover text; empty = none (read by TooltipService)
    std::shared_ptr<IFluentDropTarget> dropTarget_;  // drag-drop handler; null = none

    SizeDip desired_;  // output of Measure

private:
    // Measure cache (roadmap §6.2). cachedDesired_ is the desired size from the
    // last real Measure at (lastAvailW_, lastAvailH_); measureCacheValid_ gates
    // reuse. Cleared via OnMeasureInvalidated whenever Measure is invalidated.
    SizeDip cachedDesired_;
    float lastAvailW_ = 0.0f;
    float lastAvailH_ = 0.0f;
    bool measureCacheValid_ = false;

    // Tree attachment (roadmap §6.2). context_ is the injected service set while
    // attached (empty default otherwise); attached_ gates the attach hooks so a
    // double attach/detach is a no-op. contextSubs_ owns registrations tied to the
    // current attach period — cleared on detach so nothing outlives the tree.
    UIContext context_;
    bool attached_ = false;
    std::vector<Subscription> contextSubs_;

    // Per-element right-click menu (null = none). Two slots for two ownership modes
    // (P1-21): contextMenu_ is the owned instance (SetContextMenu), contextMenuRef_
    // is a non-owning reference (SetContextMenuRef, shared by multiple elements).
    // Only one is non-null at a time; ContextMenu() returns whichever is set. The
    // owner context is fed on attach (and immediately when set if already attached).
    std::unique_ptr<IContextMenu> contextMenu_;
    IContextMenu* contextMenuRef_ = nullptr;
};

} // namespace fluent
