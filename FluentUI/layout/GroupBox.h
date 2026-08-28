// GroupBox.h — a titled frame around one child. WPF's GroupBox.
//
// Structure: a header text run, then a bordered box holding the content:
//
//     Settings                 <- header (theme bodyStrong, textSecondary)
//     +--------------------+
//     |  <child content>   |   <- bordered box, content inset by padding
//     +--------------------+
//
// WHY THE HEADER SITS ABOVE THE BOX RATHER THAN ON THE BORDER LINE.
//
// WPF's GroupBox interrupts the top border and places the header text in the gap.
// Reproducing that needs the border segment behind the text erased, which in turn
// needs an opaque colour to erase WITH. This framework composites over Mica, so the
// window background is semi-transparent (see cardFill / ResolveBaseFill in
// project documentation): filling that gap with any concrete colour paints a visibly wrong
// patch over the material. Drawing the border as two separate strokes to leave a
// real hole would need a geometry sink per GroupBox per frame.
//
// Putting the header above the box avoids the problem entirely and matches what
// WinUI does for the same reason (HeaderedContentControl). The trade-off is that a
// GroupBox is slightly taller than its WPF equivalent for the same content.
//
// Like Border, this is a non-interactive single-child decorator, so it derives from
// FrameworkElement rather than Control and forwards the element contract down.
#pragma once

#include "../core/FrameworkElement.h"
#include <memory>
#include <string>

namespace fluent {

class GroupBox : public FrameworkElement {
public:
    GroupBox() = default;

    // Take ownership of the single child; returns a borrowed pointer to configure.
    // Replaces any existing child. Attaches immediately when already attached.
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
        InvalidateDirty(DirtyFlags::Measure);
        return raw;
    }
    FrameworkElement* Child() const { return content_.get(); }

    // The title drawn above the box. Empty (the default) collapses the header row
    // entirely, degenerating this into a plain bordered Border.
    void SetHeader(std::wstring text) {
        header_ = std::move(text);
        InvalidateDirty(DirtyFlags::Measure);
    }
    const std::wstring& Header() const { return header_; }

    // Interior padding between the box border and the child.
    void SetPadding(const Thickness& p) { SetProperty(padding_, p, DirtyFlags::Measure); }
    const Thickness& Padding() const { return padding_; }

    // Border stroke thickness in DIPs. 0 draws no box (header + bare content).
    void SetBorderThickness(float dip) { SetProperty(borderThickness_, dip, DirtyFlags::Measure); }
    float BorderThickness() const { return borderThickness_; }

    // Corner radius of the box, in DIPs. Defaults to the theme's small radius at
    // render time when left at the sentinel -1.
    void SetCornerRadius(float dip) { SetProperty(cornerRadius_, dip, DirtyFlags::Render); }

    // The header row's rect in window DIPs (empty height when the header is empty).
    // Public for the same reason as Expander::HeaderRect: a headless test has no
    // other way to check where the header landed.
    RectDip HeaderRect() const;
    // The bordered box's rect in window DIPs — everything below the header row.
    RectDip BoxRect() const;

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
    // Height of the header row including its bottom gap; 0 when the header is empty.
    float HeaderBandHeight() const;
    // Total inset from the box rect to the child rect (border + padding per edge).
    Thickness interiorInset() const {
        return {borderThickness_ + padding_.left, borderThickness_ + padding_.top,
                borderThickness_ + padding_.right, borderThickness_ + padding_.bottom};
    }

    static constexpr float kHeaderFontSizeDip = 14.0f;

    std::unique_ptr<FrameworkElement> content_;
    std::wstring header_;
    Thickness padding_{12.0f, 12.0f, 12.0f, 12.0f};
    float borderThickness_ = 1.0f;
    float cornerRadius_ = -1.0f;  // -1 = use the theme's small radius
    float headerHeight_ = 0.0f;   // cached from the last Measure
};

} // namespace fluent
