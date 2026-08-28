// ItemsControl.h — template base for controls that own a list of items (WP-06).
//
// ItemsControl<TItem> gives any Control a typed items vector with a change hook
// so subclasses (Selector, ComboBox, TreeView) share one definition of the item
// store instead of each declaring its own std::vector<TItem>.  The template is
// header-only (no .cpp); each instantiation is compiled into the TU that first
// uses it, which is fine for the small number of concrete TItem types here.
#pragma once

#include "../../core/Control.h"
#include <vector>

namespace fluent {

template <class TItem>
class ItemsControl : public Control {
public:
    // Replace the item collection.  The base stores the vector and calls the
    // OnItemsChanged hook so subclasses can rebuild derived state (indices,
    // visible rows, listView sync …).  Passed by value so the caller can move.
    void SetItems(std::vector<TItem> items) {
        items_ = std::move(items);
        OnItemsChanged();
    }

    const std::vector<TItem>& Items() const { return items_; }

    // The number of items the control addresses. VIRTUAL because a virtualized
    // control's count is not items_.size(): ListBox in provider mode holds no
    // strings at all and answers from an item count plus a text callback. The
    // base's own selection clamp (Selector::ClampIndex) asks through this, so a
    // non-virtual version silently clamped every virtual-mode selection to -1.
    virtual int ItemCount() const { return static_cast<int>(items_.size()); }

    // Bounds-checked item accessor.  Returns nullptr on out-of-range index.
    const TItem* ItemAt(int i) const {
        if (i < 0 || i >= static_cast<int>(items_.size())) return nullptr;
        return &items_[i];
    }

protected:
    // Called after items_ is updated by SetItems.  Override to rebuild derived
    // state (tree indices, scroll height, listView reference sync …).
    virtual void OnItemsChanged() {}

    std::vector<TItem> items_;
};

} // namespace fluent
