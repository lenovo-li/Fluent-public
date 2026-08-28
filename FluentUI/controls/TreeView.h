// TreeView.h — Lightweight Fluent tree/list control for value-owned row data.
//
// TreeView is a Selector<TreeViewRow> (roadmap §WP-06): the row vector lives in
// the ItemsControl base as items_, and the selection index lives in the Selector
// base as selectedIndex_ (a *row* index into items_). TreeView keeps its own
// tree-specific derived state (visible rows, id→index maps, expand/collapse,
// type-ahead) and overrides SetSelectedIndex so its public API still takes a
// *visible* index (mapped onto a row index before the base dedup/store), while
// SelectedIndex() continues to return the row index — the historical asymmetry is
// preserved deliberately.
#pragma once

#include "primitives/Selector.h"
#include "../base/Event.h"
#include "../graphics/DWriteContext.h"
#include "../layout/ScrollViewer.h"
#include "../composition/ScrollContentHost.h"
#include "../composition/OverlaySignature.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ID2D1DeviceContext;
struct IDWriteTextFormat;

namespace fluent {

enum class TreeViewIcon {
    None,
    Computer,
    Folder,
    Device,
    Warning,
    Bluetooth,
    Disk,
};

struct TreeViewRow {
    int id = 0;
    int parentId = -1;
    std::wstring text;
    int depth = 0;
    bool dimmed = false;
    bool hasChildren = false;
    bool expanded = true;
    TreeViewIcon icon = TreeViewIcon::Device;
    void* user = nullptr;
};

// Payload for TreeView::SelectionChanged: the visible index of the newly selected
// row and the row data itself.
struct TreeSelection {
    int visibleIndex = -1;
    const TreeViewRow* row = nullptr;
};

// SelectionMode now lives in Selector (primitives/Selector.h) — the enum moved
// there when the multi-select state was hoisted so ListBox could share it. It is
// still reachable as fluent::SelectionMode, so existing call sites are unchanged.

class TreeView : public Selector<TreeViewRow> {
public:
    TreeView() {
        SetFocusable(true);
    }

    // Replace the row set. Routes through the ItemsControl base (items_) so the
    // shared store owns the vector; OnItemsChanged rebuilds the tree indices and
    // restores the selection by id. Kept as SetRows for API compatibility.
    void SetRows(std::vector<TreeViewRow> rows);

    // Selection: SetSelectedIndex takes a VISIBLE index (mapped to a row index);
    // SelectedIndex() returns the ROW index (inherited from Selector). SelectedRow
    // is an alias for SelectedItem() with the historical name.
    void SetSelectedIndex(int visibleIndex) override;
    const TreeViewRow* SelectedRow() const { return SelectedItem(); }

    // Selection mode + the multi-select API live in Selector now
    // (SetSelectionMode / GetSelectionMode / SelectedIndices / IsSelected /
    // ToggleSelection / RangeSelectTo). The four names below are kept as thin
    // row-flavoured aliases: they read better at a tree call site, and they are
    // what the existing tests and demo call. The indices are ROW indices (into
    // items_), not visible indices — same as before the hoist.
    std::vector<int> SelectedRowIndices() const { return SelectedIndices(); }
    bool IsRowSelected(int rowIndex) const { return IsSelected(rowIndex); }
    void ToggleRowSelection(int rowIndex) { ToggleSelection(rowIndex); }

    // Fired when the selected row changes (arrow keys, type-ahead, or a row
    // click). Replaces SetOnSelectionChanged(std::function<...>).
    Event<TreeView, TreeSelection>& SelectionChanged() { return selectionChanged_; }

    // Fired when the set of selected rows changes in Multiple mode (Ctrl+click,
    // Shift+click, or programmatic ToggleRowSelection / RangeSelectTo).
    Event<TreeView, std::vector<int>>& SelectionSetChanged() { return selectionSetChanged_; }

    void ScrollBy(float deltaDip) { scroll_.ScrollBy(deltaDip); Invalidate(); }
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void OnTextInput(wchar_t ch) override;
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

    void Render(const DrawingContext& dc) override;

    // Routed pointer input: scrollbar drag (with capture) + row hover / click.
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

protected:
    // Tree-attach: create the compositor scroll host if the window offers a
    // composition backend (else fall back to the UI-thread scroll_ path).
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    void OnBoundsChanged() override;
    void UpdateContextModalResize(bool inModalResize) override;
    void OnFocusChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnDeviceLost() override;
    void OnDeviceRestored() override;
    void OnPointerLeft() override;
    // In composited mode Render() draws nothing, so opacity has to reach the
    // composition sub-tree instead of the host's DrawingContext.
    void OnOpacityChanged(float opacity) override;

    // Same reason, for the collapsed state: skipping Render cannot hide a DComp
    // visual, so hiding has to reach the compositor too.
    void OnVisibilityChanged(bool visible) override;
    void OnAncestorVisibilityChanged() override;

    // ItemsControl hook: rebuild derived indices + restore selection by id.
    void OnItemsChanged() override;
    // Selector hook: ensure the new row is visible + raise the rich TreeSelection.
    void OnSelectionChanged(int oldIndex, int newIndex) override;
    // Selector hook: the multi-select set changed via ToggleSelection /
    // RangeSelectTo. Repaint the composited rows and raise SelectionSetChanged.
    void OnSelectionSetChanged() override;

private:
    // --- Compositor scroll (Phase 3) --------------------------------------
    // True when the overscan content host is live (a composition backend was
    // available at attach). When false everything falls back to the UI-thread
    // scroll_ path unchanged.
    bool CompositionActive() const { return content_ && content_->Valid(); }
    // The scroll offset currently shown: the content host's effective (possibly
    // mid-tween) offset in composition mode, else scroll_'s offset.
    float CurrentOffset() const;
    // Reconcile the compositor surfaces with the current state and commit. Safe
    // only OUTSIDE the window content frame (attach / bounds / theme / dpi / input
    // / tick) — never from Render.
    //   redrawContent: re-rasterize the row surface (selection/items/theme/bounds
    //     changed its pixels). false = only refill when scrolling near a buffer
    //     edge; the compositor owns the offset otherwise.
    //   overlayForce: re-rasterize the scrollbar+focus overlay unconditionally
    //     (focus/hover/theme/bounds). false = redraw ONLY if the bar's visual
    //     signature changed since last time — this is what stops the idle-hide
    //     countdown from re-rasterizing a static bar every frame (the flicker).
    void RefreshComposition(bool redrawContent, bool overlayForce);
    void UpdateCompositionVisibility();
    // The overlay's current visual determinants (shared with TextArea — see
    // OverlaySignature.h for why the comparison exists).
    OverlaySignature CurrentOverlaySig() const;
    // Draw all visible rows intersecting [surfaceOriginDip, +heightDip) into the
    // content surface's device context, in DIPs local to the surface origin.
    void DrawRowsToSurface(ID2D1DeviceContext* dc, float surfaceOriginDip,
                           float surfaceHeightDip);
    // Draw the scrollbar + focus ring into the overlay surface (viewport-local DIPs).
    void DrawOverlayToSurface(ID2D1DeviceContext* dc, float viewportWDip,
                              float viewportHDip);
    // Shared single-row draw (used by both the fallback Render and the surface
    // refill). `rowTopY` is the row's top in the target's DIP space.
    void DrawRow(const DrawingContext& dc, IDWriteTextFormat* fmt, int rowIndex,
                 float rowTopY, float leftX, float rightX);

    int HitRow(float dipY) const;
    int VisibleCapacity() const;
    int PageStep() const;
    void RebuildIndex();
    void RebuildVisible();
    int RowIndexById(int id) const;
    int VisibleIndexByRow(int rowIndex) const;
    void ToggleRow(int rowIndex);
    void EnsureSelectedVisible();
    bool MatchPrefix(const std::wstring& text, const std::wstring& prefix) const;
    // Select the first visible row (searching from just after the current
    // selection, then wrapping) whose text starts with `prefix`. Returns true if
    // a row was selected. Does NOT touch typeSearch_ — the caller owns that state
    // — so it can be reused for the single-character fallback without recursion.
    bool SelectByPrefix(const std::wstring& prefix);

    // Derived tree state (items_ + selectedIndex_ live in the Selector base).
    std::vector<int> visibleRows_;
    std::unordered_map<int, int> idToIndex_;
    std::vector<int> parentIndex_;
    int pendingSelectedId_ = -1;   // captured across a SetRows to restore selection
    float rowHeight_ = 28.0f;
    ScrollViewer scroll_;  // scrollbar visual model + fallback UI-thread scroll
    // Compositor overscan content host (null → UI-thread fallback via scroll_).
    // In composition mode scroll_ is used only for the scrollbar's fade/expand
    // visual state + hit-testing; the content OFFSET is owned by content_.
    std::unique_ptr<ScrollContentHost> content_;
    float dragStartOffset_ = 0.0f;  // content offset at thumb-drag start (comp mode)
    OverlaySignature lastOverlaySig_;  // last-rasterized overlay state (tick gating)
    std::wstring typeSearch_;
    DWORD lastTypeTick_ = 0;
    Event<TreeView, TreeSelection> selectionChanged_;
    Event<TreeView, std::vector<int>> selectionSetChanged_;
    // Multi-select state (selectionMode_ / selectedIndices_ / selectionAnchor_)
    // lives in the Selector base — see primitives/Selector.h for the invariants.
};

} // namespace fluent
