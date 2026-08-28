// Panel.cpp

#include "Panel.h"
#include <algorithm>
#include <cmath>
#include <limits>    // std::numeric_limits — unset Max means +infinity
#include <optional>  // Min/Max getters return std::optional<float>

namespace fluent {

void Panel::UpdateLayout(const RectDip& area) {
    Measure(area.w, area.h);
    Arrange(area);
}

void Panel::RemoveAt(size_t index) {
    if (index >= children_.size()) return;

    auto& child = children_[index];
    if (!child) {
        // Slot holds nullptr. Erase it anyway and still invalidate: the child count
        // changed, which is what MeasureOverride reads.
        children_.erase(children_.begin() + index);
        InvalidateDirty(DirtyFlags::Measure);
        return;
    }

    // Detach while the context is still valid, then break the parent link, then erase.
    // This is the order TabControl::RemoveTab and Panel::Clear already use: detaching
    // after the element left the tree would strand its context subscriptions, and the
    // unique_ptr in the vector is what keeps it alive until the erase.
    if (child->IsAttached())
        child->DetachFromContext();
    child->SetParent(nullptr);
    children_.erase(children_.begin() + index);

    // The panel's own desired size is a function of its children, so a removal can
    // shrink it. Measure is the level that matters here — see the caching contract on
    // ChildAt: every list mutation must invalidate Measure or MeasureCached will reuse
    // a desired size computed from the old child set.
    InvalidateDirty(DirtyFlags::Measure);
}

void Panel::AttachChildren(const UIContext& ctx) {
    for (auto& c : children_)
        if (c) c->AttachToContext(ctx);
}

void Panel::DetachChildren() {
    for (auto& c : children_)
        if (c && c->IsAttached()) c->DetachFromContext();
}

void Panel::UpdateChildrenContextDpi(float dpiScale) {
    for (auto& c : children_)
        if (c) c->UpdateContextDpi(dpiScale);
}

void Panel::UpdateChildrenContextModalResize(bool inModalResize) {
    for (auto& c : children_)
        if (c) c->UpdateContextModalResize(inModalResize);
}

void Panel::Measure(float availW, float availH) {
    // MeasureOverride recurses into the children; only the panel's OWN size is
    // written here.
    SizeDip sz = MeasureOverride(availW, availH);
    // Honor an explicit size if one was set on the panel itself.
    if (!IsAuto(width_)) sz.w = width_;
    if (!IsAuto(height_)) sz.h = height_;
    SetDesired(sz);
}

void Panel::Arrange(const RectDip& finalRect) {
    SetBounds(finalRect);
    ArrangeOverride(finalRect);
}

void Panel::ArrangeChild(FrameworkElement* child, const RectDip& slot) {
    if (!child) return;
    // Delegate to the shared logic in FrameworkElement, which handles margin,
    // Min/Max clamping, and alignment. This ensures Border and Panel agree.
    child->Arrange(FrameworkElement::ComputeArrangeRect(child, slot));
}

void Panel::Render(const DrawingContext& dc) {
    OnRenderBackground(dc);
    // WP-07 §S3: viewport culling — skip children outside the dirty/clip hint.
    // This covers two scenarios:
    //   1. Partial redraw: the host narrows ClipHint to the dirty rect union;
    //      children entirely outside it need no render call.
    //   2. StackPanel overflow: children stacked beyond the panel's own height
    //      are still visited by MeasureOverride but produce no visible pixels;
    //      skipping them avoids the Render overhead for those out-of-bounds rows.
    const RectDip& hint = dc.ClipHint();
    for (auto& c : children_) {
        if (!c) continue;
        // Test the child's VISUAL bounds, not its layout bounds: a child that paints
        // outside bounds_ (focus ring, edge-centered stroke) can have that overflow
        // land in the dirty region while its bounds sit outside it. The host clears
        // the whole dirty region, so culling on bounds alone would wipe the overflow
        // and then skip the repaint that would restore it — visible residue.
        const RectDip cb = c->VisualBounds();
        // Two-level cull: outside the panel's own bounds OR outside the dirty hint.
        if (!cb.intersects(bounds_)) continue;
        if (!cb.intersects(hint)) continue;
        // RenderWithOpacity, not Render: it folds the child's Opacity() into the
        // context it hands down (and forwards unchanged at full opacity, the
        // normal case). The panel's OWN opacity is already in `dc` — our caller
        // used RenderWithOpacity on us — so the two multiply naturally.
        c->RenderWithOpacity(dc);
    }
}

UIElement* Panel::HitTestDeep(float dipX, float dipY) {
    // Topmost-first: later children render on top, so test in reverse order.
    // Collapsed children take no input (WPF semantics) and keep the bounds they
    // were last arranged at, so without the visibility check a hidden page would
    // still swallow clicks aimed at whatever replaced it.
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (!*it || !(*it)->IsVisible()) continue;
        if (UIElement* hit = (*it)->HitTestDeep(dipX, dipY)) return hit;
    }
    return nullptr;  // panels themselves are not interactive targets
}

UIElement* Panel::HitTestDropTarget(float dipX, float dipY) {
    // Recurse children topmost-first, same order as HitTestDeep. Collapsed
    // children are skipped for the same reason (UIElement::HitTestDropTarget also
    // checks, but skipping here avoids walking the whole hidden subtree).
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (!*it || !(*it)->IsVisible()) continue;
        if (UIElement* hit = (*it)->HitTestDropTarget(dipX, dipY)) return hit;
    }
    // Check the panel itself as a fallback (panel-level drop targets are rare
    // but legitimate, e.g. a whole drop-zone panel).
    return UIElement::HitTestDropTarget(dipX, dipY);
}

void Panel::CollectFocusables(std::vector<UIElement*>& out) {
    for (auto& c : children_) {
        if (!c || !c->IsVisible()) continue;  // skip collapsed; saves walk + append
        c->CollectFocusables(out);
    }
}

void Panel::CollectDirtyBounds(std::vector<RectDip>& out) {
    // The panel itself contributes its bounds when its own flags are set (e.g. a
    // background/theme change on the panel). Each child is asked independently so
    // a dirty leaf deep in the tree adds only its own (small) rect, not the whole
    // panel — that is what keeps the partial-redraw region tight (WP-07 §S4).
    if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    // A collapsed child may retain dirty flags while hidden, but it produces no
    // pixels until reveal, so keep its stale rect out of the invalidate union.
    for (auto& c : children_) {
        if (!c || !c->IsVisible()) continue;
        c->CollectDirtyBounds(out);
    }
}

void Panel::OnThemeChanged() {
    for (auto& c : children_)
        if (c) c->OnThemeChanged();
}

bool Panel::AnyDirtyInSubtree(DirtyFlags flags) const {
    if (Has(Dirty(), flags)) return true;
    for (auto& c : children_)
        if (c && c->AnyDirtyInSubtree(flags)) return true;
    return false;
}

void Panel::ClearDirtySubtree() {
    ClearDirty();
    for (auto& c : children_)
        if (c) c->ClearDirtySubtree();
}

void Panel::OnDpiChanged(float dpiScale) {
    for (auto& c : children_)
        if (c) c->OnDpiChanged(dpiScale);
}

void Panel::OnDeviceLost() {
    for (auto& c : children_)
        if (c) c->OnDeviceLost();
}

void Panel::OnDeviceRestored() {
    for (auto& c : children_)
        if (c) c->OnDeviceRestored();
}

void Panel::CollectAnimations(std::vector<UIElement*>& out) {
    // The panel ITSELF first, then the children. Chaining to the base is what was
    // missing: this override used to only recurse, so a Panel subclass that
    // animates in its own right was never handed to the host's tick set and its
    // WantsAnimationTick() was never even consulted. Two such subclasses exist —
    // ScrollPanel (smooth-scroll tween + scrollbar fade) and TabControl (content
    // cross-fade) — and the ScrollPanel case is why wheel scrolling did nothing:
    // OnPointerWheelChanged only sets the tween TARGET, so with no tick the live
    // offset never advanced. A resize appeared to fix it because WM_SIZE forces a
    // full OnLayout, which re-arranges children at whatever offset had been set.
    UIElement::CollectAnimations(out);
    // Skip collapsed subtrees. An animation nobody can see still costs a tick per
    // frame, and — worse — a registered ticker keeps the message loop off its
    // INFINITE block, so "idle costs zero" fails for the whole application while
    // any hidden page holds a live tween.
    for (auto& c : children_) {
        if (!c || !c->IsVisible()) continue;
        c->CollectAnimations(out);
    }
}

void Panel::OnVisibilityChanged(bool) {
    // This panel's own visible_ just flipped, so every descendant's EFFECTIVE
    // visibility flipped with it. Recurse.
    NotifyChildrenAncestorVisibilityChanged();
}

void Panel::OnAncestorVisibilityChanged() {
    // An ancestor above us changed; keep carrying it down so the notification
    // reaches composited controls at any depth.
    NotifyChildrenAncestorVisibilityChanged();
}

void Panel::NotifyChildrenAncestorVisibilityChanged() {
    // No bool is passed: each child computes IsEffectivelyVisible() itself. Passing
    // "the ancestor is now visible" would be wrong, because a DIFFERENT ancestor
    // between us and the child (or above us) may still be collapsed — showing a
    // panel inside a hidden page must not un-hide its contents.
    for (auto& c : children_)
        if (c) c->OnAncestorVisibilityChanged();
}

} // namespace fluent
