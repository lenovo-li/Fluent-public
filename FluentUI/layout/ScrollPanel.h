// ScrollPanel.h — A scrollable container panel for arbitrary child elements.
//
// ScrollPanel is the missing piece in FluentUI's layout system: a general-purpose
// scrollable container that can hold any mix of controls and layouts. Unlike
// ListBox/TreeView (which virtualize fixed-height items), ScrollPanel does NOT
// virtualize — it measures and renders all children, then clips to the viewport.
//
// Use cases:
//   * Demo windows with many controls stacked vertically
//   * Settings panels with form fields that exceed the window height
//   * TabControl content pages that need to scroll
//
// NOT suitable for:
//   * Large uniform lists (use ListBox — it virtualizes)
//   * Large trees (use TreeView — it virtualizes)
//   * Multi-column text longer than ~10k lines (use TextArea — it virtualizes)
//
// Scrolling is two-axis. Vertical is the primary axis: children stack top-to-bottom
// and the total height drives the vertical extent. Horizontal overflow (a child
// whose desired width exceeds the viewport, e.g. a Grid of fixed-width Calendars in
// a narrow window) gets the bottom rail: the child keeps its natural width and the
// panel scrolls sideways. Both rails are overlays from the shared ScrollViewer, so
// they coexist with a child's own scrollbars exactly the way the vertical one does
// (16 DIP hover strip wins the hit-test, content keeps the rest).
//
// SCROLLING MECHANICS. The scroll offset is applied directly to children's bounds
// in ArrangeOverride (x = content.x - offsetX, y = content.y - offsetY), so their
// bounds_ reflect where they actually appear on screen. This is required for
// compositor-backed children (TextArea, TreeView) whose pixels come from DComp child
// visuals positioned by bounds_ — a D2D transform in Render would not move them.
// The consequence: when either offset changes, the whole panel must re-Arrange (not
// just repaint), which is why every offset mutation routes through SyncScrollArrange.
#pragma once

#include "Panel.h"
#include "ScrollViewer.h"
#include <algorithm>
#include <vector>

namespace fluent {

class ScrollPanel : public Panel {
public:
    ScrollPanel() {
        SetFocusable(true);
        // A ScrollPanel is a general-purpose container — the user should always be
        // able to see that its content overflows (unlike a ListBox/TreeView where
        // the convention is an auto-hiding overlay scrollbar).
        scroll_.SetKeepVisibleWhenOverflow(true);
    }

    // Vertical spacing between children (DIPs). Defaults to 0.
    void SetSpacing(float dip) { spacing_ = dip; InvalidateMeasure(); }
    float Spacing() const { return spacing_; }

    // Content padding (DIPs) — the gap between the panel's edge and the child
    // stack, on all four sides. One value sets every side; a Thickness sets them
    // independently (left, top, right, bottom). Distinct from SetSpacing, which is
    // the gap BETWEEN children.
    //
    // This is also what keeps a focus ring intact: focus visuals paint a few DIPs
    // OUTSIDE a control's bounds, and a child flush against the panel edge has its
    // ring sheared off by the viewport clip. A small padding (4–8 DIP) is the
    // difference between a full ring and a clipped one.
    void SetPadding(Thickness p) { padding_ = p; InvalidateMeasure(); }
    void SetPadding(float all) { SetPadding(Thickness(all)); }
    Thickness Padding() const { return padding_; }

    // Current vertical scroll offset (DIPs from top of content).
    float VerticalOffset() const { return scroll_.Offset(); }
    // Moving the offset re-Arranges rather than merely repainting: the offset is
    // baked into the children's bounds, so stale bounds would leave both the
    // compositor visuals and hit-testing at the old position.
    void SetVerticalOffset(float dip) {
        scroll_.SetOffset(dip);
        SyncScrollArrange();
    }

    // Maximum scroll offset (contentHeight - viewportHeight, clamped >= 0).
    float MaxVerticalOffset() const { return scroll_.MaxOffset(); }

    // Horizontal axis: the widest child's natural width vs the viewport width.
    // Same semantics as the vertical one; zero when everything fits.
    float HorizontalOffset() const { return scroll_.OffsetX(); }
    void SetHorizontalOffset(float dip) {
        scroll_.SetOffsetX(dip);
        SyncScrollArrange();
    }
    float MaxHorizontalOffset() const { return scroll_.MaxOffsetX(); }

    // Scrollbar interaction.
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;
    void OnPointerLeft() override;

    // Animation tick for smooth scrolling + scrollbar fade/expand.
    bool WantsAnimationTick() const override { return scroll_.NeedsTick(); }
    void OnAnimationTick(float dtSec) override {
        scroll_.Tick(dtSec);
        SyncScrollArrange();  // tween advanced the offset
        // scroll_ is a MEMBER, not a tree node: its Invalidate() (called inside Tick
        // on every fade/expand step) has no invalidate callback and no parent, so it
        // never reaches the frame scheduler. The bar's hover-expand and idle-fade
        // animations therefore produced no dirty region — the rail only repainted
        // when some OTHER element happened to invalidate on that frame (a blinking
        // caret, a progress animation), which the user saw as the expand animation
        // "loading in segments". Report the repaint from here, where it does reach
        // the tree.
        Invalidate();
    }

    void Render(const DrawingContext& dc) override;

    // Declare the clip so compositor-backed descendants (TextArea, TreeView) at any
    // depth crop their DComp visuals to this viewport. A D2D clip in Render covers
    // ordinary controls but cannot reach a composition visual; without this, a child
    // arranged half-below the panel (its bounds carry the scroll offset) paints
    // straight over whatever sits below — the StatusBar, in the reported case.
    bool GetViewportClipForDescendants(RectDip& out) const override {
        out = bounds_;
        return true;
    }

    // A ScrollPanel must be hit-testable itself (not only through its children):
    // a wheel event over empty space between/around children still needs to reach
    // the panel so it can scroll. The base Panel::HitTestDeep returns nullptr
    // ("panels are not interactive targets"), which would swallow the wheel when
    // the pointer is not directly over a child.
    //
    // No scroll compensation here, deliberately: ArrangeOverride already bakes the
    // offset into every child's bounds_, so a child's bounds ARE where it is on
    // screen. Adding the offset again (as an earlier revision did) double-counts it.
    UIElement* HitTestDeep(float dipX, float dipY) override {
        if (!HitTest(dipX, dipY)) return nullptr;

        // The scrollbars are overlays drawn on top of the content, so they win the
        // hit-test — matching the paint order. Vertical rail (right strip) first,
        // horizontal rail (bottom strip) second; the corner belongs to the vertical
        // rail, same as the paint order.
        if (scroll_.HitBarRegion(dipX, dipY)) return this;
        if (scroll_.HitHBarRegion(dipX, dipY)) return this;

        // Children only inside the content rect (viewport minus the live rails): a
        // child wider than the viewport keeps valid bounds past the right edge (the
        // overflow is clipped visually), and without this check the clipped-away part
        // would still answer clicks on pixels nobody can see.
        const RectDip vp = ContentViewportDip();
        if (vp.contains(dipX, dipY)) {
            for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
                if (!*it || !(*it)->IsVisible()) continue;
                if (UIElement* hit = (*it)->HitTestDeep(dipX, dipY)) return hit;
            }
        }

        // Fall back to the panel itself so wheel-over-empty-space scrolls.
        return this;
    }

    // Children's dirty rects need no offset fixup (their bounds are already the
    // on-screen position), but they DO need clipping: Render clips to bounds_, so a
    // child scrolled halfway out must not advertise the part nobody paints — the
    // host would clear those pixels and never restore them.
    void CollectDirtyBounds(std::vector<RectDip>& out) override {
        if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
        std::vector<RectDip> childRects;
        for (auto& c : children_)
            if (c && c->IsVisible()) c->CollectDirtyBounds(childRects);
        for (RectDip r : childRects) {
            const float left = std::max(r.x, bounds_.x);
            const float right = std::min(r.right(), bounds_.right());
            const float top = std::max(r.y, bounds_.y);
            const float bottom = std::min(r.bottom(), bounds_.bottom());
            if (right <= left || bottom <= top) continue;
            out.push_back(RectDip{left, top, right - left, bottom - top});
        }
    }

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;
    void OnBoundsChanged() override;

    // scroll_ is an embedded model, not a tree node, so nothing would ever give it
    // a UIContext — and UIElement::Theme() falls back to a LIGHT default snapshot
    // when context_.theme is null. The rail was therefore drawn with light-theme
    // ink on a dark background, i.e. invisible in dark mode. Forwarding the context
    // here is what puts it on the real theme (and keeps it correct across a theme
    // switch, since the host overwrites its snapshot in place).
    void OnAttachedToTree() override { scroll_.AttachToContext(Context()); }
    void OnDetachedFromTree() override {
        if (scroll_.IsAttached()) scroll_.DetachFromContext();
    }


private:
    // Update scroll content height based on measured children.
    void UpdateScrollExtent();

    // Page step for PageUp/PageDown (viewport height minus one line).
    float PageStep() const;

    // Request an Arrange pass when the scroll has moved, or only its TARGET moved.
    // Every path that changes either offset (wheel, drag, keyboard, tween tick)
    // routes through here, because with the offset living in the children's bounds
    // an Arrange is the only thing that can move them.
    //
    // The vertical comparison is against TargetOffset(), not Offset(), and that
    // distinction is the whole reason wheel scrolling appeared to do nothing:
    // AnimateBy only sets the tween TARGET — offset_ does not move until the first
    // Tick. Comparing the live offset therefore found it unchanged on the frame the
    // notch arrived, skipped the invalidation, and left the children arranged at the
    // old place. The horizontal axis has no tween (deliberately — see ScrollViewer),
    // so OffsetX() IS its live value.
    void SyncScrollArrange() {
        if (scroll_.TargetOffset() == arrangedOffset_ &&
            scroll_.OffsetX() == arrangedOffsetX_) return;
        InvalidateArrange();
    }

    // The content rect actually visible: bounds minus the horizontal rail's strip
    // when it is live. The vertical rail's strip is already excluded from children
    // by ArrangeOverride's width reserve; the horizontal rail's strip is excluded
    // from the viewport here so the last row can scroll fully into view (and so
    // hit-testing does not treat the rail's strip as content).
    RectDip ContentViewportDip() const {
        RectDip r = bounds_;
        if (scroll_.MaxOffsetX() > 0.0f)
            r.h = std::max(0.0f, r.h - 16.0f);
        return r;
    }

    ScrollViewer scroll_;
    float spacing_ = 0.0f;
    Thickness padding_;           // content inset on all four sides
    float contentHeight_ = 0.0f;  // total height of all children + spacing
    // The width constraint children were last measured with. Arrange compares each
    // child's Desired width against it: only a child wider than its constraint has a
    // genuine natural width worth a horizontal rail; the rest were stretched BY the
    // measurement and can be re-fit to the viewport width.
    float measureConstraintW_ = 0.0f;
    // The offset the current child bounds were arranged at. Compared against the
    // live offset to decide whether an Arrange is still owed.
    float arrangedOffset_ = 0.0f;
    float arrangedOffsetX_ = 0.0f;
};

} // namespace fluent
