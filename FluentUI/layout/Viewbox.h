// Viewbox.h — a single-child container that scales its content to fit the available
// space while preserving aspect ratio. WPF's Viewbox.
//
// Viewbox takes whatever space its parent offers (subject to its own Width/Height/
// Min/Max/Align), measures its child at infinite constraint (so the child reports
// its natural size), then applies a uniform scale transform to make the child's
// natural size fit exactly into the Viewbox's final arranged bounds.
//
// Three stretch modes control the scaling behavior:
// - None: no scaling; child is rendered at its natural size (may overflow or underflow)
// - Fill: non-uniform scale; stretches to fill both width and height (aspect ratio changes)
// - Uniform: uniform scale; fits entirely inside bounds (preserves aspect ratio, may letterbox)
// - UniformToFill: uniform scale; covers the entire bounds (preserves aspect ratio, may crop)
//
// Primary use cases: responsive icons, logos, and any vector content that must adapt to
// varying container sizes without distortion. A Button containing a Viewbox wrapping a
// Canvas with vector paths auto-scales the icon when the button resizes.
//
// Implementation notes:
// - Measure: offers the child infinite space (so it reports unconstrained desired size),
//   then reports our own explicit Width/Height or the parent's available space as desired.
// - Arrange: computes the scale factor from the child's natural size and our final bounds,
//   stores it, and arranges the child into a rect scaled by that factor (child sees its
//   natural size during Arrange, so nested layout logic is unaffected).
// - Render: wraps the child's render in a D2D scale transform. The child paints as if at
//   its natural size; D2D scales the output to fit our bounds.
//
// No compositor offload (Viewbox is a pure layout container, not an animation target),
// so the scale is recomputed every frame during a resize — acceptable since the cost is
// O(1) arithmetic, not a layout walk.
#pragma once

#include "../core/FrameworkElement.h"
#include <memory>

namespace fluent {

enum class Stretch {
    None,           // No scaling; child at natural size
    Fill,           // Non-uniform scale to fill both dimensions
    Uniform,        // Uniform scale to fit inside (letterbox if needed)
    UniformToFill   // Uniform scale to cover entirely (crop if needed)
};

class Viewbox : public FrameworkElement {
public:
    Viewbox() = default;

    // Take ownership of the single child; returns a borrowed pointer to configure.
    // Replaces any existing child (the old one is destroyed). If the Viewbox is
    // already attached, the new child is attached to the same context immediately.
    template <typename T>
    T* SetChild(std::unique_ptr<T> child) {
        T* raw = child.get();
        if (content_ && content_->IsAttached()) content_->DetachFromContext();
        if (content_) content_->SetParent(nullptr);
        content_ = std::move(child);
        if (content_) {
            content_->SetParent(this);
            if (IsAttached()) {
                content_->AttachToContext(Context());
                static_cast<Visual*>(content_.get())->OnAncestorVisibilityChanged();
            }
        }
        InvalidateMeasure();
        return raw;
    }
    UIElement* Child() const { return content_.get(); }

    // Stretch mode: how the child is scaled to fit the available space.
    void SetStretch(Stretch s) { SetProperty(stretch_, s, DirtyFlags::Measure); }
    Stretch GetStretch() const { return stretch_; }

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

protected:
    // Propagate the tree context to the single child.
    void AttachChildren(const UIContext& ctx) override {
        if (content_) content_->AttachToContext(ctx);
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
    // Map a point from window DIPs to the child's unscaled coordinate space.
    void ToContentSpace(float& dipX, float& dipY) const;

    std::unique_ptr<FrameworkElement> content_;
    Stretch stretch_ = Stretch::Uniform;
    float scaleX_ = 1.0f;  // Computed during Arrange, applied during Render
    float scaleY_ = 1.0f;
};

} // namespace fluent
