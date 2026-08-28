// Selector.h — template base for selection-capable items controls (WP-06).
//
// Selector<TItem> owns two layers of selection state:
//
//   Single selection — a selectedIndex_ with SelectedIndex()/SetSelectedIndex()
//   (clamp + change-dedup), SelectedItem() reaching into items_, and an
//   OnSelectionChanged() hook the subclass uses to raise its own typed Event and
//   run side effects (sync a list view, scroll a row into view).
//
//   Multiple selection — a selectionMode_ policy, a selectedIndices_ set, and a
//   selectionAnchor_ for Shift-range gestures, with SelectedIndices() /
//   IsSelected() / ToggleSelection() / RangeSelectTo(). Side effects go through
//   the OnSelectionSetChanged() hook.
//
// The selection index addresses items_ directly (a *data* index). TreeView keeps
// its own visible-index-vs-row-index mapping by overriding SetSelectedIndex — the
// base is still the single owner of the storage and the dedup logic.
//
// Why the multi-select state lives here rather than in each list control:
// TreeView grew it privately first, and a second list control (ListBox) would
// have had to write it a second time — with its own subtly different answers to
// "what does a plain click do to the set", "where does the anchor go on Ctrl
// +click", and "what happens to the set when the items are replaced". Three
// controls would have meant three dialects of the same gesture. The set is the
// authoritative selection in Multiple mode and selectedIndex_ tracks the
// most-recently-touched ("active") index for keyboard navigation, in both.
//
// Header-only like ItemsControl (no .cpp).
#pragma once

#include "ItemsControl.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace fluent {

// Selection policy shared by every Selector. Single is the classic one-index
// selection; Multiple allows Ctrl/Shift to build a set.
enum class SelectionMode { Single, Multiple };

template <class TItem>
class Selector : public ItemsControl<TItem> {
public:
    int SelectedIndex() const { return selectedIndex_; }

    // Set the selected index. Clamps to [-1, count) and is a no-op when unchanged.
    // On a real change it stores the index, collapses the multi-select set onto it
    // (see below), runs the subclass hook, and repaints. Virtual so TreeView can
    // remap a *visible* index onto a row index before delegating to base storage.
    virtual void SetSelectedIndex(int index) {
        int next = ClampIndex(index);
        if (next == selectedIndex_) return;
        int prev = selectedIndex_;
        selectedIndex_ = next;

        // In Multiple mode a plain SetSelectedIndex (arrow key, type-ahead, or a
        // programmatic set) means "select exactly this index" — the same thing a
        // plain click means. The collapse happens HERE, in the base, rather than
        // in each subclass's OnSelectionChanged override: this is the one place
        // every non-modifier selection change funnels through, so the set cannot
        // be left holding indices from a previous gesture, and no subclass has to
        // remember to do it. ToggleSelection / RangeSelectTo deliberately do NOT
        // route through here — they write selectedIndex_ directly, precisely so
        // this collapse does not undo the set they just built.
        if (selectionMode_ == SelectionMode::Multiple) {
            selectedIndices_.clear();
            if (next >= 0) selectedIndices_.insert(next);
            selectionAnchor_ = next;
        }

        OnSelectionChanged(prev, next);
        this->Invalidate();
    }

    // The selected item, or nullptr when nothing is selected / out of range.
    const TItem* SelectedItem() const { return this->ItemAt(selectedIndex_); }

    // --- Multiple selection -------------------------------------------------

    // Switching modes reconciles the two representations rather than leaving one
    // stale, because both are read by rendering and by the public accessors:
    //   → Multiple: seed the set from the current single selection, so the index
    //     the user already had selected stays selected instead of the set
    //     starting empty.
    //   → Single: drop the set. selectedIndex_ (the active index) survives, so
    //     the result is "the item you last touched stays selected", which is the
    //     only choice that needs no arbitrary pick among several selected items.
    void SetSelectionMode(SelectionMode mode) {
        if (selectionMode_ == mode) return;
        selectionMode_ = mode;
        selectedIndices_.clear();
        if (mode == SelectionMode::Multiple) {
            if (selectedIndex_ >= 0) selectedIndices_.insert(selectedIndex_);
            selectionAnchor_ = selectedIndex_;
        } else {
            selectionAnchor_ = -1;
        }
        this->Invalidate();
    }
    SelectionMode GetSelectionMode() const { return selectionMode_; }

    // The selected indices, ascending. In Single mode this is the single
    // selection (or empty), so a caller that only ever reads this accessor works
    // unchanged in both modes.
    std::vector<int> SelectedIndices() const {
        if (selectionMode_ == SelectionMode::Single)
            return (selectedIndex_ >= 0) ? std::vector<int>{selectedIndex_}
                                         : std::vector<int>{};
        // Sorted for a predictable iteration order — unordered_set has none.
        std::vector<int> out(selectedIndices_.begin(), selectedIndices_.end());
        std::sort(out.begin(), out.end());
        return out;
    }

    bool IsSelected(int index) const {
        if (selectionMode_ == SelectionMode::Single) return index == selectedIndex_;
        return selectedIndices_.count(index) > 0;
    }

    // Toggle one index's membership (Multiple mode only; no-op in Single). The
    // toggled index becomes both the active index and the new anchor, so a
    // following Shift+click extends from where the Ctrl+click landed.
    void ToggleSelection(int index) {
        if (selectionMode_ != SelectionMode::Multiple) return;
        if (index < 0 || index >= this->ItemCount()) return;
        if (selectedIndices_.count(index)) selectedIndices_.erase(index);
        else                              selectedIndices_.insert(index);
        selectedIndex_ = index;
        selectionAnchor_ = index;
        OnSelectionSetChanged();
        this->Invalidate();
    }

    // Replace the set with the inclusive range [anchor, index] (Multiple mode
    // only). The anchor stays put, so a second Shift+click extends from the same
    // origin rather than from the previous target.
    void RangeSelectTo(int index) {
        if (selectionMode_ != SelectionMode::Multiple) return;
        if (index < 0 || index >= this->ItemCount()) return;
        const int anchor = (selectionAnchor_ >= 0) ? selectionAnchor_ : index;
        const int lo = std::min(anchor, index);
        const int hi = std::max(anchor, index);
        selectedIndices_.clear();
        for (int i = lo; i <= hi; ++i) selectedIndices_.insert(i);
        selectedIndex_ = index;  // the end of the range becomes the active index
        OnSelectionSetChanged();
        this->Invalidate();
    }

protected:
    // Clamp an arbitrary index to the legal selection range [-1, count).
    int ClampIndex(int index) const {
        int count = this->ItemCount();
        if (index < -1) return -1;
        if (index > count - 1) return count - 1;
        return index;
    }

    // Called after selectedIndex_ actually changes. Subclass raises its Event<>
    // and runs side effects (list-view sync, ensure-visible). Default no-op.
    virtual void OnSelectionChanged(int /*oldIndex*/, int /*newIndex*/) {}

    // Called after the multi-select SET changes without going through
    // SetSelectedIndex (i.e. from ToggleSelection / RangeSelectTo). Subclass
    // raises its set-changed Event<> and runs side effects. Default no-op.
    virtual void OnSelectionSetChanged() {}

    // Re-seed the set from the current selectedIndex_. For subclasses whose
    // OnItemsChanged replaces items_ wholesale: the old set held indices into
    // the PREVIOUS items_, where the same index now means a different item (or
    // none). Re-seeding rather than only clearing keeps the invariant that in
    // Multiple mode the set agrees with selectedIndex_ — leaving it empty
    // produces an active item that IsSelected() reports as unselected, and the
    // disagreement persists until the next click.
    void ReseedSelectionSet() {
        selectedIndices_.clear();
        if (selectionMode_ == SelectionMode::Multiple && selectedIndex_ >= 0)
            selectedIndices_.insert(selectedIndex_);
        selectionAnchor_ =
            (selectionMode_ == SelectionMode::Multiple) ? selectedIndex_ : -1;
    }

    // Selection storage owned here so subclasses never redeclare it. Using
    // `this->items_` from the dependent base requires the this-> qualification.
    int selectedIndex_ = -1;

    // Multi-select state. In Single mode selectedIndex_ is the only selection and
    // selectedIndices_ stays empty; in Multiple mode selectedIndices_ is
    // authoritative and selectedIndex_ tracks the most-recently-touched index.
    SelectionMode selectionMode_ = SelectionMode::Single;
    std::unordered_set<int> selectedIndices_;
    int selectionAnchor_ = -1;  // origin index for Shift range-select
};

} // namespace fluent
