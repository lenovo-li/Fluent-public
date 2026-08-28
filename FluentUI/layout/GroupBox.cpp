// GroupBox.cpp — see GroupBox.h.

#include "GroupBox.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/ResourceCache.h"
#include "../styling/ThemeTokens.h"
#include <algorithm>

namespace fluent {

namespace {
// Gap between the header baseline box and the top of the framed box.
constexpr float kHeaderGapDip = 6.0f;
// Header height used when DWrite is unavailable (headless tests, pre-attach). The
// real height comes from GetMetrics during Measure; this only has to be plausible
// and stable so a headless layout assertion is deterministic.
constexpr float kFallbackHeaderH = 20.0f;
}  // namespace

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

float GroupBox::HeaderBandHeight() const {
    if (header_.empty()) return 0.0f;
    // Cached from the last Measure so HeaderRect/BoxRect/Arrange/Render all agree on
    // one number. Recomputing per caller would let them disagree by a sub-pixel and
    // leave a seam between the header band and the box.
    return headerHeight_ + kHeaderGapDip;
}

RectDip GroupBox::HeaderRect() const {
    if (header_.empty()) return {};
    return {bounds_.x, bounds_.y, bounds_.w, headerHeight_};
}

RectDip GroupBox::BoxRect() const {
    const float band = HeaderBandHeight();
    return {bounds_.x, bounds_.y + band, bounds_.w,
            std::max(0.0f, bounds_.h - band)};
}

// ---------------------------------------------------------------------------
// Measure / Arrange
// ---------------------------------------------------------------------------

void GroupBox::Measure(float availW, float availH) {
    // Header height first — the child's available height depends on it.
    headerHeight_ = 0.0f;
    if (!header_.empty()) {
        headerHeight_ = kFallbackHeaderH;
        if (DWriteContext* dw = Dwrite()) {
            const float layoutW = (availW > 0.0f) ? availW : 1000.0f;
            constexpr float kLayoutH = 1000.0f;

            // Through the shared layout cache (roadmap §13.3) rather than a fresh
            // CreateTextLayout on every Measure. A GroupBox re-measures on every
            // frame of a resize drag (the constraint changes, so MeasureCached
            // cannot short-circuit), and the header string is what determines this
            // height — it almost never changes. Note that layoutW IS the offered
            // width, so a drag that truly varies the width still misses; the win is
            // the steady state and the repeated measures at one constraint.
            ComPtr<IDWriteTextLayout> layout;
            if (ResourceCache* cache = Context().resourceCache) {
                TextLayoutKey key;
                key.text = header_;
                key.fontSize = kHeaderFontSizeDip;
                key.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                key.textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
                key.paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
                key.wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
                key.maxWidth = layoutW;
                key.maxHeight = kLayoutH;
                layout = cache->GetTextLayout(std::move(key));
            } else if (IDWriteTextFormat* fmt = dw->Format(
                           kHeaderFontSizeDip, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                           DWRITE_TEXT_ALIGNMENT_LEADING,
                           DWRITE_PARAGRAPH_ALIGNMENT_NEAR)) {
                // Cache-less context (headless tests): build directly, same inputs.
                if (FAILED(dw->Factory()->CreateTextLayout(
                        header_.c_str(), static_cast<UINT32>(header_.size()), fmt,
                        layoutW, kLayoutH, layout.GetAddressOf())))
                    layout.Reset();
            }
            if (layout) {
                DWRITE_TEXT_METRICS m{};
                if (SUCCEEDED(layout->GetMetrics(&m)) && m.height > 0.0f)
                    headerHeight_ = m.height;
            }
        }
    }

    const float band = HeaderBandHeight();
    const Thickness inset = interiorInset();

    float childW = 0.0f, childH = 0.0f;
    if (content_ && content_->IsVisible()) {
        const float availChildW = std::max(0.0f, availW - inset.horizontal());
        const float availChildH = std::max(0.0f, availH - band - inset.vertical());
        content_->MeasureCached(availChildW, availChildH);
        // Read the field the child just wrote — see the same note in Border::Measure.
        const SizeDip& childSize =
            content_->Desired();
        childW = childSize.w;
        childH = childSize.h;
    }

    SetDesired({IsAuto(width_) ? childW + inset.horizontal() : width_,
                     IsAuto(height_) ? band + childH + inset.vertical() : height_});
}

void GroupBox::Arrange(const RectDip& finalRect) {
    SetBounds(finalRect);
    if (!content_ || !content_->IsVisible()) return;

    const RectDip box = BoxRect();
    const Thickness inset = interiorInset();
    const RectDip slot = {box.x + inset.left, box.y + inset.top,
                          std::max(0.0f, box.w - inset.horizontal()),
                          std::max(0.0f, box.h - inset.vertical())};
    // Shared arrange logic so the child's Min/Max/Align are honoured, matching Border.
    content_->Arrange(FrameworkElement::ComputeArrangeRect(content_.get(), slot));
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void GroupBox::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;
    const float radius =
        (cornerRadius_ < 0.0f) ? th.spacing.cornerRadiusSmall : cornerRadius_;

    // --- Header text ---
    if (!header_.empty()) {
        if (DWriteContext* dw = Dwrite()) {
            if (IDWriteTextFormat* fmt = dw->Format(
                    kHeaderFontSizeDip, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR)) {
                const RectDip hr = HeaderRect();
                dc.DrawText(header_.c_str(), static_cast<UINT32>(header_.size()), fmt,
                            D2D1::RectF(hr.x, hr.y, hr.right(), hr.bottom()),
                            pal.textSecondary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }

    // --- Framed box ---
    const RectDip box = BoxRect();
    if (borderThickness_ > 0.0f && box.w > 0.0f && box.h > 0.0f) {
        // Inset the stroke by half its width so it stays inside the box, same as Border.
        const float h = borderThickness_ * 0.5f;
        const D2D1_RECT_F sr =
            D2D1::RectF(box.x + h, box.y + h, box.right() - h, box.bottom() - h);
        dc.DrawRoundedRect(D2D1::RoundedRect(sr, radius, radius),
                           pal.controlStrokeDefault, borderThickness_);
    }

    if (content_) content_->RenderWithOpacity(dc);
}

// ---------------------------------------------------------------------------
// Hit-testing
// ---------------------------------------------------------------------------

UIElement* GroupBox::HitTestDeep(float dipX, float dipY) {
    // Same policy as Border: the frame itself is decorative and must stay
    // transparent to hit-testing, or it would swallow clicks meant for whatever sits
    // behind it. A GroupBox carrying a context menu is the one exception, since
    // right-click routing walks the hit element's parent chain.
    if (content_ && content_->IsVisible()) {
        if (UIElement* hit = content_->HitTestDeep(dipX, dipY)) return hit;
    }
    if (HasContextMenu() && HitTest(dipX, dipY)) return this;
    return nullptr;
}

UIElement* GroupBox::HitTestDropTarget(float dipX, float dipY) {
    if (content_ && content_->IsVisible()) {
        if (UIElement* hit = content_->HitTestDropTarget(dipX, dipY)) return hit;
    }
    return UIElement::HitTestDropTarget(dipX, dipY);
}

// ---------------------------------------------------------------------------
// Tree forwarding
// ---------------------------------------------------------------------------

void GroupBox::CollectFocusables(std::vector<UIElement*>& out) {
    if (content_ && content_->IsVisible()) content_->CollectFocusables(out);
}

void GroupBox::CollectDirtyBounds(std::vector<RectDip>& out) {
    if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    if (content_ && content_->IsVisible()) content_->CollectDirtyBounds(out);
}

void GroupBox::CollectAnimations(std::vector<UIElement*>& out) {
    UIElement::CollectAnimations(out);
    if (content_ && content_->IsVisible()) content_->CollectAnimations(out);
}

void GroupBox::OnVisibilityChanged(bool) {
    if (content_) content_->OnAncestorVisibilityChanged();
}

void GroupBox::OnAncestorVisibilityChanged() {
    if (content_) content_->OnAncestorVisibilityChanged();
}

void GroupBox::OnThemeChanged() {
    // The header colour and box stroke are both theme tokens, so this control's own
    // pixels change too — not just the child's.
    Invalidate();
    if (content_) content_->OnThemeChanged();
}

void GroupBox::OnDpiChanged(float dpiScale) {
    if (content_) content_->OnDpiChanged(dpiScale);
}

void GroupBox::OnDeviceLost() {
    if (content_) content_->OnDeviceLost();
}

void GroupBox::OnDeviceRestored() {
    if (content_) content_->OnDeviceRestored();
}

bool GroupBox::AnyDirtyInSubtree(DirtyFlags flags) const {
    if (Has(Dirty(), flags)) return true;
    return content_ && content_->AnyDirtyInSubtree(flags);
}

void GroupBox::ClearDirtySubtree() {
    ClearDirty();
    if (content_) content_->ClearDirtySubtree();
}

} // namespace fluent
