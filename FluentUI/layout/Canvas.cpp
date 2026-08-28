// Canvas.cpp

#include "Canvas.h"
#include <algorithm>
#include <limits>

namespace fluent {

// Static storage for attached properties.
std::unordered_map<UIElement*, Canvas::Position> Canvas::positions_;
uint64_t Canvas::zIndexGeneration_ = 0;

void Canvas::SetLeft(UIElement* element, float dip) {
    if (!element) return;
    positions_[element].left = dip;
    // Changing an attached property doesn't immediately invalidate — the parent
    // Canvas will read the new value on its next ArrangeOverride. If the caller
    // wants immediate visual update, they invalidate the Canvas manually.
}

void Canvas::SetTop(UIElement* element, float dip) {
    if (!element) return;
    positions_[element].top = dip;
}

void Canvas::SetRight(UIElement* element, float dip) {
    if (!element) return;
    positions_[element].right = dip;
}

void Canvas::SetBottom(UIElement* element, float dip) {
    if (!element) return;
    positions_[element].bottom = dip;
}

void Canvas::SetZIndex(UIElement* element, int z) {
    if (!element) return;
    positions_[element].zIndex = z;
    ++zIndexGeneration_;  // invalidate every Canvas's cached z-order
}

float Canvas::GetLeft(UIElement* element) {
    if (!element) return 0.0f;
    auto it = positions_.find(element);
    return (it != positions_.end()) ? it->second.left : 0.0f;
}

float Canvas::GetTop(UIElement* element) {
    if (!element) return 0.0f;
    auto it = positions_.find(element);
    return (it != positions_.end()) ? it->second.top : 0.0f;
}

float Canvas::GetRight(UIElement* element) {
    if (!element) return std::numeric_limits<float>::quiet_NaN();
    auto it = positions_.find(element);
    return (it != positions_.end()) ? it->second.right : std::numeric_limits<float>::quiet_NaN();
}

float Canvas::GetBottom(UIElement* element) {
    if (!element) return std::numeric_limits<float>::quiet_NaN();
    auto it = positions_.find(element);
    return (it != positions_.end()) ? it->second.bottom : std::numeric_limits<float>::quiet_NaN();
}

int Canvas::GetZIndex(UIElement* element) {
    if (!element) return 0;
    auto it = positions_.find(element);
    return (it != positions_.end()) ? it->second.zIndex : 0;
}

void Canvas::ClearPositions(UIElement* element) {
    if (!element) return;
    positions_.erase(element);
}

Canvas::~Canvas() {
    // Drop this Canvas's children from the static map. Without this, `positions_`
    // grows for the life of the process and — worse — a destroyed child's entry is
    // inherited by whatever the allocator later places at that same address. The
    // failure is silent and looks nothing like a lifetime bug: a fresh child that
    // never called SetRight comes out right-anchored, because it picked up a dead
    // element's Right.
    //
    // `children_` belongs to Panel, a base class, so it is still alive here — base
    // members are destroyed only after the derived destructor body returns.
    //
    // This covers the dominant ownership pattern (the Canvas owns its children via
    // unique_ptr, so they die with it). Two gaps remain, both requiring a deliberate
    // act by the caller, and ClearPositions is public so they can be closed by hand:
    // an element moved out of the Canvas and destroyed separately, and Panel::Clear(),
    // which is not virtual and so cannot be intercepted here.
    for (auto& cptr : children_) {
        if (cptr) positions_.erase(cptr.get());
    }
}

SizeDip Canvas::MeasureOverride(float availW, float availH) {
    // Canvas doesn't constrain children — each measures with infinite constraint.
    constexpr float inf = std::numeric_limits<float>::infinity();

    for (auto& cptr : children_) {
        if (!cptr || !cptr->IsVisible()) continue;
        cptr->MeasureCached(inf, inf);
    }

    // Canvas itself reports (0, 0) unless Width/Height are explicitly set
    // (handled by FrameworkElement).
    return {0.0f, 0.0f};
}

void Canvas::ArrangeOverride(const RectDip& content) {
    for (auto& cptr : children_) {
        if (!cptr || !cptr->IsVisible()) continue;
        FrameworkElement* child = cptr.get();
        float left = GetLeft(child);
        float top = GetTop(child);
        float right = GetRight(child);
        float bottom = GetBottom(child);
        const SizeDip& desired = child->Desired();

        // Compute X: if Right is set (not NaN), position from the right edge.
        float x = content.x;
        if (!std::isnan(right)) {
            x = content.right() - right - desired.w;
        } else {
            x = content.x + left;
        }

        // Compute Y: if Bottom is set (not NaN), position from the bottom edge.
        float y = content.y;
        if (!std::isnan(bottom)) {
            y = content.bottom() - bottom - desired.h;
        } else {
            y = content.y + top;
        }

        RectDip slot{x, y, desired.w, desired.h};
        ArrangeChild(child, slot);
    }
}

void Canvas::EnsureZOrder() const {
    // Rebuild the cached z-order if the generation moved or the child list changed size.
    if (zOrderGeneration_ == zIndexGeneration_ && zOrderChildCount_ == children_.size())
        return;

    zOrder_.clear();
    zOrder_.reserve(children_.size());
    for (auto& cptr : children_) {
        if (!cptr) continue;  // skip null slots (shouldn't exist, defensive)
        zOrder_.push_back(cptr.get());
    }
    std::stable_sort(zOrder_.begin(), zOrder_.end(), [](FrameworkElement* a, FrameworkElement* b) {
        return GetZIndex(a) < GetZIndex(b);
    });
    zOrderGeneration_ = zIndexGeneration_;
    zOrderChildCount_ = children_.size();
}

void Canvas::Render(const DrawingContext& dc) {
    OnRenderBackground(dc);

    EnsureZOrder();
    // Paint in ascending ZIndex order (lower ZIndex first, higher on top).
    // RenderWithOpacity handles visibility — it short-circuits at opacity 0 and
    // skips !visible_ children, so we don't filter here.
    const RectDip& hint = dc.ClipHint();
    for (FrameworkElement* c : zOrder_) {
        const RectDip cb = c->VisualBounds();
        if (!cb.intersects(bounds_)) continue;
        if (!cb.intersects(hint)) continue;
        c->RenderWithOpacity(dc);
    }
}

UIElement* Canvas::HitTestDeep(float dipX, float dipY) {
    EnsureZOrder();
    // Test in descending ZIndex order (highest first, the topmost).
    // Visibility must be checked explicitly here because we're not going through
    // RenderWithOpacity; HitTest on the child itself also checks enabled state.
    for (auto it = zOrder_.rbegin(); it != zOrder_.rend(); ++it) {
        FrameworkElement* c = *it;
        if (!c->IsVisible()) continue;
        if (UIElement* hit = c->HitTestDeep(dipX, dipY)) return hit;
    }
    return nullptr;  // Canvas itself is not an interactive target
}

UIElement* Canvas::HitTestDropTarget(float dipX, float dipY) {
    EnsureZOrder();
    // Drop routing follows the same descending ZIndex order as hit-testing, for the
    // same reason: the visually-topmost child should win.
    for (auto it = zOrder_.rbegin(); it != zOrder_.rend(); ++it) {
        FrameworkElement* c = *it;
        if (!c->IsVisible()) continue;
        if (UIElement* hit = c->HitTestDropTarget(dipX, dipY)) return hit;
    }
    // Fallback: check the panel itself (a whole-Canvas drop zone is rare but valid).
    return UIElement::HitTestDropTarget(dipX, dipY);
}

} // namespace fluent
