// Border.cpp — see Border.h.

#include "Border.h"
#include <algorithm>

namespace fluent {

void Border::Measure(float availW, float availH) {
    Thickness inset = interiorInset();

    // Child is offered the space left after our inset (clamped to >= 0). Its
    // desired size plus the inset is our own desired size, unless an explicit
    // Width/Height overrides it.
    float childW = 0.0f, childH = 0.0f;
    if (content_ && content_->IsVisible()) {
        float availChildW = std::max(0.0f, availW - inset.horizontal());
        float availChildH = std::max(0.0f, availH - inset.vertical());
        content_->MeasureCached(availChildW, availChildH);
        // Phase 3b: read the field the child just wrote. On the async path the child
        // filled asyncDesired_ and left desired_ holding whatever the UI thread last
        // arranged, so reading Desired() here would compose this border's size from a
        // stale child measurement — a wrong-but-plausible size, which is the failure
        // mode this whole design is built to avoid.
        const SizeDip& childSize =
            content_->Desired();
        childW = childSize.w;
        childH = childSize.h;
    }

    SetDesired({IsAuto(width_) ? childW + inset.horizontal() : width_,
                     IsAuto(height_) ? childH + inset.vertical() : height_});
}

void Border::Arrange(const RectDip& finalRect) {
    SetBounds(finalRect);
    if (!content_ || !content_->IsVisible()) return;

    // The child's available slot is the border box minus the interior inset.
    Thickness inset = interiorInset();
    RectDip slot = {finalRect.x + inset.left, finalRect.y + inset.top,
                    std::max(0.0f, finalRect.w - inset.horizontal()),
                    std::max(0.0f, finalRect.h - inset.vertical())};
    // Use the shared arrange logic so the child's Min/Max/Align are respected.
    // Before this change Border ignored them; now it behaves like Panel does.
    content_->Arrange(FrameworkElement::ComputeArrangeRect(content_.get(), slot));
}

void Border::Render(const DrawingContext& dc) {
    // Paint our own chrome (background fill + border stroke) first, then the child
    // on top (roadmap §11.5 order: Background -> BorderBrush -> content).
    const RectDip& b = bounds_;
    if (b.w > 0.0f && b.h > 0.0f) {
        if (hasBg_ && bg_.a > 0.0f) {
            if (cornerRadius_ > 0.0f) {
                dc.FillRoundedRect(
                    D2D1::RoundedRect(D2D1::RectF(b.x, b.y, b.right(), b.bottom()),
                                      cornerRadius_, cornerRadius_),
                    bg_);
            } else {
                dc.FillRect(D2D1::RectF(b.x, b.y, b.right(), b.bottom()), bg_);
            }
        }
        if (hasBorder_ && borderThickness_ > 0.0f && borderColor_.a > 0.0f) {
            // Inset the stroke by half its width so it stays inside the bounds.
            float h = borderThickness_ * 0.5f;
            D2D1_RECT_F sr = D2D1::RectF(b.x + h, b.y + h, b.right() - h, b.bottom() - h);
            if (cornerRadius_ > 0.0f) {
                dc.DrawRoundedRect(
                    D2D1::RoundedRect(sr, cornerRadius_, cornerRadius_), borderColor_,
                    borderThickness_);
            } else {
                dc.DrawRect(sr, borderColor_, borderThickness_);
            }
        }
    }
    if (content_) content_->RenderWithOpacity(dc);
}

UIElement* Border::HitTestDeep(float dipX, float dipY) {
    // Prefer the child; the Border chrome itself is not an interactive target —
    // a decorative Border (a card, a group frame) must stay transparent to
    // hit-testing so it does not swallow clicks meant for what is behind it.
    if (content_ && content_->IsVisible()) {
        if (UIElement* hit = content_->HitTestDeep(dipX, dipY)) return hit;
    }
    // The one exception: a Border carrying a context menu IS a target, otherwise
    // right-click routing (which walks the hit element's parent chain) never
    // reaches it and the menu can never open. Scoped to HasContextMenu() rather
    // than made unconditional so decorative Borders keep the old behavior.
    if (HasContextMenu() && HitTest(dipX, dipY)) return this;
    return nullptr;
}

UIElement* Border::HitTestDropTarget(float dipX, float dipY) {
    if (content_ && content_->IsVisible()) {
        if (UIElement* hit = content_->HitTestDropTarget(dipX, dipY)) return hit;
    }
    return UIElement::HitTestDropTarget(dipX, dipY);
}

void Border::CollectFocusables(std::vector<UIElement*>& out) {
    if (content_ && content_->IsVisible()) content_->CollectFocusables(out);
}

void Border::CollectDirtyBounds(std::vector<RectDip>& out) {
    if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    if (content_ && content_->IsVisible()) content_->CollectDirtyBounds(out);
}

void Border::CollectAnimations(std::vector<UIElement*>& out) {
    UIElement::CollectAnimations(out);
    if (content_ && content_->IsVisible()) content_->CollectAnimations(out);
}

void Border::OnVisibilityChanged(bool) {
    NotifyChildAncestorVisibilityChanged();
}

void Border::OnAncestorVisibilityChanged() {
    NotifyChildAncestorVisibilityChanged();
}

void Border::NotifyChildAncestorVisibilityChanged() {
    if (content_) content_->OnAncestorVisibilityChanged();
}

void Border::OnThemeChanged() {
    if (content_) content_->OnThemeChanged();
}

void Border::OnDpiChanged(float dpiScale) {
    if (content_) content_->OnDpiChanged(dpiScale);
}

void Border::OnDeviceLost() {
    if (content_) content_->OnDeviceLost();
}

void Border::OnDeviceRestored() {
    if (content_) content_->OnDeviceRestored();
}

bool Border::AnyDirtyInSubtree(DirtyFlags flags) const {
    if (Has(Dirty(), flags)) return true;
    return content_ && content_->AnyDirtyInSubtree(flags);
}

void Border::ClearDirtySubtree() {
    ClearDirty();
    if (content_) content_->ClearDirtySubtree();
}

// Phase 3b async recursion. Border owns a child but is NOT a Panel, so it does not
// inherit Panel's overrides — it has to forward these two itself, exactly as it
// already forwards AttachChildren / ClearDirtySubtree / AnyDirtyInSubtree.

} // namespace fluent
