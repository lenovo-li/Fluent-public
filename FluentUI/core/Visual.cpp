// Visual.cpp — see Visual.h.

#include "Visual.h"

namespace fluent {

void Visual::InvalidateDirty(DirtyFlags flags) {
    // Accumulate the closure of the requested flags on this visual so a change
    // records exactly what it dirtied (Measure -> Arrange -> Render).
    dirty_ |= ExpandDirty(flags);

    // A Measure invalidation means the desired size may have changed, so any
    // subclass measure cache must be dropped (roadmap §6.2). Visual keeps no
    // measure state itself, so it delegates to the hook UIElement overrides —
    // keeping the layer boundary clean.
    if (Has(flags, DirtyFlags::Measure)) OnMeasureInvalidated();

    // Request a frame: prefer this visual's own callback; otherwise walk toward
    // the root until we find one. Setting the callback on the tree root is enough
    // for the whole tree — nested elements need no invalidate hook of their own.
    if (invalidateCb_) {
        invalidateCb_(invalidateCtx_);
        return;
    }
    if (parent_) parent_->InvalidateDirty(DirtyFlags::None);
}

} // namespace fluent
