#include "DockPanel.h"
#include <algorithm>

namespace fluent {

std::unordered_map<UIElement*, Dock> DockPanel::docks_;

void DockPanel::SetDock(UIElement* element, Dock dock) {
    if (!element) return;
    docks_[element] = dock;
}

Dock DockPanel::GetDock(UIElement* element) {
    if (!element) return Dock::Left;
    auto it = docks_.find(element);
    return (it != docks_.end()) ? it->second : Dock::Left;
}

SizeDip DockPanel::MeasureOverride(float availW, float availH) {
    float totalW = 0.0f, totalH = 0.0f;
    float remainingW = availW, remainingH = availH;

    const size_t count = children_.size();

    for (size_t i = 0; i < count; ++i) {
        FrameworkElement* child = children_[i].get();
        if (!child || !child->IsVisible()) continue;
        const bool isLast = (i == count - 1);
        const Dock dock = isLast && lastChildFill_ ? Dock::Left : GetDock(child);

        // Measure child against the remaining space.
        child->MeasureCached(remainingW, remainingH);
        const SizeDip& desired = child->Desired();

        if (isLast && lastChildFill_) {
            // The last child fills everything left — its desired size is the max.
            totalW = std::max(totalW, remainingW);
            totalH = std::max(totalH, remainingH);
        } else {
            // Docked child consumes a strip from one edge.
            switch (dock) {
            case Dock::Left:
            case Dock::Right:
                remainingW = std::max(0.0f, remainingW - desired.w);
                totalW += desired.w;
                totalH = std::max(totalH, desired.h);
                break;
            case Dock::Top:
            case Dock::Bottom:
                remainingH = std::max(0.0f, remainingH - desired.h);
                totalH += desired.h;
                totalW = std::max(totalW, desired.w);
                break;
            }
        }
    }

    return {totalW, totalH};
}

void DockPanel::ArrangeOverride(const RectDip& content) {
    RectDip remaining = content;

    const size_t count = children_.size();

    for (size_t i = 0; i < count; ++i) {
        FrameworkElement* child = children_[i].get();
        if (!child || !child->IsVisible()) continue;
        const bool isLast = (i == count - 1);

        if (isLast && lastChildFill_) {
            // The last child fills all remaining space.
            ArrangeChild(child, remaining);
        } else {
            const Dock dock = GetDock(child);
            const SizeDip& desired = child->Desired();
            RectDip slot;

            switch (dock) {
            case Dock::Left:
                slot = {remaining.x, remaining.y, desired.w, remaining.h};
                remaining.x += desired.w;
                remaining.w = std::max(0.0f, remaining.w - desired.w);
                break;
            case Dock::Top:
                slot = {remaining.x, remaining.y, remaining.w, desired.h};
                remaining.y += desired.h;
                remaining.h = std::max(0.0f, remaining.h - desired.h);
                break;
            case Dock::Right:
                slot = {remaining.x + remaining.w - desired.w, remaining.y, desired.w, remaining.h};
                remaining.w = std::max(0.0f, remaining.w - desired.w);
                break;
            case Dock::Bottom:
                slot = {remaining.x, remaining.y + remaining.h - desired.h, remaining.w, desired.h};
                remaining.h = std::max(0.0f, remaining.h - desired.h);
                break;
            }

            ArrangeChild(child, slot);
        }
    }
}

} // namespace fluent
