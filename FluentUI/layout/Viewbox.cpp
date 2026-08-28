// Viewbox.cpp — see Viewbox.h.

#include "Viewbox.h"
#include "../graphics/DrawingContext.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
// The constraint the child is measured under. Not std::numeric_limits<float>::
// infinity(): a child that stretches (HAlign::Stretch, or a Panel sizing to its
// slot) would return infinity as its own desired size, and every scale computed
// from it would be zero. A large finite constraint gives such a child a definite
// size to report while still letting a natural-sized child (an Image, a TextBlock)
// report the same number it would under infinity.
constexpr float kNaturalConstraint = 100000.0f;

// Below this the child has no usable natural size and no scale is meaningful.
constexpr float kMinContentDip = 0.01f;
} // namespace

void Viewbox::Measure(float availW, float availH) {
    // The child is measured against a near-unbounded constraint so it reports its
    // NATURAL size — the number the scale is computed from. This is what separates
    // a Viewbox from a Border: a Border passes its own constraint through, so its
    // child adapts to the container; a Viewbox wants a child that does not adapt,
    // and scales it instead.
    if (content_ && content_->IsVisible())
        content_->MeasureCached(kNaturalConstraint, kNaturalConstraint);

    // Our own desired size is the space offered, so a Viewbox fills its slot by
    // default and there is something to scale INTO. Reporting the child's natural
    // size here instead would make the parent hand us exactly that size back, the
    // scale would always be 1.0, and the control would do nothing.
    //
    // An unbounded available axis has no "fill" to speak of, so fall back to the
    // child's natural extent on that axis — otherwise the Viewbox would report
    // 100000 DIP to a StackPanel and push everything else off screen.
    const SizeDip natural = (content_ && content_->IsVisible()) ? content_->Desired()
                                                                : SizeDip{};
    const bool unboundedW = !(availW < kNaturalConstraint);
    const bool unboundedH = !(availH < kNaturalConstraint);
    SetDesired({IsAuto(width_)  ? (unboundedW ? natural.w : availW) : width_,
                IsAuto(height_) ? (unboundedH ? natural.h : availH) : height_});
}

void Viewbox::Arrange(const RectDip& finalRect) {
    SetBounds(finalRect);
    scaleX_ = scaleY_ = 1.0f;
    if (!content_ || !content_->IsVisible()) return;

    const SizeDip natural = content_->Desired();

    if (stretch_ != Stretch::None &&
        natural.w > kMinContentDip && natural.h > kMinContentDip &&
        finalRect.w > 0.0f && finalRect.h > 0.0f) {
        const float fitW = finalRect.w / natural.w;
        const float fitH = finalRect.h / natural.h;
        switch (stretch_) {
            case Stretch::Fill:
                scaleX_ = fitW;
                scaleY_ = fitH;
                break;
            case Stretch::Uniform:
                scaleX_ = scaleY_ = std::min(fitW, fitH);
                break;
            case Stretch::UniformToFill:
                scaleX_ = scaleY_ = std::max(fitW, fitH);
                break;
            case Stretch::None:
                break;  // unreachable, guarded above
        }
    }

    // The child is arranged AT ITS NATURAL SIZE. Every coordinate it computes —
    // its own bounds, its children's slots, a caret position — is in unscaled
    // space, and the scale is applied once at paint time by a transform. Arranging
    // it into the scaled rect instead would make the child re-lay-out for the new
    // size (text would re-wrap, a Grid would re-proportion), which is the opposite
    // of what a Viewbox means: the content is supposed to be magnified, not reflowed.
    //
    // The child's origin is placed so that its SCALED box is centered in our bounds.
    // Solving for the pre-scale origin: scaled_origin = bounds + (bounds - scaled)/2,
    // and the transform in Render maps bounds_-relative coordinates through the
    // scale, so the child's unscaled origin is that offset divided by the scale.
    const float scaledW = natural.w * scaleX_;
    const float scaledH = natural.h * scaleY_;
    const float padX = (finalRect.w - scaledW) * 0.5f;
    const float padY = (finalRect.h - scaledH) * 0.5f;
    content_->Arrange({finalRect.x + (scaleX_ != 0.0f ? padX / scaleX_ : 0.0f),
                       finalRect.y + (scaleY_ != 0.0f ? padY / scaleY_ : 0.0f),
                       natural.w, natural.h});
}

void Viewbox::Render(const DrawingContext& dc) {
    if (!content_ || !content_->IsVisible()) return;

    // Scale 1.0 (Stretch::None, or a child that already fits exactly) takes the
    // plain path: no transform push, no clip, nothing for the GPU to resample.
    if (scaleX_ == 1.0f && scaleY_ == 1.0f) {
        content_->RenderWithOpacity(dc);
        return;
    }

    // Scale about bounds_'s origin, so the child's arranged coordinates (which are
    // already expressed relative to that origin) land where Arrange intended.
    // TransformGuard PRE-MULTIPLIES onto whatever transform the frame already has
    // (the DPI scale, and a DComp surface's tile offset) and restores it on scope
    // exit — never SetTransform, per the surface-atlas rule in project documentation.
    const D2D1_MATRIX_3X2_F m =
        D2D1::Matrix3x2F::Translation(-bounds_.x, -bounds_.y) *
        D2D1::Matrix3x2F::Scale(scaleX_, scaleY_) *
        D2D1::Matrix3x2F::Translation(bounds_.x, bounds_.y);
    TransformGuard xform(dc.Dc(), m);

    // UniformToFill scales past at least one edge by construction, so the overflow
    // has to be clipped or it paints over the Viewbox's siblings. Clipping is
    // applied under the transform, so the rect is expressed in the child's
    // pre-scale space: the inverse-mapped bounds.
    if (stretch_ == Stretch::UniformToFill && scaleX_ != 0.0f && scaleY_ != 0.0f) {
        ClipGuard clip(dc.Dc(),
                       D2D1::RectF(bounds_.x, bounds_.y,
                                   bounds_.x + bounds_.w / scaleX_,
                                   bounds_.y + bounds_.h / scaleY_));
        content_->RenderWithOpacity(dc);
        return;
    }
    content_->RenderWithOpacity(dc);
}

// Hit-testing has to undo what Render's transform did: the pointer arrives in
// window DIPs, the child's bounds are in unscaled space. Both hit-test entry
// points map through the same inverse so they cannot disagree.
void Viewbox::ToContentSpace(float& dipX, float& dipY) const {
    if (scaleX_ != 0.0f) dipX = bounds_.x + (dipX - bounds_.x) / scaleX_;
    if (scaleY_ != 0.0f) dipY = bounds_.y + (dipY - bounds_.y) / scaleY_;
}

UIElement* Viewbox::HitTestDeep(float dipX, float dipY) {
    // A point outside the Viewbox itself must not reach the child: under
    // UniformToFill the child's unscaled box extends past bounds_, so the inverse
    // map alone would happily report a hit on content that is clipped away.
    if (!bounds_.contains(dipX, dipY)) return nullptr;
    if (content_ && content_->IsVisible()) {
        float x = dipX, y = dipY;
        ToContentSpace(x, y);
        if (UIElement* hit = content_->HitTestDeep(x, y)) return hit;
    }
    // Same exception Border makes: a Viewbox carrying a context menu is itself a
    // target, otherwise right-click routing can never reach it.
    if (HasContextMenu() && HitTest(dipX, dipY)) return this;
    return nullptr;
}

UIElement* Viewbox::HitTestDropTarget(float dipX, float dipY) {
    if (bounds_.contains(dipX, dipY) && content_ && content_->IsVisible()) {
        float x = dipX, y = dipY;
        ToContentSpace(x, y);
        if (UIElement* hit = content_->HitTestDropTarget(x, y)) return hit;
    }
    return UIElement::HitTestDropTarget(dipX, dipY);
}

void Viewbox::CollectFocusables(std::vector<UIElement*>& out) {
    if (content_ && content_->IsVisible()) content_->CollectFocusables(out);
}

void Viewbox::CollectDirtyBounds(std::vector<RectDip>& out) {
    if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    // The child reports its dirty rects in UNSCALED space, which is not where the
    // pixels are. Reporting them unmapped would clear the wrong region and leave
    // ghosting — the exact defect class VisualOverflowDip exists to prevent. Map
    // each one forward through the scale, then widen by a pixel: the scaled edge
    // rarely lands on a pixel boundary and PlanRedraw's antialiased clip needs the
    // slack (same reasoning as the snap in PlanRedraw itself).
    if (!content_ || !content_->IsVisible()) return;
    if (scaleX_ == 1.0f && scaleY_ == 1.0f) {
        content_->CollectDirtyBounds(out);
        return;
    }
    const size_t first = out.size();
    content_->CollectDirtyBounds(out);
    for (size_t i = first; i < out.size(); ++i) {
        RectDip& r = out[i];
        r = {bounds_.x + (r.x - bounds_.x) * scaleX_,
             bounds_.y + (r.y - bounds_.y) * scaleY_,
             r.w * scaleX_, r.h * scaleY_};
        r = r.inflated(1.0f);
    }
}

void Viewbox::CollectAnimations(std::vector<UIElement*>& out) {
    UIElement::CollectAnimations(out);
    if (content_ && content_->IsVisible()) content_->CollectAnimations(out);
}

void Viewbox::OnVisibilityChanged(bool) {
    if (content_) content_->OnAncestorVisibilityChanged();
}

void Viewbox::OnAncestorVisibilityChanged() {
    if (content_) content_->OnAncestorVisibilityChanged();
}

void Viewbox::OnThemeChanged() {
    if (content_) content_->OnThemeChanged();
}

void Viewbox::OnDpiChanged(float dpiScale) {
    if (content_) content_->OnDpiChanged(dpiScale);
}

void Viewbox::OnDeviceLost() {
    if (content_) content_->OnDeviceLost();
}

void Viewbox::OnDeviceRestored() {
    if (content_) content_->OnDeviceRestored();
}

bool Viewbox::AnyDirtyInSubtree(DirtyFlags flags) const {
    if (Has(Dirty(), flags)) return true;
    return content_ && content_->AnyDirtyInSubtree(flags);
}

void Viewbox::ClearDirtySubtree() {
    ClearDirty();
    if (content_) content_->ClearDirtySubtree();
}

} // namespace fluent
