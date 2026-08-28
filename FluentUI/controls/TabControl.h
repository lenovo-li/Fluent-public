// TabControl.h — a tab strip over a single-content area.
//
// The strip is drawn by TabControl itself rather than being built from Button
// children. Two reasons, both load-bearing:
//
//   * Focus. A strip of N Buttons puts N focusable stops in the Tab order, so
//     tabbing through a dialog would walk every tab header before reaching the
//     content. WinUI/WPF treat the strip as ONE stop with Left/Right moving the
//     selection, which is what this does.
//   * Cost. Headers are text plus a rounded rect. A Button per header means a
//     full FrameworkElement (margin, alignment, measure cache, tint animation)
//     per tab for something a single DrawText call covers.
//
// CONTENT OWNERSHIP AND ATTACH. Content elements are owned by the Panel base
// (children_), but only the SELECTED one is attached to the live tree and only
// it is measured, arranged, hit-tested, and rendered. An unselected tab's content
// is a detached subtree: it holds no context, no subscriptions, and no animation
// registration, so a 20-tab dialog costs one tab's worth of live tree rather than
// twenty. That is why SetSelectedIndex does the detach/attach dance instead of
// merely flipping a visibility flag.
//
// The consequence to know: a control inside an unselected tab cannot be reached
// through the tree (hit-test skips it, CollectFocusables skips it), and it loses
// anything acquired in OnAttachedToTree when the tab is deselected. State that
// must survive a tab switch belongs in the control's own members, not in a
// context-scoped resource.
#pragma once

#include "../layout/Panel.h"
#include "../animation/AnimatedValue.h"
#include "../base/Event.h"
#include <memory>
#include <string>
#include <vector>

struct IDWriteTextFormat;

namespace fluent {

enum class TabStripPlacement {
    Top,     // Strip at the top (default), content below
    Bottom,  // Strip at the bottom, content above
    Left,    // Strip on the left (vertical), content to the right
    Right    // Strip on the right (vertical), content to the left
};

class TabControl : public Panel {
public:
    TabControl() { SetFocusable(true); }

    // Add a tab. The content element is owned by this control; the returned
    // pointer is borrowed (valid until the tab is removed or the control dies).
    // The first tab added becomes selected.
    template <typename T>
    T* AddTab(std::wstring header, std::unique_ptr<T> content) {
        // Record the header FIRST. Panel::Add attaches the child immediately when
        // this panel is already live, and the "is this the selected tab" test below
        // needs the new tab's index to exist by then.
        headers_.push_back(std::move(header));
        headerWidthsDirty_ = true;  // a new string to measure
        const int newIndex = static_cast<int>(headers_.size()) - 1;
        T* raw = Add(std::move(content));
        // Panel::Add attaches eagerly; anything that is not the selection must come
        // straight back out, or every tab's subtree would be live at once.
        if (raw && raw->IsAttached() && newIndex != selectedIndex_)
            raw->DetachFromContext();
        // First tab added becomes the selection. SetSelectedIndex handles the
        // attach for it.
        if (selectedIndex_ < 0) SetSelectedIndex(newIndex);
        InvalidateDirtyPublic();
        return raw;
    }

    // Remove the tab at `index`. When the removed tab was selected, the selection
    // moves to the previous tab (or 0 if it was the first, or -1 if none remain).
    void RemoveTab(int index);

    int TabCount() const { return static_cast<int>(headers_.size()); }
    const std::wstring& HeaderAt(int index) const;

    int SelectedIndex() const { return selectedIndex_; }
    void SetSelectedIndex(int index);

    // Which edge the header strip occupies. Top (default) and Bottom keep the strip
    // horizontal; Left and Right make it vertical, with the content beside it.
    //
    // Keyboard navigation stays Left/Right arrow in every placement, matching WPF —
    // the arrows select the previous/next TAB, not a screen direction, so a vertical
    // strip does not silently rebind them to Up/Down. (Up/Down are not bound at all.)
    //
    // For a vertical strip every header takes the same width — the widest header plus
    // padding — rather than each sizing to its own text. A ragged right edge on a
    // vertical strip reads as a rendering fault, and it would also make the content
    // area's left edge depend on which tab happens to be longest.
    void SetTabStripPlacement(TabStripPlacement placement) {
        if (stripPlacement_ == placement) return;
        stripPlacement_ = placement;
        // Measure, not Render: the strip changes axis, so both the control's own
        // desired size and the content's available space change.
        InvalidateDirty(DirtyFlags::Measure);
    }
    TabStripPlacement StripPlacement() const { return stripPlacement_; }

    // True when the strip runs vertically (Left or Right placement). Exposed because
    // the geometry helpers below branch on it and a test asserting strip layout wants
    // to name the same condition the control uses.
    bool IsVerticalStrip() const {
        return stripPlacement_ == TabStripPlacement::Left ||
               stripPlacement_ == TabStripPlacement::Right;
    }

    // P1-11: Close button per tab. When enabled, each header draws a small '×'
    // button on the right. Clicking it raises TabCloseRequested with the tab index;
    // the owner decides whether to remove the tab (via RemoveTab). Default: hidden.
    void SetCloseButtonVisible(bool visible) {
        if (closeButtonVisible_ == visible) return;
        closeButtonVisible_ = visible;
        InvalidateDirty(DirtyFlags::Measure);
    }
    bool CloseButtonVisible() const { return closeButtonVisible_; }

    // Fired when a tab's close button is clicked. The tab is NOT removed
    // automatically — the owner calls RemoveTab(e.index) if the close should
    // proceed (e.g. after prompting to save unsaved changes).
    struct TabCloseRequestedArgs { int index; };
    Event<TabControl, TabCloseRequestedArgs>& TabCloseRequested() {
        return tabCloseRequested_;
    }

    // The currently selected content element, or null when there are no tabs.
    FrameworkElement* SelectedContent() const;

    // Whether the content at `index` is attached to the live tree. Exposed
    // because "only the selected tab is attached" is the whole design and a test
    // has no other way to observe it.
    bool IsContentAttached(int index) const;

    Event<TabControl, int>& SelectionChanged() { return selectionChanged_; }

    // Strip geometry, as pure functions of the header text metrics. Public so a
    // headless test can check hit-testing without a device.
    float StripHeightDip() const { return stripHeight_; }
    // The full slot width of header `index` (text plus horizontal padding), or 0
    // when out of range. Public because HeaderRect is built from it and a test
    // asserting strip layout wants the same number the control uses.
    float HeaderWidth(int index) const;
    // The strip rect for tab `index` in window DIPs, or an empty rect when the
    // index is out of range. Uses the measured header widths.
    RectDip HeaderRect(int index) const;
    // The tab index at a point in window DIPs, or -1 when the point is not on a
    // header. Used by the pointer handler and directly by tests.
    int HeaderIndexAt(float dipX, float dipY) const;

    // The close-button rect inside header `index` (window DIPs), or an empty rect
    // when close buttons are hidden or the index is out of range. Public for the
    // same reason as HeaderRect: hit-testing a header sub-region is the only
    // interesting logic here, and a headless test has no other way to reach it.
    RectDip CloseButtonRect(int index) const;
    // The tab whose close button is at the given point, or -1. Distinct from
    // HeaderIndexAt because the close button must win over tab selection: a click
    // landing on the '×' of tab 3 while tab 1 is selected must close 3, not select
    // it. The pointer handler tests this FIRST for that reason.
    int CloseButtonIndexAt(float dipX, float dipY) const;

    void Render(const DrawingContext& dc) override;
    // "Only the selected tab is live" must hold for every child-walking path,
    // not just attach: an unselected tab's content is detached (bounds stay
    // valid, so a plain bounds HitTest passes on it), and without these two
    // overrides the base Panel implementation would still render it, hit-test
    // it, and collect its dirty rects — which is how a deselected TextArea's
    // pixels showed through on top of another tab and its stale bounds kept
    // swallowing clicks meant for the headers.
    UIElement* HitTestDeep(float dipX, float dipY) override;
    void CollectDirtyBounds(std::vector<RectDip>& out) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;
    void OnAttachedToTree() override;

    // Attach ONLY the selected content. This is the hook that enforces the
    // one-live-tab invariant: Panel::AttachChildren attaches every child, so
    // doing the selection filter in OnAttachedToTree is not enough — the base's
    // AttachChildren runs afterwards and would re-attach all the others.
    void AttachChildren(const UIContext& ctx) override;

private:
    // Panel::Add is protected-by-convention here: callers use AddTab so a header
    // is always recorded alongside the content.
    using Panel::Add;
    using Panel::Emplace;

    // Invalidate through the base's protected hook (needed from the template
    // AddTab, which lives in the header).
    void InvalidateDirtyPublic() { InvalidateDirty(DirtyFlags::Measure); }

    // Recompute headerWidths_ from the current header strings, but ONLY when
    // headerWidthsDirty_ says they could have changed. Needs DWrite, so it leaves
    // a per-character fallback width (and stays dirty) while detached.
    void MeasureHeaders();
    // Attach exactly the selected content and detach every other.
    void SyncContentAttachment();

    std::vector<std::wstring> headers_;
    // Measured header widths in DIPs, parallel to headers_. Falls back to a
    // per-character estimate when DWrite is unavailable.
    std::vector<float> headerWidths_;
    // Whether headerWidths_ needs recomputing. Set by anything that changes the
    // header STRINGS (AddTab / RemoveTab), and by the initial construction.
    //
    // This exists because headerWidths_ depends on the header text and nothing
    // else — not on the available size, not on DPI (metrics are DIP-space), not on
    // the theme (kHeaderFontSize is a constant, not a token). MeasureOverride,
    // however, is called with a fresh constraint on every frame of a resize drag,
    // and it called MeasureHeaders unconditionally: two throwaway
    // IDWriteTextLayouts per tab, per frame, each read for a single float and
    // dropped. Gating on the text is what makes the steady state free.
    //
    // Starts true so the first Measure (or OnAttachedToTree) does the real
    // measurement even when no tab was ever added after construction.
    bool headerWidthsDirty_ = true;
    int selectedIndex_ = -1;
    // Strip dimensions in DIPs. For horizontal (Top/Bottom), stripHeight_ is the
    // strip's height and stripWidth_ unused; for vertical (Left/Right), stripWidth_
    // is the strip's width and stripHeight_ unused. The name stays stripHeight_ for
    // the default Top mode so every existing callsite compiles unchanged.
    float stripHeight_ = 36.0f;
    float stripWidth_ = 120.0f;
    TabStripPlacement stripPlacement_ = TabStripPlacement::Top;
    // The selected header's indicator slides between tabs instead of jumping.
    AnimatedValue indicatorX_{0.0f};
    AnimatedValue indicatorW_{0.0f};
    // Content cross-fade: 0 right after a switch, eased to 1.
    AnimatedValue contentFade_{1.0f};
    int hoverIndex_ = -1;
    Event<TabControl, int> selectionChanged_;
    bool closeButtonVisible_ = false;
    // The close button the pointer is currently over, or -1.
    int closeButtonHoverIndex_ = -1;
    Event<TabControl, TabCloseRequestedArgs> tabCloseRequested_;
};

} // namespace fluent
