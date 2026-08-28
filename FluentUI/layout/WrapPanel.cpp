// WrapPanel.cpp

#include "WrapPanel.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
// The child's main-axis footprint including its margin. ItemWidth/ItemHeight,
// when set, replace the measured content size — that is what makes a tile grid
// uniform even when the tiles' natural sizes differ.
float MainFootprint(const FrameworkElement* c, bool horizontal,
                    const std::optional<float>& itemMain) {
    const Thickness& m = c->Margin();
    float margin = horizontal ? m.horizontal() : m.vertical();
    if (itemMain.has_value()) return *itemMain + margin;
    const SizeDip& d = c->Desired();
    return (horizontal ? d.w : d.h) + margin;
}

float CrossFootprint(const FrameworkElement* c, bool horizontal,
                     const std::optional<float>& itemCross) {
    const Thickness& m = c->Margin();
    float margin = horizontal ? m.vertical() : m.horizontal();
    if (itemCross.has_value()) return *itemCross + margin;
    const SizeDip& d = c->Desired();
    return (horizontal ? d.h : d.w) + margin;
}
} // namespace

void WrapPanel::SetOrientation(Orientation o) {
    if (orientation_ == o) return;
    orientation_ = o;
    InvalidateDirty(DirtyFlags::Measure);
}

void WrapPanel::SetItemWidth(float dip) {
    if (itemWidth_.has_value() && *itemWidth_ == dip) return;
    itemWidth_ = dip;
    InvalidateDirty(DirtyFlags::Measure);
}

void WrapPanel::SetItemHeight(float dip) {
    if (itemHeight_.has_value() && *itemHeight_ == dip) return;
    itemHeight_ = dip;
    InvalidateDirty(DirtyFlags::Measure);
}

// Walk children in order, accumulating main-axis footprints until the next one
// would overflow `availMain`, then start a new line. A child wider than the
// whole line gets a line to itself rather than being dropped — matching WPF,
// where an oversized item overflows its line instead of vanishing.
float WrapPanel::ComputeLines(float availMain, float availCross) {
    UNREFERENCED_PARAMETER(availCross);
    const bool h = horizontal();
    const std::optional<float>& itemMain = h ? itemWidth_ : itemHeight_;
    const std::optional<float>& itemCross = h ? itemHeight_ : itemWidth_;

    lines_.clear();
    float totalCross = 0.0f;

    Line cur{0, 0, 0.0f, 0.0f};
    int index = 0;
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) { ++index; continue; }

        float mainSz = MainFootprint(c, h, itemMain);
        float crossSz = CrossFootprint(c, h, itemCross);

        // Break before this child if the line already holds something and
        // adding this child would exceed the available main extent. An
        // unbounded (non-finite or zero) availMain never wraps.
        const bool boundedMain = availMain > 0.0f && std::isfinite(availMain);
        if (cur.endIndex > cur.startIndex && boundedMain && cur.mainSize + mainSz > availMain) {
            lines_.push_back(cur);
            totalCross += cur.crossSize;
            cur = Line{index, index, 0.0f, 0.0f};
        }

        if (cur.endIndex == cur.startIndex) cur.startIndex = index;
        cur.endIndex = index + 1;
        cur.mainSize += mainSz;
        cur.crossSize = std::max(cur.crossSize, crossSz);
        ++index;
    }

    if (cur.endIndex > cur.startIndex) {
        lines_.push_back(cur);
        totalCross += cur.crossSize;
    }

    return totalCross;
}

SizeDip WrapPanel::MeasureOverride(float availW, float availH) {
    const bool h = horizontal();
    const std::optional<float>& itemMain = h ? itemWidth_ : itemHeight_;
    const std::optional<float>& itemCross = h ? itemHeight_ : itemWidth_;

    // Measure every child first. Each child is offered at most one line's worth
    // of main extent (ItemWidth/ItemHeight when fixed, otherwise the panel's own
    // available main extent — a child cannot be wider than a full line).
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        const Thickness& m = c->Margin();
        float offerW = itemWidth_.has_value()
            ? *itemWidth_
            : std::max(0.0f, availW - m.horizontal());
        float offerH = itemHeight_.has_value()
            ? *itemHeight_
            : std::max(0.0f, availH - m.vertical());
        c->MeasureCached(offerW, offerH);
    }

    float availMain = h ? availW : availH;
    float availCross = h ? availH : availW;
    float totalCross = ComputeLines(availMain, availCross);

    // The panel's main extent is the widest line, not the available space: an
    // auto-sized WrapPanel inside a ScrollPanel must report what it actually
    // occupies, otherwise the scroll range is wrong.
    float maxMain = 0.0f;
    for (const Line& ln : lines_) maxMain = std::max(maxMain, ln.mainSize);

    UNREFERENCED_PARAMETER(itemMain);
    UNREFERENCED_PARAMETER(itemCross);

    SizeDip out;
    if (h) { out.w = maxMain; out.h = totalCross; }
    else   { out.w = totalCross; out.h = maxMain; }
    return out;
}

void WrapPanel::ArrangeOverride(const RectDip& content) {
    const bool h = horizontal();
    const std::optional<float>& itemMain = h ? itemWidth_ : itemHeight_;
    const std::optional<float>& itemCross = h ? itemHeight_ : itemWidth_;

    // Recompute the line breaks against the *arranged* extent. Measure may have
    // run against a different (often unbounded) constraint, and using a stale
    // break set would place children outside the panel.
    ComputeLines(h ? content.w : content.h, h ? content.h : content.w);

    float crossPos = h ? content.y : content.x;
    for (const Line& ln : lines_) {
        float mainPos = h ? content.x : content.y;
        for (int i = ln.startIndex; i < ln.endIndex; ++i) {
            if (i < 0 || static_cast<size_t>(i) >= children_.size()) continue;
            FrameworkElement* c = children_[static_cast<size_t>(i)].get();
            if (!c || !c->IsVisible()) continue;

            float slotMain = MainFootprint(c, h, itemMain);
            // The child's cross slot is the whole line, so a Stretch-aligned
            // child fills the line height and a Center-aligned one centres in
            // it. ArrangeChild subtracts the margin and applies alignment.
            float slotCross = itemCross.has_value()
                                  ? CrossFootprint(c, h, itemCross)
                                  : ln.crossSize;

            RectDip slot = h ? RectDip{mainPos, crossPos, slotMain, slotCross}
                             : RectDip{crossPos, mainPos, slotCross, slotMain};
            ArrangeChild(c, slot);
            mainPos += slotMain;
        }
        crossPos += ln.crossSize;
    }
}

} // namespace fluent
