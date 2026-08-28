// LayoutCostProbe.h — opt-in per-control accumulator for one layout pass.
//
// WHY THIS EXISTS. The Resize trace reports one `layout=NNms` number, and
// Window::OnLayout now splits that into spawn/wait/publish/arrange. Neither answers
// the question that actually decides what to optimize: *which control* owns the
// time. On the Gallery's TextArea page the tree holds four TextAreas (a 10k-line
// NoWrap document, a Wrap paragraph pair, a read-only code view, a log sink) plus
// several hundred ordinary controls, and the two candidate explanations —
// "one TextArea re-wraps on every width change" versus "the page simply has too
// many elements" — call for opposite fixes.
//
// HOW IT STAYS HONEST. Only elements that opt in report, so this measures a named
// suspect rather than pretending to profile the whole tree. Instrumenting every
// Measure by wrapping the virtual would change what is being measured: the wrapper's
// own cost lands inside the parent's sample, and a control measured through
// MeasureCached's short-circuit would be double-counted with its cache hit.
//
// NESTING IS DELIBERATELY *NOT* HANDLED, because for the intended use the suspects
// are leaves. A TextArea's Measure does not measure another TextArea, so no sample
// contains another sample of the same key. If a container is ever instrumented, its
// sample will include its children's — read the numbers with that in mind rather than
// assuming they partition the total.
//
// COST WHEN OFF: one bool test per Record call. Nothing allocates, nothing is
// written, and the counters are never touched — so leaving the instrumentation in
// place does not distort the very frames it is meant to describe.
#pragma once

#include "../fl_common.h"
#include "PerformanceCounters.h"
#include <array>

namespace fluent {

// The suspects worth separating on the resize path. Deliberately a small fixed
// enum rather than a string map: this is read and written inside Measure, and a
// hash lookup there would be a cost of the same order as the thing being measured.
// Only controls that actually override Measure appear here. The list previously
// carried TextAreaMeasure / TreeViewMeasure / ListBoxMeasure, none of which override
// Measure at all (they take an explicit size and do their work elsewhere), so those
// keys could never report anything — an empty row is indistinguishable from "this
// control is free", which is the worst possible reading to hand someone. TextArea's
// real layout-path cost is the wrap walk, and that has its own key.
// ORDER IS LOAD-BEARING: the measure-side keys come first, then BoundsChangedTotal,
// then the arrange-side keys that nest inside it. Report() computes the unattributed
// arrange remainder as (BoundsChangedTotal - sum of everything after it), so a new
// arrange key must be added AFTER BoundsChangedTotal and a new measure key BEFORE it.
// Getting this wrong makes the remainder silently wrong rather than failing loudly,
// which is why it is stated here instead of left to the reader.
enum class LayoutCostKey {
    // --- Measure side (not part of the arrange remainder) ---
    TextAreaWrapExtent,   // the Wrap re-estimate / re-measure walk
    TextBoxMeasure,
    TextBlockMeasure,
    // The ARRANGE side. Added after a measured drag showed arrange at 3-4ms against
    // measure's 0.8ms with no instrumentation on the arrange path at all — every
    // per-control key above lives in Measure, so the expensive half of layout was
    // entirely dark. OnBoundsChanged is where arrange stops being pure geometry: it
    // is the hook controls use to re-fit compositor visuals, rebuild DWrite layouts
    // and resync scroll models, i.e. real work triggered by a rect assignment.
    // EVERY OnBoundsChanged override gets a key. The first arrange-side pass
    // instrumented only TextBlock and TextArea, which left ~2.6ms of a 3.4ms Button-page
    // arrange unattributed — and an unattributed majority invites the wrong conclusion
    // ("it must be the DComp controls") when the actual owner is simply unmeasured.
    // Partial attribution on a hot path is worse than none.
    BoundsChangedTotal,   // all OnBoundsChanged dispatches, whatever the subclass
    TextBlockBoundsChanged,  // UpdateMetrics -> CreateLayout (DWrite) on resize
    TextAreaBoundsChanged,   // InvalidateLayout + SyncScroll + composition refresh
    TreeViewBoundsChanged,   // scroll resync + RefreshComposition(true,true)
    ListBoxBoundsChanged,    // scroll resync + extent update
    ScrollPanelBoundsChanged,// scroll model bounds + extent
    ScrollViewerBoundsChanged,// re-clamp both offsets
    ProgressBarBoundsChanged, // compositor sweep geometry re-fit
    // Marks the end of the arrange-attribution group. Report() sums up to (not past)
    // this to compute the unattributed arrange remainder; everything below nests
    // INSIDE those keys and would double-count if included.
    ArrangeGroupEnd,
    // --- Inside DCompCompositionVisual::DrawSurface -------------------------
    // The four steps a surface refresh is made of, separated because they fail
    // differently and only one of them is our own drawing code. "arrange is slow"
    // and "the drag hitches" were both traced this far and no further; without this
    // split the answer to WHICH DComp call costs the time is a guess.
    //
    // These nest inside the *BoundsChanged keys above, so they do NOT add to the
    // arrange remainder — they explain part of it.
    DCompCreateSurface,   // IDCompositionDevice2::CreateSurface (VRAM allocation)
    DCompBeginDraw,       // IDCompositionSurface::BeginDraw (map / GPU sync point)
    DCompDrawCallback,    // our own D2D drawing into the mapped surface (CONTENT+OVERLAY mixed)
    DCompDrawContent,     // our D2D drawing of scrollable content rows (inside DCompDrawCallback)
    DCompDrawOverlay,     // our D2D drawing of overlay UI (scrollbar, caret; inside DCompDrawCallback)
    DCompEndDraw,         // IDCompositionSurface::EndDraw (flush)
    DCompSetContent,      // IDCompositionVisual2::SetContent (rebind)
    Count
};

// Counters that answer "how much work", as opposed to the timers above which
// answer "how long". They are separate because they need no clock: a resize whose
// Measure cost is spread over 900 cheap calls and one whose cost is 3 expensive
// calls read identically on a timer and call for opposite fixes (cut the traversal
// vs. fix the one control). Incremented from UIElement::MeasureCached, which is the
// single choke point every panel child passes through.
enum class LayoutCountKey {
    MeasureCalls,      // MeasureCached invocations that ran the real virtual Measure
    MeasureCacheHits,  // MeasureCached invocations served from cachedDesired_
    // SetBounds calls whose rect actually changed, i.e. the ones that dispatched
    // OnBoundsChanged. There is deliberately only ONE arrange counter: a separate
    // "OnBoundsChanged dispatches" tally would be the same number by construction,
    // and two counters that can never disagree are two chances to misread one fact.
    BoundsChanged,
    Count
};

class LayoutCostProbe {
public:
    // Enable/disable collection. Off by default; the app turns it on for a
    // diagnostic run (the demo binds it to a key alongside the F12 HUD).
    static void SetEnabled(bool on) { enabled_ = on; }
    static bool Enabled() { return enabled_; }

    // Zero every counter. Call at the start of a layout pass so the numbers
    // describe that pass rather than the session.
    //
    // WITHOUT THIS THE NUMBERS LIE IN A PARTICULAR WAY: they grow monotonically, so
    // a `total=` read after a hundred resize frames is a session sum that looks like
    // a catastrophic single frame. Window::OnLayout calls it before Measure.
    static void Reset() {
        for (auto& e : entries_) e = {};
        for (auto& c : counts_) c = 0;
    }

    // Add one to a work counter. Same off-cost as Record: a single bool test.
    static void Bump(LayoutCountKey key) {
        if (!enabled_) return;
        ++counts_[static_cast<size_t>(key)];
    }

    static int GetCount(LayoutCountKey key) {
        return counts_[static_cast<size_t>(key)];
    }

    // Add one sample. `ms` is wall-clock milliseconds for that control's work.
    static void Record(LayoutCostKey key, double ms) {
        if (!enabled_) return;
        Entry& e = entries_[static_cast<size_t>(key)];
        ++e.count;
        e.totalMs += ms;
        if (ms > e.maxMs) e.maxMs = ms;
    }

    struct Entry {
        int count = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;   // the single worst instance — a mean hides one bad actor
    };

    static const Entry& Get(LayoutCostKey key) {
        return entries_[static_cast<size_t>(key)];
    }

    // Emit one trace line per key that recorded anything. Silent when disabled or
    // when nothing was recorded, so it can be called unconditionally after a pass.
    static void Report(const char* label);

    // RAII sampler. Records on scope exit, so an early return inside the measured
    // region still reports (TextArea::Measure has several).
    class Scope {
    public:
        explicit Scope(LayoutCostKey key)
            : key_(key), start_(enabled_ ? QpcNow() : 0) {}
        ~Scope() {
            if (enabled_) Record(key_, QpcToMs(QpcNow() - start_));
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        LayoutCostKey key_;
        int64_t start_;
    };

private:
    // UI-thread only by intent, but the async worker also runs Measure — hence
    // `inline static` plain values rather than thread_local: a worker-thread sample
    // and a UI-thread sample of the same key are both interesting and neither is
    // load-bearing for correctness. Races here can only skew a diagnostic count,
    // never the layout itself.
    inline static bool enabled_ = false;
    inline static std::array<Entry, static_cast<size_t>(LayoutCostKey::Count)> entries_{};
    inline static std::array<int, static_cast<size_t>(LayoutCountKey::Count)> counts_{};
};

} // namespace fluent
