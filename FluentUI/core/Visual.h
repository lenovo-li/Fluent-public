// Visual.h — the lowest layer of the FluentUI element model (roadmap §5.1).
//
// A Visual owns only what "being a node that draws" needs:
//   * the visual-tree link to its parent (non-owning),
//   * visibility,
//   * the render entry point (OnRender / Render),
//   * dirty-flag bookkeeping + the frame-request (invalidation) plumbing,
//   * theme / DPI change notifications.
//
// It deliberately knows NOTHING about size, margin, hit-testing, input, focus,
// or layout — those belong to UIElement and above.
//
// Geometry elsewhere is in DIPs. Coordinates and layout live in UIElement; a bare
// Visual is only a drawable, invalidatable tree node.
#pragma once

#include "../fl_common.h"
#include "Invalidation.h"
#include "../graphics/DrawingContext.h"
#include <vector>

namespace fluent {

class Visual {
public:
    virtual ~Visual() = default;

    // --- Visibility -------------------------------------------------------
    // WPF's Visibility.Collapsed: an invisible element is skipped by Measure and
    // Arrange (so it occupies no layout space), by RenderWithOpacity (so it paints
    // nothing) and by HitTest (so it takes no input).
    bool IsVisible() const { return visible_; }
    // Walks the ancestor chain: false if THIS element or ANY ancestor is
    // collapsed. IsVisible() alone is not enough for a composited control,
    // because SetVisible(false) on a containing panel (e.g. a Gallery page)
    // does not touch this element's own visible_ — only the panel's. A
    // TextArea nested three levels inside a hidden page still reports
    // IsVisible()==true; this is the check that actually matches "will this
    // element ever produce a visible pixel".
    bool IsEffectivelyVisible() const {
        for (const Visual* v = this; v; v = v->parent_)
            if (!v->visible_) return false;
        return true;
    }
    void SetVisible(bool v) {
        // Measure, not Render: collapsing changes what space the parent hands out,
        // so every sibling after this one may move.
        //
        // The hook matters for composited controls (TextArea / TreeView in
        // composition mode). Their pixels come from a DComp visual that the
        // compositor draws on its own thread — skipping their D2D Render does NOT
        // hide them, so they must push the state down to the compositor themselves.
        if (SetProperty(visible_, v, DirtyFlags::Measure)) OnVisibilityChanged(v);
    }

    // --- Opacity ----------------------------------------------------------
    // Uniform transparency for this visual AND its subtree, in [0..1]. 1 (the
    // default) is fully opaque and costs nothing — the multiplier is skipped
    // entirely, so an app that never touches opacity draws exactly the pixels it
    // did before.
    //
    // Opacity affects only APPEARANCE: layout, hit-testing and focus are
    // unchanged, so a 0-opacity element still occupies its slot and still responds
    // to the mouse (same as WPF/WinUI; use SetVisible(false) to remove it from
    // layout). Nested opacities multiply.
    //
    // Applied by RenderWithOpacity via DrawingContext::WithOpacity, so every
    // control honors it without implementing anything. A control that renders
    // through a composition visual instead (TreeView / TextArea in composited
    // mode) forwards it to ICompositionVisual::SetOpacity, which gives true
    // subtree-composite semantics on the compositor thread.
    float Opacity() const { return opacity_; }
    void SetOpacity(float o) {
        const float v = (o < 0.0f) ? 0.0f : (o > 1.0f ? 1.0f : o);
        // Render-only: opacity never changes desired size or position. The
        // element must still report a dirty rect, which SetProperty's
        // InvalidateDirty(Render) does.
        if (SetProperty(opacity_, v, DirtyFlags::Render)) OnOpacityChanged(v);
    }

    // This visual's opacity multiplied by every ancestor's. Needed only by an
    // element that does NOT draw through its parent's DrawingContext — a
    // composited control paints into its own composition surface, so the ancestor
    // opacity folded into that context never reaches it and must be recomposed
    // here before being pushed onto the composition visual.
    float EffectiveOpacity() const {
        float o = opacity_;
        for (const Visual* p = parent_; p; p = p->parent_) o *= p->opacity_;
        return o;
    }

    // --- Visual tree ------------------------------------------------------
    // Non-owning link to the containing visual (set by a Panel when a child is
    // added, cleared on removal). Used to propagate invalidation toward the root
    // so a nested element repaints without wiring its own callback. The parent
    // owns the child (unique_ptr), so this pointer is valid for the child's life.
    void SetParent(Visual* parent) { parent_ = parent; }
    Visual* Parent() const { return parent_; }

    // --- Render -----------------------------------------------------------
    // Paint into the frame's device context using the shared render resources
    // (roadmap §5.3.1 / §13.2). The DrawingContext wraps the frame device
    // context + the shared per-frame brush; controls call its typed, color-taking
    // methods (dc.FillRoundedRect(rr, color), dc.DrawText(...)) rather than
    // touching a raw brush. dc.DpiScale() = dpi/96.
    virtual void Render(const DrawingContext& dc) = 0;

    // Render this visual honoring Opacity(). This is what a CONTAINER calls on its
    // children (Panel::Render, the window host's element loop) rather than
    // Render() directly: at full opacity it forwards unchanged (zero cost), and
    // otherwise it hands down a context whose alpha multiplier is folded in.
    //
    // Kept separate from Render() on purpose: a control's own Render override must
    // not have to remember to apply its opacity, and a control that draws its
    // children by hand can opt into the same behavior with one call.
    void RenderWithOpacity(const DrawingContext& dc) {
        // Collapsed: draw nothing. This is the ONE place the check belongs, because
        // it is the choke point every container renders children through — putting it
        // in each Panel subclass instead would need the same line in StackPanel, Grid,
        // WrapPanel, DockPanel, Canvas, ScrollPanel and Border, and any container
        // added later would silently paint hidden subtrees.
        //
        // The layout passes skip invisible children, so a hidden element KEEPS the
        // bounds it was last arranged at. Panel::Render culls on those bounds, which
        // still intersect the viewport — so without this, hiding a page left it
        // painting on top of whatever replaced it.
        if (!visible_) return;
        if (opacity_ >= 1.0f) {
            Render(dc);
            return;
        }
        // Fully transparent: nothing to draw. The host has already cleared the
        // dirty region, so skipping is what makes the element disappear.
        if (opacity_ <= 0.0f) return;
        Render(dc.WithOpacity(opacity_));
    }

    // Hook: called when Opacity() actually changed. Default no-op; a control that
    // mirrors opacity onto a composition visual overrides this.
    virtual void OnOpacityChanged(float opacity) { UNREFERENCED_PARAMETER(opacity); }

    // Hook: called when IsVisible() actually changed. Default no-op; a control whose
    // pixels come from a composition visual must override this and mirror the state
    // onto the compositor, because the compositor keeps drawing that visual whether
    // or not this element's Render is called.
    virtual void OnVisibilityChanged(bool visible) { UNREFERENCED_PARAMETER(visible); }

    // Hook: called when an ANCESTOR's visibility changed (this element's own
    // visible_ did not). Default no-op. Panel overrides both this and
    // OnVisibilityChanged to recurse into children, so a composited control
    // anywhere in the subtree is reachable regardless of how deep it is nested —
    // it reads IsEffectivelyVisible() to learn its actual state rather than being
    // handed a bool, because a sibling ancestor could independently be collapsed
    // too. This is what makes SetVisible(false) on a container page actually
    // detach the DComp visuals of TextArea/TreeView living inside it, instead of
    // only affecting the page's own D2D Render (which composited controls skip).
    virtual void OnAncestorVisibilityChanged() {}

    // Notifications from the host. Default no-ops; elements that cache
    // theme-dependent or DPI-dependent GPU resources override these to refresh.
    virtual void OnThemeChanged() {}
    virtual void OnDpiChanged(float dpiScale) { UNREFERENCED_PARAMETER(dpiScale); }

    // Device-loss notifications (roadmap §17). The host calls OnDeviceLost when
    // the GPU device drops (so an element can release any device-bound resource it
    // holds) and OnDeviceRestored after the device stack is rebuilt (to reacquire
    // / re-dirty). Default no-ops: no control holds a device resource of its own
    // today (all cached resources live in the host-owned ResourceCache, which the
    // host clears directly), so these exist for panels to propagate and for future
    // controls that cache a device resource. Panels override to recurse.
    virtual void OnDeviceLost() {}
    virtual void OnDeviceRestored() {}

    // --- Invalidation callback -------------------------------------------
    // Set by the owner; called when the visual wants a repaint. Setting it on the
    // tree root is enough: descendants without their own callback delegate the
    // request up through Parent() until they reach one that has it.
    void SetInvalidateCallback(void (*cb)(void*), void* ctx) {
        invalidateCb_ = cb;
        invalidateCtx_ = ctx;
    }

    // --- Dirty flags ------------------------------------------------------
    // Accumulated dirty flags for this visual (set by Invalidate* calls, read and
    // cleared by the host during a frame). Public so the window host can
    // query/clear them; elements use the Invalidate* helpers below.
    DirtyFlags Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = DirtyFlags::None; }

    // True if this visual (or, for panels, any descendant) has `flags` set. The
    // host calls this before a frame: if any Measure bit is set anywhere in the
    // tree it re-runs layout before painting. Leaves test only themselves; Panel
    // overrides to recurse into children.
    virtual bool AnyDirtyInSubtree(DirtyFlags flags) const {
        return Has(dirty_, flags);
    }
    // Clear the accumulated dirty flags on this visual and (for panels) all
    // descendants. Called by the host after it has serviced a frame.
    virtual void ClearDirtySubtree() { ClearDirty(); }

protected:
    // Mark this visual dirty and request a frame. The flags are expanded to their
    // closure (Measure -> Arrange -> Render) and accumulated here; the request
    // then propagates toward the root. Invalidate() defaults to Render-only.
    void Invalidate() { InvalidateDirty(DirtyFlags::Render); }
    // The desired size may have changed (text/font/items): re-measure.
    void InvalidateMeasure() { InvalidateDirty(DirtyFlags::Measure); }
    // Position within the slot changed but not the desired size: re-arrange.
    void InvalidateArrange() { InvalidateDirty(DirtyFlags::Arrange); }
    // Record dirty flags and trigger the repaint/relayout request up the tree.
    void InvalidateDirty(DirtyFlags flags);

    // Assign a property and invalidate exactly what it dirtied, in one place
    // (roadmap §7.1). Returns false (no-op) when unchanged, so a redundant set
    // never schedules a frame; otherwise stores the value, records `flags`
    // (expanded by InvalidateDirty) and returns true.
    template <class T>
    bool SetProperty(T& storage, T value, DirtyFlags flags) {
        if (storage == value) return false;
        storage = std::move(value);
        InvalidateDirty(flags);
        return true;
    }

    // Hook: called by InvalidateDirty when a Measure invalidation is recorded, so
    // a subclass that caches measure results can drop them. Visual itself keeps no
    // measure state, so this is where UIElement clears its measure cache without
    // Visual having to know about it (keeps the layer boundary clean).
    virtual void OnMeasureInvalidated() {}

private:
    bool visible_ = true;
    float opacity_ = 1.0f;      // [0..1]; 1 = opaque (multiplier skipped entirely)
    Visual* parent_ = nullptr;  // non-owning; set by Panel::Add
    DirtyFlags dirty_ = DirtyFlags::None;  // accumulated between frames
    void (*invalidateCb_)(void*) = nullptr;
    void* invalidateCtx_ = nullptr;
};

} // namespace fluent
