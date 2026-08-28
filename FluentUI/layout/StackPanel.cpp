// StackPanel.cpp

#include "StackPanel.h"
#include <algorithm>

namespace fluent {

namespace {
// WPF's StackPanel has no star semantics: children always get their desired
// size on the main axis and stretch on the cross axis. "Fill remaining space"
// layouts belong in Grid with a Star row/column.
bool IsStarMain(const FrameworkElement* c, bool horizontal) {
    UNREFERENCED_PARAMETER(c);
    UNREFERENCED_PARAMETER(horizontal);
    return false;
}

// The child's fixed main-axis content size (explicit size if set, else its
// measured desired size).
float MainContentSize(const FrameworkElement* c, bool horizontal) {
    float explicitSize = horizontal ? c->Width() : c->Height();
    if (!IsAuto(explicitSize)) return explicitSize;
    const SizeDip& d = c->Desired();
    return horizontal ? d.w : d.h;
}

float MainMargin(const FrameworkElement* c, bool horizontal) {
    return horizontal ? c->Margin().horizontal() : c->Margin().vertical();
}
} // namespace

SizeDip StackPanel::MeasureOverride(float availW, float availH) {
    const bool h = horizontal();
    float sumMain = 0.0f;   // every child's natural main size + all margins
    float maxCross = 0.0f;
    int count = 0;

    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        ++count;

        // Offer the child the full cross extent (minus its cross margin) and the
        // panel's main extent.
        float crossAvail = h ? availH : availW;
        float crossMargin = h ? c->Margin().vertical() : c->Margin().horizontal();
        float childCrossAvail = std::max(0.0f, crossAvail - crossMargin);
        if (h)
            c->MeasureCached(/*availW*/ availW, /*availH*/ childCrossAvail);
        else
            c->MeasureCached(/*availW*/ childCrossAvail, /*availH*/ availH);

        sumMain += MainMargin(c, h);
        // Accumulate the child's natural main size so the panel can auto-size
        // correctly in a parent that offers unbounded space (ScrollPanel).
        sumMain += MainContentSize(c, h);

        const SizeDip& d = c->Desired();
        float childCross = (h ? d.h : d.w) + crossMargin;
        maxCross = std::max(maxCross, childCross);
    }

    if (count > 1) sumMain += spacing_ * (count - 1);

    SizeDip out;
    if (h) { out.w = sumMain; out.h = maxCross; }
    else   { out.w = maxCross; out.h = sumMain; }
    return out;
}

void StackPanel::ArrangeOverride(const RectDip& content) {
    const bool h = horizontal();
    float totalMain = h ? content.w : content.h;

    // First pass: account for fixed children + margins + spacing.
    int count = 0;
    float usedMain = 0.0f;
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        ++count;
        usedMain += MainMargin(c, h);
        usedMain += MainContentSize(c, h);
    }
    if (count > 1) usedMain += spacing_ * (count - 1);

    // Second pass: walk the main axis, building each child's slot (which still
    // includes its margin — ArrangeChild subtracts it and applies alignment).
    float pos = h ? content.x : content.y;
    bool first = true;
    for (auto& cptr : children_) {
        FrameworkElement* c = cptr.get();
        if (!c || !c->IsVisible()) continue;
        if (!first) pos += spacing_;
        first = false;

        float mainSize = MainContentSize(c, h);
        float slotMain = mainSize + MainMargin(c, h);

        RectDip slot = h ? RectDip{pos, content.y, slotMain, content.h}
                         : RectDip{content.x, pos, content.w, slotMain};
        ArrangeChild(c, slot);
        pos += slotMain;
    }
}

} // namespace fluent
