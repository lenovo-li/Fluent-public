// Border.h — a single-child decorator that draws a background fill, an optional
// stroked border, and rounded corners, then lays its child out inside its padding
// (roadmap §5.5 / the layout primitives list). WPF's Border.
//
// Border owns exactly one child (unique_ptr) and forwards the whole element
// contract to it: attach/detach, render (after painting its own chrome), hit-test,
// focus/animation collection, theme/DPI, measure/arrange (child measured against
// the space left after padding + border, then arranged into the padded interior).
//
// It is a pure layout decorator with no interactive chrome, so it derives from
// FrameworkElement (not Control) and lives in a Panel's
// `vector<unique_ptr<FrameworkElement>>`.
#pragma once

#include "../core/FrameworkElement.h"
#include <memory>

namespace fluent {

class Border : public FrameworkElement {
public:
    Border() = default;

    // Take ownership of the single child; returns a borrowed pointer to configure.
    // Replaces any existing child (the old one is destroyed). If the Border is
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

    // --- Chrome -----------------------------------------------------------
    // Background fill. Unset (the default) paints no fill (transparent). A set
    // value with alpha 0 also paints nothing.
    // Colors are D2D1_COLOR_F (no operator==), so they can't go through
    // SetProperty's change-detection; set directly + Render invalidate.
    void SetBackground(const D2D1_COLOR_F& c) { hasBg_ = true; bg_ = c; Invalidate(); }
    void ClearBackground() { if (hasBg_) { hasBg_ = false; Invalidate(); } }
    // Border stroke color + thickness (DIPs, uniform). Thickness 0 = no stroke.
    void SetBorderColor(const D2D1_COLOR_F& c) { hasBorder_ = true; borderColor_ = c; Invalidate(); }
    void SetBorderThickness(float dip) { SetProperty(borderThickness_, dip, DirtyFlags::Measure); }
    // Corner radius in DIPs for both fill and stroke (0 = square corners).
    void SetCornerRadius(float dip) { SetProperty(cornerRadius_, dip, DirtyFlags::Render); }
    // Interior padding between the border and the child.
    void SetPadding(const Thickness& p) { SetProperty(padding_, p, DirtyFlags::Measure); }
    const Thickness& Padding() const { return padding_; }

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
    // Propagate the tree context to the single child (roadmap §6.3).
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
    void NotifyChildAncestorVisibilityChanged();
    // Total inset (border thickness + padding) subtracted from the border box to
    // get the child's content rect on each edge.
    Thickness interiorInset() const {
        return {borderThickness_ + padding_.left, borderThickness_ + padding_.top,
                borderThickness_ + padding_.right, borderThickness_ + padding_.bottom};
    }

    std::unique_ptr<FrameworkElement> content_;
    D2D1_COLOR_F bg_{};
    D2D1_COLOR_F borderColor_{};
    bool hasBg_ = false;
    bool hasBorder_ = false;
    float borderThickness_ = 0.0f;
    float cornerRadius_ = 0.0f;
    Thickness padding_;
};

} // namespace fluent
