// ScrollMath.h — pure scroll-offset helpers shared by list-like controls (WP-06).
//
// ComboBox's dropdown list and TreeView both carried a byte-identical
// "scroll the given item into the viewport" computation (ComboListView::EnsureVisible
// and TreeView::EnsureSelectedVisible). EnsureVisibleOffset extracts that math as a
// pure function so both call sites share one definition (and it is unit-testable
// with no window). The caller still owns clamping to its own [0, maxScroll] range,
// because the two controls clamp against different content extents.
#pragma once

#include <cmath>

namespace fluent {

// Return the scroll offset needed to bring the item spanning
// [itemTop, itemTop + itemH) fully into a viewport of height viewH whose current
// top is viewTop. If the item is above the viewport, scroll up to its top; if
// below, scroll down so its bottom aligns with the viewport bottom; otherwise the
// offset is unchanged (already visible). The result is NOT clamped — the caller
// clamps to its own [0, maxScroll].
inline float EnsureVisibleOffset(float itemTop, float itemH,
                                 float viewTop, float viewH) {
    if (itemTop < viewTop) return itemTop;                 // item above view
    float itemBottom = itemTop + itemH;
    if (itemBottom > viewTop + viewH) return itemBottom - viewH;  // item below view
    return viewTop;                                        // already visible
}

} // namespace fluent
