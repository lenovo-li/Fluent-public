// ScrollPanel.cpp
#include "ScrollPanel.h"
#include "../graphics/DrawingContext.h"
#include "../core/UIContext.h"
#include "../input/InputManager.h"
#include "../styling/ThemeTokens.h"
#include <d2d1_1.h>
#include <algorithm>

namespace fluent {

SizeDip ScrollPanel::MeasureOverride(float availW, float availH)
{
    // Measure all children with unbounded height (they can report their natural size).
    float totalH = 0.0f;
    float maxW = 0.0f;
    bool first = true;

    // Children see the width minus horizontal padding. Infinity minus a finite
    // padding stays infinity (IEEE), so an unconstrained measure stays unconstrained.
    const float childAvailW = std::max(0.0f, availW - padding_.horizontal());
    // Remember the width constraint: Arrange needs it to tell "the child genuinely
    // wants more than the viewport" (desired > constraint — e.g. a fixed-column Grid)
    // from "the child was stretched BY the measurement" (desired == constraint).
    measureConstraintW_ = childAvailW;


    for (auto& child : children_) {
        if (!child || !child->IsVisible()) continue;
        const Thickness& m = child->Margin();
        child->MeasureCached(std::max(0.0f, childAvailW - m.horizontal()),
                             std::numeric_limits<float>::infinity());
        const auto& desired = child->Desired();
        maxW = std::max(maxW, desired.w + m.horizontal());
        if (!first)
            totalH += spacing_;
        totalH += desired.h + m.vertical();
        first = false;
    }

    contentHeight_ = totalH;
    // The horizontal extent includes the padding, so a wide child can scroll until
    // its right edge AND the trailing padding are both visible.
    scroll_.SetContentWidth(maxW + padding_.horizontal());
    UpdateScrollExtent();

    // Report our desired size: width = max of children (or availW if constrained),
    // height = min(contentHeight, availH) so we don't request more than the parent
    // offers. Padding counts toward both.
    const float totalW = maxW + padding_.horizontal();
    const float totalHPad = contentHeight_ + padding_.vertical();
    return SizeDip{
        std::isfinite(availW) ? availW : totalW,
        std::isfinite(availH) ? std::min(totalHPad, availH) : totalHPad
    };
}

void ScrollPanel::ArrangeOverride(const RectDip& content)
{
    // Reserve a strip on the right for the vertical rail so a child with its own
    // scrollbar (ListBox, TreeView) does not draw its rail directly under ours.
    // When the content fits (no scroll) the strip is not needed.
    const float vRail = (scroll_.MaxOffset() > 0.0f) ? 16.0f : 0.0f;
    const float fitW = std::max(0.0f, content.w - padding_.horizontal() - vRail);

    // Arrange children in a vertical stack, APPLYING both scroll offsets to their
    // bounds. This moves compositor-backed children (TextArea, TreeView) along with
    // ordinary controls — a D2D transform in Render would not affect DComp visuals.
    const float offsetY = scroll_.Offset();
    const float offsetX = scroll_.OffsetX();
    arrangedOffset_ = offsetY;    // record for SyncScrollArrange comparison
    arrangedOffsetX_ = offsetX;
    float y = content.y + padding_.top - offsetY;
    const float childX = content.x + padding_.left - offsetX;
    for (auto& childPtr : children_) {
        FrameworkElement* child = childPtr.get();
        if (!child || !child->IsVisible()) continue;
        const auto& desired = child->Desired();
        const Thickness& m = child->Margin();
        // Width: a child measured WIDER than the constraint it was given has a
        // genuine natural width (a fixed-column Grid of Calendars in a narrow
        // window) — keep it and let the horizontal rail scroll. Everyone else was
        // merely stretched by the measurement to exactly that constraint and can be
        // re-fit to the rail-inset width without losing any content.
        const float childW = (desired.w > measureConstraintW_ + 0.5f)
                                 ? desired.w + m.horizontal()
                                 : fitW;
        // ArrangeChild honors Margin/HAlign/VAlign — the per-control docking the
        // framework already exposes. The slot spans the child's full band including
        // its margin; the child aligns inside the margin-inset remainder.
        ArrangeChild(child, RectDip{childX, y, childW, desired.h + m.vertical()});
        y += desired.h + m.vertical() + spacing_;
    }
}

void ScrollPanel::OnBoundsChanged()
{
    LayoutCostProbe::Scope probe(LayoutCostKey::ScrollPanelBoundsChanged);
    scroll_.SetBounds(bounds_);
    UpdateScrollExtent();
}

void ScrollPanel::UpdateScrollExtent()
{
    // While the horizontal rail is live it covers the bottom 16 DIP of content;
    // inflate the vertical extent by that strip so the last child can scroll fully
    // above the rail instead of resting half-covered beneath it. Vertical padding
    // also counts, so the trailing gap scrolls into view too.
    const float hRail = (scroll_.MaxOffsetX() > 0.0f) ? 16.0f : 0.0f;
    scroll_.SetContentHeight(contentHeight_ + padding_.vertical() + hRail);
}

void ScrollPanel::OnPointerWheelChanged(PointerEventArgs& e)
{
    // Shift+Wheel scrolls horizontally (the OS-wide convention); plain wheel is
    // vertical. The horizontal axis has no tween (see ScrollViewer's design note),
    // so it applies immediately.
    const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
    if (shift) {
        if (scroll_.MaxOffsetX() <= 0.0f) return;  // nothing to scroll sideways
        constexpr float kStep = 3.0f * 16.0f;      // 3 lines per notch
        scroll_.SetOffsetX(scroll_.OffsetX() - (e.wheelDelta / 120.0f) * kStep);
        SyncScrollArrange();
        e.handled = true;
        return;
    }

    if (scroll_.MaxOffset() <= 0.0f) return;  // nothing to scroll

    // Standard wheel scroll: 3 lines per notch (120 delta units).
    constexpr float kLinesPerNotch = 3.0f;
    constexpr float kLineHeight = 16.0f;  // arbitrary line height
    float lines = (e.wheelDelta / 120.0f) * kLinesPerNotch;
    scroll_.AnimateBy(-lines * kLineHeight);
    SyncScrollArrange();
    e.handled = true;
}

void ScrollPanel::OnKeyDownRouted(KeyEventArgs& e)
{
    // Either axis alone must keep working: a panel that overflows only horizontally
    // (wide Grid, short content) still needs Left/Right.
    if (scroll_.MaxOffset() <= 0.0f && scroll_.MaxOffsetX() <= 0.0f) return;

    float delta = 0.0f;
    constexpr float kLineHeight = 16.0f;

    switch (e.vk) {
    case VK_UP:
        delta = -kLineHeight;
        break;
    case VK_DOWN:
        delta = kLineHeight;
        break;
    case VK_PRIOR:  // PageUp
        delta = -PageStep();
        break;
    case VK_NEXT:   // PageDown
        delta = PageStep();
        break;
    case VK_LEFT:
        if (scroll_.MaxOffsetX() > 0.0f) {
            scroll_.SetOffsetX(scroll_.OffsetX() - kLineHeight);
            SyncScrollArrange();
            e.handled = true;
        }
        return;
    case VK_RIGHT:
        if (scroll_.MaxOffsetX() > 0.0f) {
            scroll_.SetOffsetX(scroll_.OffsetX() + kLineHeight);
            SyncScrollArrange();
            e.handled = true;
        }
        return;
    case VK_HOME:
        scroll_.SetOffset(0.0f);
        SyncScrollArrange();
        e.handled = true;
        return;
    case VK_END:
        scroll_.SetOffset(scroll_.MaxOffset());
        SyncScrollArrange();
        e.handled = true;
        return;
    default:
        return;
    }

    scroll_.AnimateBy(delta);
    SyncScrollArrange();
    e.handled = true;
}

float ScrollPanel::PageStep() const
{
    // Page up/down scrolls (viewport height - one line), so there's overlap for context.
    constexpr float kLineHeight = 16.0f;
    return std::max(bounds_.h - kLineHeight, kLineHeight);
}

void ScrollPanel::OnPointerMoved(PointerEventArgs& e)
{
    float dipX = e.position.x;
    float dipY = e.position.y;

    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        SyncScrollArrange();  // the drag moved the offset
        e.handled = true;
        return;
    }
    if (scroll_.IsHDragging()) {
        scroll_.HDragTo(dipX);
        SyncScrollArrange();
        e.handled = true;
        return;
    }

    // Wake the scrollbars on any movement near them.
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));
    scroll_.SetHBarHover(scroll_.HitHBarRegion(dipX, dipY));
}

void ScrollPanel::OnPointerPressed(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;

    float dipX = e.position.x;
    float dipY = e.position.y;

    // A press anywhere in either hover strip (16 DIP) starts a drag, not just one on
    // the visible thumb (3 DIP idle / 7 DIP expanded). The press target must match
    // the region that gives hover feedback: a rail that lights up under the pointer
    // but ignores the click reads as broken, and asking the user to land inside
    // 3 DIP is not a real target. Vertical rail first: it owns the corner.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        // Off-thumb presses first center the thumb on the pointer, then drag from
        // there. Without the jump the thumb would stay put and then leap on the first
        // move, because DragTo is relative to where the press landed.
        if (!scroll_.HitThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
            SyncScrollArrange();
        }
        scroll_.BeginDrag(dipY);
        if (auto* input = Context().input)
            input->CapturePointer(this);
        e.handled = true;
        return;
    }

    if (scroll_.HitHBarRegion(dipX, dipY)) {
        if (!scroll_.HitHThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.HThumbRect();
            const float trackRange = std::max(1.0f, bounds_.w - thumb.w);
            const float wantLeft = (dipX - thumb.w * 0.5f) - bounds_.x;
            scroll_.SetOffsetX(wantLeft / trackRange * scroll_.MaxOffsetX());
            SyncScrollArrange();
        }
        scroll_.BeginHDrag(dipX);
        if (auto* input = Context().input)
            input->CapturePointer(this);
        e.handled = true;
        return;
    }
}

void ScrollPanel::OnPointerReleased(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;

    bool wasDragging = scroll_.IsDragging() || scroll_.IsHDragging();
    if (wasDragging) {
        scroll_.EndDrag();
        scroll_.EndHDrag();
        if (auto* input = Context().input) {
            if (input->Captured() == this)
                input->ReleaseCapture(this);
        }
        e.handled = true;
    }
}

void ScrollPanel::OnPointerLeft()
{
    // Stop hovering the scrollbars when the pointer leaves the panel.
    scroll_.SetBarHover(false);
    scroll_.SetHBarHover(false);
}

void ScrollPanel::Render(const DrawingContext& dc)
{
    if (!dc.Dc()) return;  // headless

    // Clip to the panel's bounds so children and scrollbar don't draw outside.
    ClipGuard clip = dc.PushClip(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

    // NO scroll transform. The offset lives in the children's bounds (see
    // ArrangeOverride), which is what makes compositor-backed children scroll too:
    // a TextArea's pixels come from a DComp child visual positioned from its bounds,
    // and a D2D transform on this device context cannot move it. The previous
    // revision translated here instead, so ordinary controls scrolled while
    // composited ones stayed nailed in place.
    for (auto& child : children_) {
        if (!child || !child->IsVisible()) continue;
        // Cull against the viewport using on-screen bounds.
        const RectDip cb = child->VisualBounds();
        if (cb.bottom() < bounds_.y || cb.y > bounds_.bottom()) continue;
        child->RenderWithOpacity(dc);
    }

    // Scrollbar on top of the content.
    scroll_.Render(dc);
}

} // namespace fluent
