// LayoutCostProbe.cpp
#include "LayoutCostProbe.h"

namespace fluent {

namespace {
const char* KeyName(LayoutCostKey key) {
    switch (key) {
        case LayoutCostKey::TextAreaWrapExtent: return "TextArea::WrapExtent";
        case LayoutCostKey::TextBoxMeasure:     return "TextBox::Measure";
        case LayoutCostKey::TextBlockMeasure:   return "TextBlock::Measure";
        case LayoutCostKey::BoundsChangedTotal: return "*OnBoundsChanged(all)";
        case LayoutCostKey::TextBlockBoundsChanged: return "TextBlock::BoundsChg";
        case LayoutCostKey::TextAreaBoundsChanged:  return "TextArea::BoundsChg";
        case LayoutCostKey::TreeViewBoundsChanged:  return "TreeView::BoundsChg";
        case LayoutCostKey::ListBoxBoundsChanged:   return "ListBox::BoundsChg";
        case LayoutCostKey::ScrollPanelBoundsChanged: return "ScrollPanel::BoundsChg";
        case LayoutCostKey::ScrollViewerBoundsChanged: return "ScrollViewer::BoundsChg";
        case LayoutCostKey::ProgressBarBoundsChanged: return "ProgressBar::BoundsChg";
        case LayoutCostKey::DCompCreateSurface: return "  DComp:CreateSurface";
        case LayoutCostKey::DCompBeginDraw:     return "  DComp:BeginDraw";
        case LayoutCostKey::DCompDrawCallback:  return "  DComp:ourDrawing";
        case LayoutCostKey::DCompDrawContent:   return "    ourDraw:content";
        case LayoutCostKey::DCompDrawOverlay:   return "    ourDraw:overlay";
        case LayoutCostKey::DCompEndDraw:       return "  DComp:EndDraw";
        case LayoutCostKey::DCompSetContent:    return "  DComp:SetContent";
        default:                                return "?";
    }
}
}  // namespace

void LayoutCostProbe::Report(const char* label) {
    if (!enabled_) return;
    // Counts first: they frame how to read the timers below. `calls` is the size of
    // the traversal that actually ran a virtual Measure, `hits` the part the cache
    // absorbed. A hit rate near zero on a resize is expected (every width changed);
    // a hit rate near zero on a NON-resize layout means something is invalidating
    // the whole tree and that is the bug, not the cost of any one control.
    const int calls = counts_[static_cast<size_t>(LayoutCountKey::MeasureCalls)];
    const int hits = counts_[static_cast<size_t>(LayoutCountKey::MeasureCacheHits)];
    const int rects = counts_[static_cast<size_t>(LayoutCountKey::BoundsChanged)];
    if (calls || hits || rects)
        FL_TRACEF("LayoutCost",
                  "%s measured=%-5d cacheHits=%-5d (hit rate %.0f%%)  boundsChanged=%d",
                  label, calls, hits,
                  (calls + hits) ? 100.0 * hits / (calls + hits) : 0.0, rects);
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (e.count == 0) continue;
        FL_TRACEF("LayoutCost", "%s %-24s n=%-4d total=%7.3fms max=%7.3fms",
                  label, KeyName(static_cast<LayoutCostKey>(i)), e.count,
                  e.totalMs, e.maxMs);
    }

    // Explicit unattributed remainder on the arrange side. BoundsChangedTotal is the
    // outer scope around every OnBoundsChanged; the per-control keys nest inside it.
    // Whatever the children do not account for is either a control with no key yet or
    // the base (non-overriding) elements. Printing the subtraction is the point: the
    // previous pass left 2.6ms of a 3.4ms arrange unlabelled, and reading that gap off
    // two lines by hand is exactly how a wrong suspect gets blamed.
    const Entry& total = entries_[static_cast<size_t>(LayoutCostKey::BoundsChangedTotal)];
    if (total.count > 0) {
        double attributed = 0.0;
        int attributedN = 0;
        // The arrange group is the half-open range (BoundsChangedTotal, ArrangeGroupEnd)
        // — see the ordering contract on LayoutCostKey. The DComp keys past
        // ArrangeGroupEnd are deliberately excluded: they nest INSIDE the per-control
        // keys, so counting them would subtract the same milliseconds twice and report
        // a negative remainder.
        for (size_t i = static_cast<size_t>(LayoutCostKey::BoundsChangedTotal) + 1;
             i < static_cast<size_t>(LayoutCostKey::ArrangeGroupEnd); ++i) {
            attributed += entries_[i].totalMs;
            attributedN += entries_[i].count;
        }
        FL_TRACEF("LayoutCost",
                  "%s %-24s n=%-4d total=%7.3fms  (of %.3fms total, %.0f%% unexplained)",
                  label, "  [unattributed]", total.count - attributedN,
                  total.totalMs - attributed, total.totalMs,
                  total.totalMs > 0.0
                      ? 100.0 * (total.totalMs - attributed) / total.totalMs
                      : 0.0);
    }
}

}  // namespace fluent
