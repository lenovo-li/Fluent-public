// Canvas.h — absolute-positioning container.
//
// Children are positioned via static attached properties Canvas::SetLeft /
// Canvas::SetTop / Canvas::SetRight / Canvas::SetBottom. Each child measures
// with infinite constraint (self-determined size) and is arranged at the
// absolute position given by its attached properties.
//
// Positioning rules:
// - If only Left is set, child's left edge is at Left, right edge is Left + width.
// - If only Right is set, child's right edge is at (canvas width - Right).
// - If both Left and Right are set, Right takes precedence (child is positioned
//   from the right edge).
// - Same logic applies to Top/Bottom: if both are set, Bottom takes precedence.
// - ZIndex controls paint order: higher ZIndex paints on top of lower ZIndex.
//   Children with the same ZIndex paint in the order they appear in the children list.
//
// Right/Bottom default to NaN rather than 0 because 0 is a legitimate value
// ("flush against the edge") and so cannot double as "unset". NaN is what lets
// ArrangeOverride tell the two apart, and it is the only reason the precedence
// rule above is expressible at all.
//
// The Canvas itself reports (0, 0) from MeasureOverride unless Width/Height are
// explicitly set — it doesn't constrain or aggregate children's bounds.
//
// Attached properties live in a static unordered_map keyed by UIElement*, so they
// work on any element (not just children of one Canvas). The map entry is erased
// when the element is destroyed — see core/AttachedProperties.h for why that
// matters: a raw pointer key is reused by whatever the allocator puts at that
// address next, and inheriting a stale Right would silently flip a fresh child
// from left- to right-anchored.
//
// Use cases: overlays, drag handles, custom-positioned UI layers, design surfaces
// where controls are placed by coordinates.
#pragma once

#include "Panel.h"
#include <unordered_map>
#include <cstdint>
#include <limits>

namespace fluent {

class Canvas : public Panel {
public:
    Canvas() = default;
    ~Canvas();  // Clears positions_ entries for all children to prevent address reuse bugs

    // Attached properties for child positioning (static — apply to any UIElement,
    // not just children of this Canvas instance).
    static void SetLeft(UIElement* element, float dip);
    static void SetTop(UIElement* element, float dip);
    static void SetRight(UIElement* element, float dip);
    static void SetBottom(UIElement* element, float dip);
    static void SetZIndex(UIElement* element, int z);
    static float GetLeft(UIElement* element);   // default 0.0
    static float GetTop(UIElement* element);    // default 0.0
    static float GetRight(UIElement* element);  // default NaN (not set)
    static float GetBottom(UIElement* element); // default NaN (not set)
    static int GetZIndex(UIElement* element);   // default 0

    // Drop every stored attached property for `element`. Called automatically when
    // an element is destroyed (see core/AttachedProperties.h); also useful to reset
    // a recycled element back to defaults by hand.
    static void ClearPositions(UIElement* element);

    // Both are public to match Panel — narrowing an override's access is legal C++
    // but severs every call made through the static type, and the tests hit-test a
    // Canvas& directly.
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    // Drop routing follows the same ZIndex order as hit-testing. Without this a
    // child *underneath* another could claim a drop that visually landed on the
    // one on top.
    UIElement* HitTestDropTarget(float dipX, float dipY) override;

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;

private:
    // Static storage for attached properties. Key: element pointer. Value:
    // {left, top, right, bottom, zIndex}. Right/Bottom default to NaN (not set).
    // When both Left and Right are set, Right takes precedence for positioning.
    // When both Top and Bottom are set, Bottom takes precedence for positioning.
    struct Position {
        float left = 0.0f;
        float top = 0.0f;
        float right = std::numeric_limits<float>::quiet_NaN();
        float bottom = std::numeric_limits<float>::quiet_NaN();
        int zIndex = 0;
    };
    static std::unordered_map<UIElement*, Position> positions_;

    // Bumped by SetZIndex. A Canvas compares it against the value its cache was
    // built at to know whether the order it has is still current. Global rather
    // than per-Canvas because SetZIndex is static and cannot name the Canvas the
    // element belongs to; over-invalidating every Canvas on any ZIndex change is
    // fine, since a ZIndex change is rare and the rebuild is a sort of the child
    // list.
    static uint64_t zIndexGeneration_;

    // Children in ascending ZIndex order (stable, so equal ZIndex keeps insertion
    // order). Render walks it forwards, hit-testing backwards.
    //
    // Cached rather than rebuilt per call because Render runs once a frame and
    // HitTestDeep runs once per mouse-move: sorting a fresh vector in both would
    // put a heap allocation on the two hottest paths in the framework. Rebuilt
    // only when the ZIndex generation moves or the child list changes size — NOT
    // on a visibility change, since visibility is checked while iterating and so
    // does not affect the stored order.
    void EnsureZOrder() const;
    mutable std::vector<FrameworkElement*> zOrder_;
    mutable uint64_t zOrderGeneration_ = 0;
    mutable size_t zOrderChildCount_ = static_cast<size_t>(-1);
};

} // namespace fluent
