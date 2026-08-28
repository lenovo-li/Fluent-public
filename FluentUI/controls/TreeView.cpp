// TreeView.cpp

#include "TreeView.h"
#include "../styling/ThemeTokens.h"
#include "../core/ScrollMath.h"
#include "../input/InputManager.h"
#include "../window/WindowServices.h"
#include "../composition/ICompositionBackend.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {

void DrawIcon(const DrawingContext& dc, TreeViewIcon icon, float x, float y,
              const ColorTokens& pal)
{
    D2D1_COLOR_F c = pal.textSecondary;
    switch (icon) {
    case TreeViewIcon::Computer: c = pal.accent; break;
    case TreeViewIcon::Folder: c = D2D1::ColorF(0.86f, 0.60f, 0.18f, 1.0f); break;
    case TreeViewIcon::Warning: c = D2D1::ColorF(0.90f, 0.38f, 0.16f, 1.0f); break;
    case TreeViewIcon::Bluetooth: c = D2D1::ColorF(0.18f, 0.47f, 0.86f, 1.0f); break;
    case TreeViewIcon::Disk: c = D2D1::ColorF(0.25f, 0.55f, 0.78f, 1.0f); break;
    default: c = pal.textSecondary; break;
    }

    if (icon == TreeViewIcon::Folder) {
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(x + 1, y + 5, x + 13, y + 12), 1.5f, 1.5f), c);
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(x + 2, y + 3, x + 8, y + 6), 1.0f, 1.0f), c);
        return;
    }
    if (icon == TreeViewIcon::Warning) {
        dc.DrawLine(D2D1::Point2F(x + 7, y + 2), D2D1::Point2F(x + 13, y + 12), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 13, y + 12), D2D1::Point2F(x + 1, y + 12), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 1, y + 12), D2D1::Point2F(x + 7, y + 2), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 7, y + 5), D2D1::Point2F(x + 7, y + 8), c, 1.2f);
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 7, y + 10.5f), 0.8f, 0.8f), c);
        return;
    }
    if (icon == TreeViewIcon::Bluetooth) {
        dc.DrawLine(D2D1::Point2F(x + 7, y + 2), D2D1::Point2F(x + 7, y + 12), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 7, y + 2), D2D1::Point2F(x + 11, y + 5), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 11, y + 5), D2D1::Point2F(x + 4, y + 10), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 7, y + 12), D2D1::Point2F(x + 11, y + 9), c, 1.3f);
        dc.DrawLine(D2D1::Point2F(x + 11, y + 9), D2D1::Point2F(x + 4, y + 4), c, 1.3f);
        return;
    }
    if (icon == TreeViewIcon::Disk) {
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(x + 2, y + 3, x + 12, y + 11), 2.0f, 2.0f), c);
        dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 7, y + 7), 2.0f, 2.0f), pal.cardFill);
        return;
    }

    if (icon == TreeViewIcon::Computer) {
        // Monitor body
        dc.DrawRoundedRect(D2D1::RoundedRect(D2D1::RectF(x + 1, y + 2, x + 13, y + 10), 1.5f, 1.5f), c, 1.2f);
        // Stand foot
        dc.DrawLine(D2D1::Point2F(x + 4, y + 10), D2D1::Point2F(x + 4, y + 12), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(x + 10, y + 10), D2D1::Point2F(x + 10, y + 12), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(x + 3, y + 12), D2D1::Point2F(x + 11, y + 12), c, 1.2f);
    } else {
        // Generic device — IC chip: filled body with pin lines on both sides.
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(x + 4, y + 3, x + 10, y + 11), 1.5f, 1.5f), c);
        // Left pins
        dc.DrawLine(D2D1::Point2F(x + 2, y + 5), D2D1::Point2F(x + 4, y + 5), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(x + 2, y + 7), D2D1::Point2F(x + 4, y + 7), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(x + 2, y + 9), D2D1::Point2F(x + 4, y + 9), c, 1.2f);
        // Right pins
        dc.DrawLine(D2D1::Point2F(x + 10, y + 5), D2D1::Point2F(x + 12, y + 5), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(x + 10, y + 7), D2D1::Point2F(x + 12, y + 7), c, 1.2f);
        dc.DrawLine(D2D1::Point2F(x + 10, y + 9), D2D1::Point2F(x + 12, y + 9), c, 1.2f);
    }
}

} // namespace

void TreeView::SetRows(std::vector<TreeViewRow> rows)
{
    // Capture the selected row's id so it can be restored after the store is
    // replaced (OnItemsChanged reads pendingSelectedId_). Then route through the
    // ItemsControl base, which stores items_ and calls OnItemsChanged.
    pendingSelectedId_ = (selectedIndex_ >= 0 && selectedIndex_ < ItemCount())
                             ? items_[selectedIndex_].id
                             : -1;
    SetItems(std::move(rows));
}

void TreeView::OnItemsChanged()
{
    RebuildIndex();
    RebuildVisible();

    // Restore selection by id (falls back to the first visible row). Set the base
    // storage directly — this is not a user-driven selection change, so it does
    // not raise SelectionChanged (matches the historical SetRows behavior).
    int restored = RowIndexById(pendingSelectedId_);
    if (restored < 0 && !visibleRows_.empty())
        restored = visibleRows_[0];
    selectedIndex_ = restored;
    pendingSelectedId_ = -1;

    // Re-seed the multi-select set from the restored single selection. The old set
    // held row indices into the PREVIOUS items_, where the same index now means a
    // different row (or none) — see Selector::ReseedSelectionSet for why this
    // re-seeds rather than only clearing.
    ReseedSelectionSet();
    scroll_.SetContentHeight(rowHeight_ * visibleRows_.size());
    if (CompositionActive()) RefreshComposition(true, true);  // extent + rows changed
    Invalidate();
}

void TreeView::SetSelectedIndex(int visibleIndex)
{
    // The public API takes a VISIBLE index; map it to a row index, then delegate
    // to the Selector base for the dedup/store + OnSelectionChanged hook.
    // -1 means "no selection" and passes through unchanged.
    int rowIndex = -1;
    if (visibleIndex >= 0 && !visibleRows_.empty()) {
        int vi = std::clamp(visibleIndex, 0, static_cast<int>(visibleRows_.size()) - 1);
        rowIndex = visibleRows_[vi];
    }
    Selector<TreeViewRow>::SetSelectedIndex(rowIndex);
}

void TreeView::OnSelectionChanged(int /*oldIndex*/, int newIndex)
{
    // The set collapse for a plain (non-modifier) selection now happens in
    // Selector::SetSelectedIndex, before this hook runs — it is the one place
    // every such change funnels through, so no subclass has to remember it.
    EnsureSelectedVisible();
    if (CompositionActive()) RefreshComposition(true, true);  // selection highlight moved
    if (newIndex >= 0 && newIndex < ItemCount()) {
        TreeSelection sel{VisibleIndexByRow(newIndex), &items_[newIndex]};
        selectionChanged_.Raise(*this, sel);
    }
}

// --- Multi-select side effects ---------------------------------------------
// The state and the four gesture methods live in Selector now. What stays here
// is the tree-specific consequence of a set change: the composited row surface
// holds the selection highlight, so it has to be re-rasterized, and the typed
// event carries a vector the tree's subscribers already expect.
void TreeView::OnSelectionSetChanged()
{
    if (CompositionActive()) RefreshComposition(true, true);
    // Event::Raise takes Args& (non-const), so the vector must be an lvalue.
    std::vector<int> sel = SelectedIndices();
    selectionSetChanged_.Raise(*this, sel);
}

void TreeView::OnBoundsChanged()
{
    LayoutCostProbe::Scope probe(LayoutCostKey::TreeViewBoundsChanged);
    scroll_.SetBounds(bounds_);
    scroll_.SetContentHeight(rowHeight_ * visibleRows_.size());
    // After quantization, SetViewport only clears contentDrawn_ when the quantized
    // surface actually resized — not on every bounds change. The old comment (deleted
    // redrawContent=true is NOT known to be necessary here, and measurement says it is
    // most of the resize cost (TreeView's single surface was 3.37ms in one snapshot,
    // versus 0.4ms for surface allocation). A conditional second pass was tried and
    // deleted: by the time the first RefreshComposition returns, Rebase has already
    // rasterized and set contentDrawn_, so any "did the pixels survive" test after it
    // is always false — dead code that reads as a working optimization. Skipping the
    // redraw for real needs the geometry and the pixels to be separable (push
    // viewport/clip/extent now, rasterize on WM_EXITSIZEMOVE), which is a change to
    // ScrollContentHost, not a flag here. Rows are fixed-height and their content does
    // not depend on width, so re-rasterizing them because the viewport moved during
    // Rebase resize is pure waste — but the flag can't avoid it.
    if (CompositionActive()) {
        RefreshComposition(/*redrawContent*/ false, /*overlayForce*/ true);
    }
}

void TreeView::UpdateContextModalResize(bool inModalResize)
{
    UIElement::UpdateContextModalResize(inModalResize);
    // On the falling edge (modal resize ends), force a composition refresh even if
    // bounds didn't change: the DComp surface was scaled/clipped during the drag,
    // and now needs to be rasterized at the final resolution.
    if (!inModalResize && CompositionActive()) {
        RefreshComposition(/*redrawContent*/ true, /*overlayForce*/ true);
    }
}

int TreeView::VisibleCapacity() const
{
    return std::max(0, static_cast<int>(bounds_.h / rowHeight_) + 1);
}

int TreeView::PageStep() const
{
    return std::max(1, static_cast<int>(bounds_.h / rowHeight_) - 1);
}

void TreeView::RebuildIndex()
{
    idToIndex_.clear();
    idToIndex_.reserve(items_.size());
    for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        idToIndex_[items_[i].id] = i;

    parentIndex_.assign(items_.size(), -1);
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        auto it = idToIndex_.find(items_[i].parentId);
        if (it != idToIndex_.end()) parentIndex_[i] = it->second;
    }
}

void TreeView::RebuildVisible()
{
    visibleRows_.clear();
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        bool isHidden = false;
        int parentIndex = (i < static_cast<int>(parentIndex_.size())) ? parentIndex_[i] : -1;
        while (parentIndex >= 0) {
            if (!items_[parentIndex].expanded) { isHidden = true; break; }
            parentIndex = (parentIndex < static_cast<int>(parentIndex_.size())) ? parentIndex_[parentIndex] : -1;
        }
        if (!isHidden) visibleRows_.push_back(i);
    }
}

int TreeView::RowIndexById(int id) const
{
    if (id < 0) return -1;
    auto it = idToIndex_.find(id);
    return it == idToIndex_.end() ? -1 : it->second;
}

int TreeView::VisibleIndexByRow(int rowIndex) const
{
    for (int i = 0; i < static_cast<int>(visibleRows_.size()); ++i)
        if (visibleRows_[i] == rowIndex) return i;
    return -1;
}

int TreeView::HitRow(float dipY) const
{
    // Use the EFFECTIVE offset (mid-tween in composition mode) so a click during a
    // smooth scroll hits the row actually under the cursor (roadmap §11.10).
    float y = dipY - bounds_.y + CurrentOffset();
    int visibleIndex = static_cast<int>(y / rowHeight_);
    return (visibleIndex >= 0 && visibleIndex < static_cast<int>(visibleRows_.size()))
        ? visibleRows_[visibleIndex]
        : -1;
}

void TreeView::ToggleRow(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(items_.size())) return;
    if (!items_[rowIndex].hasChildren) return;
    items_[rowIndex].expanded = !items_[rowIndex].expanded;
    int selectedId = (selectedIndex_ >= 0) ? items_[selectedIndex_].id : -1;
    RebuildVisible();
    selectedIndex_ = RowIndexById(selectedId);
    if (selectedIndex_ >= 0 && VisibleIndexByRow(selectedIndex_) < 0)
        selectedIndex_ = rowIndex;
    scroll_.SetContentHeight(rowHeight_ * visibleRows_.size());
    if (CompositionActive()) RefreshComposition(true, true);  // expand/collapse changed rows
    Invalidate();
}

void TreeView::OnPointerMoved(PointerEventArgs& e)
{
    float dipX = e.position.x, dipY = e.position.y;
    // scroll_ is an internal member with no invalidate callback of its own, so
    // its SetOffset can't repaint us — drive the repaint from here.
    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        if (CompositionActive()) {
            // Thumb drag is immediate (no glide, §11.8): push the dragged offset to
            // the content host and refresh the overlay thumb.
            content_->SetOffsetImmediate(scroll_.Offset(),
                [this](ID2D1DeviceContext* dc, float o, float h) { DrawRowsToSurface(dc, o, h); });
            // The thumb moved; content position changed but the rows are unchanged
            // (a covered refill, not a redraw). Overlay must redraw to track the thumb.
            RefreshComposition(false, true);
        }
        Invalidate(); e.handled = true; return;
    }
    // Any move over the control keeps the thin rail visible; nearing the right
    // edge expands it to a pill (WinUI-11 style).
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));
    // Hover/wake only affects the overlay (rail visibility/expand). The sig gate in
    // RefreshComposition still no-ops it if nothing actually moved.
    if (CompositionActive()) RefreshComposition(false, false);  // fade/hover changed the overlay
}

void TreeView::OnPointerPressed(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;
    float dipX = e.position.x, dipY = e.position.y;
    // Scrollbar drag: press anywhere in the hover strip (16 DIP), not just the
    // visible thumb (3-7 DIP). A rail that lights up on hover but ignores clicks
    // outside the narrow thumb reads as broken, and 3 DIP is not a real target.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        // Off-thumb presses first center the thumb on the pointer, then drag from
        // there. Without the jump the thumb would stay put and leap on the first
        // move, which feels like lag. With it the list jumps to roughly where the
        // pointer landed — consistent with how page up/down behave (they jump too).
        if (!scroll_.HitThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
            if (CompositionActive()) {
                content_->SetOffsetImmediate(scroll_.Offset(),
                    [this](ID2D1DeviceContext* dc, float o, float h) { DrawRowsToSurface(dc, o, h); });
                RefreshComposition(false, true);
            }
            Invalidate();
        }
        scroll_.BeginDrag(dipY);
        if (Context().input) Context().input->CapturePointer(this);
        if (CompositionActive()) RefreshComposition(false, true);  // thumb widened on drag start
        Invalidate();
        e.handled = true;
        return;
    }
    // Row press: capture so the matching release routes back to us even if the
    // pointer drifts, then act on release (chevron toggle or row select).
    if (Context().input) Context().input->CapturePointer(this);
    e.handled = true;
}

void TreeView::OnPointerReleased(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;
    bool wasDragging = scroll_.IsDragging();
    if (wasDragging) { scroll_.EndDrag(); Invalidate(); }
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    e.handled = true;
    if (wasDragging) return;

    // A row click only counts when released inside the control (drag-off cancels).
    float dipX = e.position.x, dipY = e.position.y;
    if (!bounds_.contains(dipX, dipY)) return;
    if (scroll_.HitBarRegion(dipX, dipY)) return;  // release on scrollbar region, not a row click
    int rowIndex = HitRow(dipY);
    if (rowIndex < 0) return;
    float chevronLeft = bounds_.x + 10.0f + items_[rowIndex].depth * 14.0f;
    if (items_[rowIndex].hasChildren && dipX >= chevronLeft && dipX < chevronLeft + 16.0f) {
        ToggleRow(rowIndex);
        return;
    }
    // Modifier-aware selection (Multiple mode only; Single mode ignores modifiers
    // so a Ctrl+click there behaves exactly as before).
    //
    // Ctrl wins over Shift when both are held: that is what Explorer and the VS
    // solution explorer do, and the alternative (Ctrl+Shift extending without
    // clearing) needs a second anchor concept to be well-defined.
    if (selectionMode_ == SelectionMode::Multiple) {
        const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
        if (ctrl) {
            ToggleRowSelection(rowIndex);
            return;
        }
        if (shift) {
            RangeSelectTo(rowIndex);
            return;
        }
        // Plain click falls through to SetSelectedIndex, whose OnSelectionChanged
        // hook collapses the set to this row and re-anchors — so no set handling is
        // needed here. Doing it here as well would raise SelectionSetChanged twice.
    }

    int visibleIndex = VisibleIndexByRow(rowIndex);
    if (visibleIndex >= 0) SetSelectedIndex(visibleIndex);
}

void TreeView::OnPointerLeft()
{
    // Pointer left the control entirely: drop bar hover so it shrinks + fades.
    scroll_.SetBarHover(false);
}

void TreeView::EnsureSelectedVisible()
{
    int visibleIndex = VisibleIndexByRow(selectedIndex_);
    if (visibleIndex < 0) return;
    float target = EnsureVisibleOffset(visibleIndex * rowHeight_, rowHeight_,
                                       CurrentOffset(), bounds_.h);
    if (CompositionActive()) {
        // Keyboard nav / selection: jump immediately (no long glide, §11.7).
        if (std::fabs(target - content_->EffectiveOffset()) > 0.5f) {
            content_->SetOffsetImmediate(target,
                [this](ID2D1DeviceContext* dc, float o, float h) { DrawRowsToSurface(dc, o, h); });
            RefreshComposition(false, true);  // offset jumped; thumb moved, rows unchanged
        }
    } else if (target != scroll_.Offset()) {
        scroll_.SetOffset(target);
    }
}

void TreeView::OnPointerWheelChanged(PointerEventArgs& e)
{
    if (scroll_.MaxOffset() <= 0.0f) return;  // nothing to scroll (bubbles up)
    // One wheel notch (WHEEL_DELTA = 120) scrolls 3 rows, matching the OS list
    // default. Wheel-up (positive delta) scrolls content up (offset decreases).
    // AnimateBy accumulates so spinning the wheel builds momentum; the window's
    // animation timer drives scroll_.Tick() until it settles.
    float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
    if (CompositionActive()) {
        // Compositor smooth scroll: retarget from the current visual offset so
        // spinning the wheel accumulates without a jump (roadmap §11.6). The tween
        // runs on the compositor thread; keep the scrollbar awake for the fade.
        content_->AnimateBy(lines * rowHeight_,
            [this](ID2D1DeviceContext* dc, float o, float h) { DrawRowsToSurface(dc, o, h); });
        scroll_.Wake();
        RefreshComposition(false, true);  // update thumb + overlay to the new target
    } else {
        scroll_.AnimateBy(lines * rowHeight_);
    }
    Invalidate();
    e.handled = true;
}

void TreeView::OnKeyDownRouted(KeyEventArgs& e)
{
    if (visibleRows_.empty()) return;
    int visibleIndex = VisibleIndexByRow(selectedIndex_);
    if (visibleIndex < 0) visibleIndex = 0;

    // --- Multi-select keyboard gestures (Multiple mode only) ----------------
    // Handled before the plain switch so Shift+Down extends the range instead of
    // moving the single selection. Ctrl+Space toggles the active row in place —
    // the one gesture that changes selection without moving the active row.
    if (selectionMode_ == SelectionMode::Multiple) {
        const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;

        if (ctrl && e.vk == VK_SPACE) {
            ToggleRowSelection(selectedIndex_);
            e.handled = true;
            return;
        }
        if (shift) {
            // Compute the target visible index for the navigation keys that extend
            // a range; anything else falls through to the normal switch.
            int targetVisible = -1;
            switch (e.vk) {
            case VK_UP:    targetVisible = std::max(0, visibleIndex - 1); break;
            case VK_DOWN:  targetVisible = std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + 1); break;
            case VK_HOME:  targetVisible = 0; break;
            case VK_END:   targetVisible = static_cast<int>(visibleRows_.size()) - 1; break;
            case VK_PRIOR: targetVisible = std::max(0, visibleIndex - PageStep()); break;
            case VK_NEXT:  targetVisible = std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + PageStep()); break;
            default: break;
            }
            if (targetVisible >= 0) {
                RangeSelectTo(visibleRows_[targetVisible]);
                EnsureSelectedVisible();  // RangeSelectTo does not scroll on its own
                e.handled = true;
                return;
            }
        }
    }

    switch (e.vk) {
    case VK_UP:
        SetSelectedIndex(std::max(0, visibleIndex - 1));
        e.handled = true; break;
    case VK_DOWN:
        SetSelectedIndex(std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + 1));
        e.handled = true; break;
    case VK_LEFT:
        if (selectedIndex_ >= 0 && items_[selectedIndex_].hasChildren && items_[selectedIndex_].expanded) {
            ToggleRow(selectedIndex_);
        } else if (selectedIndex_ >= 0 && items_[selectedIndex_].parentId >= 0) {
            int parent = RowIndexById(items_[selectedIndex_].parentId);
            int parentVisible = VisibleIndexByRow(parent);
            if (parentVisible >= 0) SetSelectedIndex(parentVisible);
        }
        e.handled = true; break;
    case VK_RIGHT:
        if (selectedIndex_ >= 0 && items_[selectedIndex_].hasChildren && !items_[selectedIndex_].expanded)
            ToggleRow(selectedIndex_);
        e.handled = true; break;
    case VK_HOME:
        SetSelectedIndex(0);
        e.handled = true; break;
    case VK_END:
        SetSelectedIndex(static_cast<int>(visibleRows_.size()) - 1);
        e.handled = true; break;
    case VK_PRIOR:
        SetSelectedIndex(std::max(0, visibleIndex - PageStep()));
        e.handled = true; break;
    case VK_NEXT:
        SetSelectedIndex(std::min(static_cast<int>(visibleRows_.size()) - 1, visibleIndex + PageStep()));
        e.handled = true; break;
    default:
        break;
    }
}

bool TreeView::MatchPrefix(const std::wstring& text, const std::wstring& prefix) const
{
    if (prefix.empty() || text.size() < prefix.size()) return false;
    return _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool TreeView::SelectByPrefix(const std::wstring& prefix)
{
    if (prefix.empty()) return false;
    int start = std::max(0, VisibleIndexByRow(selectedIndex_) + 1);
    for (int pass = 0; pass < 2; ++pass) {
        int begin = (pass == 0) ? start : 0;
        int end = (pass == 0) ? static_cast<int>(visibleRows_.size()) : start;
        for (int i = begin; i < end; ++i) {
            if (MatchPrefix(items_[visibleRows_[i]].text, prefix)) {
                SetSelectedIndex(i);
                return true;
            }
        }
    }
    return false;
}

void TreeView::OnTextInput(wchar_t ch)
{
    if (visibleRows_.empty() || ch < 0x20) return;
    DWORD now = GetTickCount();
    if (now - lastTypeTick_ > 1000) typeSearch_.clear();
    lastTypeTick_ = now;
    typeSearch_ += ch;

    // Search on the accumulated prefix. If nothing matches and we had more than
    // one character buffered, restart the search from just this key (a common
    // type-ahead behavior: typing a new letter that doesn't extend the current
    // match jumps to items starting with that letter). This is a direct retry —
    // NOT a recursive OnTextInput call, which previously re-appended ch and
    // looped forever (stack overflow).
    if (SelectByPrefix(typeSearch_)) return;
    if (typeSearch_.size() > 1) {
        typeSearch_.assign(1, ch);
        SelectByPrefix(typeSearch_);
    }
}

// ---------------------------------------------------------------------------
// Compositor overscan scrolling (Phase 3). All of this is a no-op when
// CompositionActive() is false — the control then uses the UI-thread scroll_ path.
// ---------------------------------------------------------------------------

float TreeView::CurrentOffset() const
{
    return CompositionActive() ? content_->EffectiveOffset() : scroll_.Offset();
}

bool TreeView::WantsAnimationTick() const
{
    if (!IsEffectivelyVisible()) return false;
    // Composition mode: the CONTENT tween runs on the compositor thread (no UI tick
    // needed for it), but we still tick to advance the scrollbar fade/expand and to
    // track the thumb + refill while a scroll settles. scroll_.NeedsTick() covers
    // the fade; content_->IsAnimating() keeps ticking the thumb during a fling.
    if (CompositionActive())
        return content_->IsAnimating() || scroll_.NeedsTick();
    return scroll_.NeedsTick();
}

void TreeView::OnAnimationTick(float dtSec)
{
    if (!IsEffectivelyVisible()) return;
    // Runs in PumpAnimations, OUTSIDE the window content frame — safe to refill
    // surfaces here. Advance the scrollbar fade (scroll_ owns only the visual state
    // in composition mode; its own smooth-scroll offset animation is unused there),
    // then reconcile the surfaces with the current effective offset.
    scroll_.Tick(dtSec);
    // Ticks never change the ROWS, so never force a content redraw here (that was the
    // per-frame re-rasterize that shimmered the frame). The overlay is redrawn only if
    // its sig actually moved this tick — i.e. while the bar is fading/expanding or the
    // thumb is tracking a settling fling; a stable idle-visible bar redraws nothing.
    if (CompositionActive()) RefreshComposition(false, false);
    else Invalidate();  // fallback path: scroll_ is a member whose Invalidate() cannot
                        // reach the tree, so fade/expand steps need this to repaint
}

void TreeView::DrawRowsToSurface(ID2D1DeviceContext* dc, float surfaceOriginDip,
                                 float surfaceHeightDip)
{
    if (!dc || !Dwrite()) return;
    IDWriteTextFormat* fmt = Dwrite()->Format(
        12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return;

    // The dc arrives with a Scale(dpi) transform (ScrollContentHost), so we draw in
    // DIPs with (0,0) at the surface's logical top (content-space surfaceOriginDip).
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf())))
        return;
    DrawingContext rc{dc, brush.Get(), Context().dpiScale};

    const float viewW = bounds_.w;  // surface spans the viewport width
    int first = std::max(0, static_cast<int>(surfaceOriginDip / rowHeight_));
    const float surfaceBottom = surfaceOriginDip + surfaceHeightDip;
    for (int i = first; i < static_cast<int>(visibleRows_.size()); ++i) {
        const float rowTopContent = i * rowHeight_;
        if (rowTopContent >= surfaceBottom) break;      // past the drawn surface
        const float localY = rowTopContent - surfaceOriginDip;  // 0 at surface top
        DrawRow(rc, fmt, visibleRows_[i], localY, 0.0f, viewW);
    }
}

void TreeView::DrawOverlayToSurface(ID2D1DeviceContext* dc, float /*viewportWDip*/,
                                    float /*viewportHDip*/)
{
    if (!dc) return;
    const ColorTokens& pal = Theme().colors;
    const float s = Context().dpiScale;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf())))
        return;
    // scroll_.Render + the focus ring draw in WINDOW DIPs (bounds_-relative). Map
    // window DIP → viewport-local pixels: (dip - bounds.origin) * dpi. In D2D row-
    // vector order that is Translation(-bounds) then Scale(dpi).
    //
    // PREMULTIPLY onto the incoming transform — it carries the composition surface's
    // atlas tile origin, which changes between draws. Replacing it puts this drawing
    // outside our tile (clipped away / into a neighbour), which is what made the
    // scrollbar frame flicker whenever the overlay re-rasterized (resize, wheel tween).
    D2D1_MATRIX_3X2_F base;
    dc->GetTransform(&base);
    dc->SetTransform(
        SurfaceTransformFromWindowDip(base, bounds_.x, bounds_.y, s));
    DrawingContext rc{dc, brush.Get(), s};
    scroll_.Render(rc);
    if (IsFocused()) {
        // Inset by 0.5 DIP, matching the non-composited path. The 1px stroke is centered
        // on this rect, so half extends inward and half outward — the outward half
        // (0.5 DIP) stays within the viewport clip. Previously 1.5 DIP, which was too
        // conservative and left content visibly extending past the ring at the bottom.
        rc.DrawRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(bounds_.x + 0.5f, bounds_.y + 0.5f,
                                          bounds_.right() - 0.5f, bounds_.bottom() - 0.5f),
                              4.0f, 4.0f),
            D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.7f), 1.0f);
    }
}

OverlaySignature TreeView::CurrentOverlaySig() const
{
    // Everything the overlay surface's pixels depend on. Sampled AFTER the scroll
    // model's offset/geometry is updated (thumbY tracks the current offset), so
    // callers must sync scroll_ before comparing. When none of these move, the
    // overlay is pixel-identical and must NOT be re-rasterized (see RefreshComposition).
    OverlaySignature s;
    s.visibility = scroll_.Visibility();
    s.expand     = scroll_.ExpandFactor();
    s.thumbY     = scroll_.ThumbRect().y;
    s.focused    = IsFocused();
    s.dragging   = scroll_.IsDragging();
    return s;
}

void TreeView::RefreshComposition(bool redrawContent, bool overlayForce)
{
    if (!CompositionActive()) return;
    if (!IsEffectivelyVisible()) {
        content_->SetTreeVisible(false);
        return;
    }
    content_->SetTreeVisible(true);
    const float extent = rowHeight_ * visibleRows_.size();
    // In composited mode Render() returns early, so an opacity folded into the
    // host's DrawingContext never reaches these pixels — push it onto the
    // composition sub-tree instead. EffectiveOpacity() (not Opacity()) because an
    // ancestor panel's opacity is likewise invisible to this path.
    // Visibility is handled by root membership above; opacity remains an independent
    // appearance property and is only pushed while the tree is visible.
    content_->SetOpacity(EffectiveOpacity());
    // Full bounds as the viewport, ancestor clip supplied SEPARATELY — same contract
    // as TextArea: a D2D container clip cannot reach a DComp visual, and the host
    // needs the clip as its own operand to place it relative to the true position.
    RectDip ancestorClip;
    if (AncestorViewportClip(ancestorClip)) content_->SetAncestorClip(ancestorClip);
    else content_->ClearAncestorClip();
    // Defer surface rasterization while a resize border is held — same BeginDraw
    // reasoning as TextArea. Rows are fixed-height and width-independent, so the
    // deferred pixels stay valid for the whole drag.
    content_->SetModalResize(Context().inModalResize);
    content_->SetViewport(bounds_);
    content_->SetContentHeight(extent);
    // Keep the scrollbar model's geometry + extent in sync so ThumbRect / hit-test
    // stay correct, and point its thumb at the CURRENT effective offset.
    scroll_.SetBounds(bounds_);
    scroll_.SetContentHeight(extent);
    scroll_.SetOffset(content_->EffectiveOffset());

    auto drawRows = [this](ID2D1DeviceContext* dc, float o, float h) {
        DrawRowsToSurface(dc, o, h);
    };
    // Content surface: re-rasterize only when the ROWS themselves changed (selection,
    // items, expand/collapse, theme, bounds) — passed as redrawContent. When false,
    // EnsureContent still refills if a fling drifted near a buffer edge, but it does
    // NOT redraw a covered, unchanged surface. This is what stops the per-tick redraw
    // that shimmered the frame while the scrollbar was idle-visible.
    content_->EnsureContent(drawRows, /*forceRedraw=*/redrawContent);

    // Overlay surface (scrollbar + focus ring): re-rasterize only when its visual
    // determinants actually moved. A static idle-visible bar has an unchanged sig, so
    // the fade-countdown ticks no longer re-rasterize it every frame (the reported
    // edge flicker). overlayForce covers changes not captured by the sig timing
    // (focus toggles, theme, a fresh surface after resize/DPI/device-restore).
    const OverlaySignature sig = CurrentOverlaySig();
    if (overlayForce || sig.Differs(lastOverlaySig_)) {
        content_->RedrawOverlay([this](ID2D1DeviceContext* dc, float w, float h) {
            DrawOverlayToSurface(dc, w, h);
        });
        lastOverlaySig_ = sig;
    }
    if (WindowServices* win = Window())
        if (ICompositionBackend* comp = win->Composition()) comp->RequestCommit();
}

void TreeView::OnAttachedToTree()
{
    // scroll_ is an embedded model, not a tree node, so nothing else would ever give
    // it a UIContext — and UIElement::Theme() falls back to a LIGHT default snapshot
    // when context_.theme is null. The rail was therefore drawn with light-theme ink
    // (textPrimary = near-black) regardless of the real theme, which is invisible on
    // a dark background. Forwarding the context is what puts it on the actual theme,
    // and it keeps working across a theme switch because the host overwrites its
    // snapshot in place. Same fix as ScrollPanel.
    scroll_.AttachToContext(Context());
    // Match ScrollPanel: a rail that never fully disappears while the content
    // overflows, so "there is more to see" stays discoverable instead of fading out.
    scroll_.SetKeepVisibleWhenOverflow(true);

    // Create the compositor scroll host if the window offers a backend; otherwise
    // fall back to the UI-thread scroll_ path (content_ stays null).
    if (WindowServices* win = Window()) {
        if (ICompositionBackend* comp = win->Composition()) {
            content_ = std::make_unique<ScrollContentHost>();
            content_->SetTreeVisible(IsEffectivelyVisible());
            if (SUCCEEDED(content_->Create(comp, Context().dpiScale))) {
                RefreshComposition(true, true);
            } else {
                content_.reset();  // creation failed → fallback
            }
        }
    }
    Invalidate();
}

void TreeView::OnDetachedFromTree()
{
    // Tear the compositor host down while the backend is still valid.
    if (content_) { content_->Destroy(); content_.reset(); }
    // Clear the scroll model's context — it is no longer valid.
    scroll_.DetachFromContext();
}

void TreeView::OnFocusChanged()
{
    if (CompositionActive()) RefreshComposition(false, true);  // focus ring lives on the overlay
    Invalidate();
}

void TreeView::OnOpacityChanged(float)
{
    // Composition path only: the visual's own opacity property is pushed straight
    // through (no surface redraw). In fallback mode the base class's Render-dirty
    // flag is enough — the host repaints through a faded DrawingContext.
    if (CompositionActive()) {
        // Hidden hosts cache this value without committing and apply it on reveal.
        content_->SetOpacity(EffectiveOpacity());
    }
}

void TreeView::OnVisibilityChanged(bool visible)
{
    UNREFERENCED_PARAMETER(visible);
    UpdateCompositionVisibility();
}

void TreeView::OnAncestorVisibilityChanged()
{
    UpdateCompositionVisibility();
}

void TreeView::UpdateCompositionVisibility()
{
    if (!CompositionActive()) return;
    if (!IsEffectivelyVisible()) {
        content_->SetTreeVisible(false);
        return;
    }
    content_->SetTreeVisible(true);
    RefreshComposition(true, true);
}

void TreeView::OnThemeChanged()
{
    if (CompositionActive()) RefreshComposition(true, true);  // redraw surfaces with new colors
    Invalidate();
}

void TreeView::OnDpiChanged(float dpiScale)
{
    if (CompositionActive()) {
        content_->SetDpiScale(dpiScale);
        RefreshComposition(true, true);
    }
    Invalidate();
}

void TreeView::OnDeviceLost()
{
    if (content_) content_->OnDeviceLost();  // drop visuals, keep scroll position
}

void TreeView::OnDeviceRestored()
{
    if (content_) {
        if (WindowServices* win = Window()) {
            if (ICompositionBackend* comp = win->Composition()) {
                content_->OnDeviceRestored(comp, Context().dpiScale);
                RefreshComposition(true, true);
            }
        }
    }
    Invalidate();
}

void TreeView::DrawRow(const DrawingContext& dc, IDWriteTextFormat* fmt,
                       int rowIndex, float rowTopY, float leftX, float rightX)
{
    const ColorTokens& pal = Theme().colors;
    const TreeViewRow& row = items_[rowIndex];
    const float y = rowTopY;
    D2D1_RECT_F rr = D2D1::RectF(leftX + 6.0f, y + 1.0f, rightX - 8.0f, y + rowHeight_ - 2.0f);
    const float itemCorner = Theme().spacing.cornerRadiusSmall;

    // Every selected row gets the fill; only the ACTIVE row (selectedIndex_) gets
    // the left accent bar. In Single mode the two coincide, so this renders exactly
    // as before. In Multiple mode the bar is what tells the user which row the next
    // Shift+arrow will extend from — without it a multi-row selection gives no clue
    // where the keyboard is.
    const bool rowIsSelected = IsRowSelected(rowIndex);
    if (rowIsSelected) {
        dc.FillRoundedRect(D2D1::RoundedRect(rr, itemCorner, itemCorner),
                           D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.18f));
    }
    if (rowIndex == selectedIndex_) {
        dc.FillRoundedRect(D2D1::RoundedRect(D2D1::RectF(rr.left, rr.top + 5, rr.left + 3, rr.bottom - 5), 2.0f, 2.0f),
                           pal.accent);
    }

    float indent = 10.0f + row.depth * 14.0f;
    if (row.hasChildren) {
        D2D1_COLOR_F chevron = EffectiveForeground(
            row.dimmed ? pal.textSecondary : pal.textPrimary);
        D2D1_POINT_2F p1, p2, p3;
        float cx = leftX + indent + 5.0f;
        float cy = y + rowHeight_ * 0.5f;
        if (row.expanded) {
            p1 = D2D1::Point2F(cx - 4.0f, cy - 2.0f);
            p2 = D2D1::Point2F(cx + 4.0f, cy - 2.0f);
            p3 = D2D1::Point2F(cx, cy + 3.0f);
        } else {
            p1 = D2D1::Point2F(cx - 2.0f, cy - 4.0f);
            p2 = D2D1::Point2F(cx - 2.0f, cy + 4.0f);
            p3 = D2D1::Point2F(cx + 3.0f, cy);
        }
        dc.DrawLine(p1, p2, chevron, 1.2f);
        dc.DrawLine(p2, p3, chevron, 1.2f);
        dc.DrawLine(p3, p1, chevron, 1.2f);
    }

    DrawIcon(dc, row.icon, leftX + indent + 16.0f, y + 7.0f, pal);

    // Row text is the neutral text color, not the icon's accent. Selected rows use
    // the text color for clear contrast on the selection fill.
    D2D1_COLOR_F textColor = EffectiveForeground(
        row.dimmed ? pal.textSecondary : pal.textPrimary);
    if (rowIsSelected) textColor = EffectiveForeground(pal.textPrimary);
    dc.DrawText(row.text.c_str(), static_cast<UINT32>(row.text.size()), fmt,
                D2D1::RectF(leftX + indent + 34.0f, y, rightX - 12.0f, y + rowHeight_),
                textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void TreeView::Render(const DrawingContext& dc)
{
    if (!Dwrite()) return;

    const ColorTokens& pal = Theme().colors;

    // Composition mode: the rows live on the overscan surface and the scrollbar /
    // focus ring on the overlay surface, both composited above the window content
    // by the compositor. Render must NOT draw a surface here (we are inside the
    // window content frame — a nested BeginDraw would fail), so it only keeps the
    // surfaces current for a stalled UI catching up on its next paint. The actual
    // refill happens outside the content frame; here we just request it lazily.
    if (CompositionActive()) {
        // A repaint reached us (selection/hover/theme) — refresh the surfaces from
        // outside-the-frame safe state on the next tick/refresh. We can, however,
        // safely re-run the offset application + overlay redraw scheduling.
        return;
    }

    IDWriteTextFormat* fmt = Dwrite()->Format(
        12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return;

    // Scoped to a block so the clip pops before the scrollbar / focus ring draw.
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

        int first = static_cast<int>(scroll_.Offset() / rowHeight_);
        int visible = VisibleCapacity();
        float y = bounds_.y - (scroll_.Offset() - first * rowHeight_);
        for (int n = 0; n < visible && first + n < static_cast<int>(visibleRows_.size()); ++n) {
            DrawRow(dc, fmt, visibleRows_[first + n], y, bounds_.x, bounds_.right());
            y += rowHeight_;
        }
    }

    scroll_.Render(dc);

    // Focus ring: a subtle accent outline so keyboard focus is visible.
    if (IsFocused()) {
        const auto accent = EffectiveAccentColor(pal.accent);
        dc.DrawRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(bounds_.x + 0.5f, bounds_.y + 0.5f,
                                          bounds_.right() - 0.5f, bounds_.bottom() - 0.5f),
                              4.0f, 4.0f),
            D2D1::ColorF(accent.r, accent.g, accent.b, 0.7f), 1.0f);
    }
}

} // namespace fluent
