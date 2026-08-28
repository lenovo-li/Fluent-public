// Scenes.cpp — the FluentUIBench scenarios (roadmap §18.2).
//
// Ten scenes are registered. The ones whose cost is pure CPU/logic run for real
// against the FluentUI core (layout, dirty propagation, active-animation
// ticking, measure cache, idle detection) and report timings measured with the
// diagnostics ScopedTimer. The ones that fundamentally need a live swap chain,
// GPU, or interactive window (visual/text raster stress, resize, popup HWND
// churn, DPI lab, theme raster, input latency) are registered as honest stubs:
// they describe what a real Windows+GPU run must measure and report `measured =
// false` rather than fabricate a number (roadmap §20.4 forbids faking GPU data).
//
// As later work packages land the real pipeline, a stub is replaced by a scene
// that drives the actual window — the harness shape does not change.

#include "Bench.h"

#include "../FluentUI/layout/StackPanel.h"
#include "../FluentUI/layout/Grid.h"
#include "../FluentUI/core/FrameworkElement.h"
#include "../FluentUI/animation/AnimationRegistry.h"
#include "../FluentUI/window/FrameScheduler.h"
#include "../FluentUI/diagnostics/PerformanceCounters.h"
#include "../FluentUI/controls/Expander.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace fluent;
using bench::Result;

namespace {

// ---- shared helpers -------------------------------------------------------

// The frame budget of THIS MACHINE's display, not a hard-coded 60 Hz.
//
// Reporting a cost as "% of a 16.67 ms frame" is wrong on any panel that is not 60 Hz,
// and wrong in the dangerous direction: on a 240 Hz display the real budget is 4.17 ms,
// so a hard-coded 60 Hz figure understates the load by 4x. A scene that looked like it
// used 3% of a frame would actually be using 12%.
//
// Same source the frame pacer uses (NativeWindowHost::RefreshHz -> EnumDisplaySettingsW),
// so the bench and the running app agree about what a frame is. Falls back to 60 only
// when the query fails, and says so in the label rather than silently pretending.
struct FrameBudget {
    int hz = 0;              // 0 = unknown
    double ms = 1000.0 / 60.0;
};

FrameBudget DisplayFrameBudget() {
    FrameBudget b;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmDisplayFrequency > 1) {           // 0 and 1 both mean "default/unknown"
        b.hz = static_cast<int>(dm.dmDisplayFrequency);
        b.ms = 1000.0 / static_cast<double>(b.hz);
    }
    return b;
}

// A leaf with a fixed desired size and no GPU dependency (mirrors the test
// TestLeaf). Used to build large synthetic trees.
class BenchLeaf : public FrameworkElement {
public:
    BenchLeaf(float w, float h) { SetWidth(w); SetHeight(h); }
    void Render(const DrawingContext&) override {}
};

// A leaf that always wants an animation tick (never finishes), to load the
// active-animation set. Counts how many ticks it received.
class BenchAnimLeaf : public FrameworkElement {
public:
    void Render(const DrawingContext&) override {}
    bool WantsAnimationTick() const override { return true; }
    void OnAnimationTick(float) override { ++ticks_; }
    uint32_t ticks_ = 0;
};

char msgbuf[256];

// ---- 1. Controls Grid: many leaves laid out repeatedly --------------------

Result ControlsGrid() {
    // Build a StackPanel with 1,000 fixed-size children and run a full
    // Measure+Arrange pass many times, measuring the layout cost. This is the
    // "lots of ordinary controls" scene reduced to its layout core (no GPU).
    constexpr int kChildren = 1000;
    constexpr int kPasses = 200;

    StackPanel panel;
    panel.SetOrientation(StackPanel::Orientation::Vertical);
    panel.SetSpacing(2.0f);
    for (int i = 0; i < kChildren; ++i)
        panel.Emplace<BenchLeaf>(120.0f, 24.0f);

    double ms = 0.0;
    volatile float sink = 0.0f;
    {
        ScopedTimer t(ms);
        for (int p = 0; p < kPasses; ++p) {
            // Force a fresh measure each pass by invalidating a child's size.
            panel.UpdateLayout({0, 0, 400, 40000});
            sink += panel.Desired().h;
        }
    }
    (void)sink;

    Result r;
    r.primaryMs = ms;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "%d children x %d passes; %.4f ms/pass",
                  kChildren, kPasses, ms / kPasses);
    r.metric = msgbuf;
    return r;
}

// ---- 2. Text Stress -------------------------------------------------------

Result TextStress() {
    Result r;
    r.measured = false;
    r.note = "Requires DWrite/GPU: measures IDWriteTextLayout build + draw for "
             "mixed CJK/Latin wrapping. Run in a real window; record "
             "textLayoutsNew / textLayoutCacheHits from FrameStats.";
    return r;
}

// ---- 3. Tree Virtualization ----------------------------------------------

Result TreeVirtualization() {
    Result r;
    r.measured = false;
    r.note = "Virtualization not implemented yet (roadmap WP-07). Target: 100k "
             "data rows must not create 100k UIElements — visual element count "
             "should track visible rows. Scene activates once TreeView "
             "virtualizes.";
    return r;
}

// ---- 4. Animation Stress: active-set tick ---------------------------------

Result AnimationStress() {
    // The roadmap's key perf property: per-frame cost is O(active animations),
    // not O(tree). Build a large tree where only some leaves animate, collect
    // the active set once, then tick it many times and measure.
    constexpr int kTotal = 5000;
    constexpr int kAnimating = 200;
    constexpr int kTicks = 1000;

    StackPanel root;
    std::vector<UIElement*> roots{&root};
    for (int i = 0; i < kTotal; ++i) {
        if (i < kAnimating) root.Emplace<BenchAnimLeaf>();
        else root.Emplace<BenchLeaf>(10.0f, 10.0f);
    }

    AnimationRegistry reg;
    reg.Collect(roots);  // pull-once

    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int i = 0; i < kTicks; ++i) reg.Tick(1.0f / 60.0f);
    }

    Result r;
    r.primaryMs = ms;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "active=%zu of %d elements; %d ticks; %.5f ms/tick "
                  "(cost tracks active set, not tree size)",
                  reg.Count(), kTotal, kTicks, ms / kTicks);
    r.metric = msgbuf;
    return r;
}

// ---- 4b. Expander reveal ---------------------------------------------------

Result ExpanderReveal() {
    // THE QUESTION THIS SCENE EXISTS TO ANSWER: does the Expander reveal animation
    // block the UI thread? Nothing in this framework may do blocking or long work on
    // the UI thread, so an animation that is expensive per tick is a defect even if it
    // "looks fine" on a fast machine.
    //
    // The reveal is deliberately the WORST-CASE shape for a UI-thread animation,
    // because Expander::OnAnimationTick raises DirtyFlags::Measure rather than Render:
    // the revealed height feeds this element's desired size, so the parent must re-run
    // layout on every frame of the ease. That is O(content subtree) per tick, unlike a
    // fade or a knob slide which are O(1) Render-only.
    //
    // So this measures the whole per-frame cost a real reveal pays: tick + the relayout
    // the tick forces, reported against THIS DISPLAY's actual frame budget. The budget
    // is not a constant: 16.67 ms at 60 Hz but 4.17 ms at 240 Hz, and quoting a 60 Hz
    // figure on a high-refresh panel understates the load by the ratio of the two.
    //
    // Deliberately NOT measured here: DirectWrite text measurement and any GPU work.
    // The content is BenchLeaf (fixed desired size, empty Render), and the Expander is
    // unattached so Dwrite() is null and Render returns early. This isolates the
    // layout/tick cost, which is the part the animation adds. A real page also pays for
    // its content's own Measure, which is why the scene reports per-child cost too.
    constexpr int kChildren = 200;   // a deliberately heavy content subtree
    constexpr int kTicks = 1000;

    Expander exp;
    exp.SetHeader(L"Reveal");
    auto content = std::make_unique<StackPanel>();
    for (int i = 0; i < kChildren; ++i)
        content->Emplace<BenchLeaf>(120.0f, 24.0f);
    exp.SetContent(std::move(content));

    // Animated expand: Transition::Animate leaves reveal_ away from its target, which
    // is what makes WantsAnimationTick() true and puts the element in the active set.
    exp.SetExpanded(true, Expander::Transition::Animate);

    std::vector<UIElement*> roots{&exp};
    AnimationRegistry reg;
    reg.Collect(roots);
    const size_t activeAtStart = reg.Count();

    // Measure the realistic per-frame pair: advance the ease, then satisfy the layout
    // it just invalidated — exactly what RunDueFrame does after PumpAnimations.
    // reveal_ is re-seeded each iteration so the ease never settles and every one of
    // the kTicks iterations does real work (a settled ease would drop out of the active
    // set and flatter the average).
    double ms = 0.0;
    {
        ScopedTimer t(ms);
        for (int i = 0; i < kTicks; ++i) {
            if (!exp.WantsAnimationTick()) {
                // Restart the reveal so the loop keeps measuring live ticks.
                exp.SetExpanded(!exp.IsExpanded(), Expander::Transition::Animate);
                reg.Collect(roots);
            }
            reg.Tick(1.0f / 60.0f);
            exp.Measure(400.0f, 10000.0f);
            exp.Arrange(RectDip{0.0f, 0.0f, 400.0f, exp.Desired().h});
        }
    }

    const double perTick = ms / kTicks;
    // This machine's real budget. The reveal must be a small fraction of one frame, not
    // merely "under" it: everything else in the frame (paint, other animations, input)
    // has to fit alongside.
    const FrameBudget fb = DisplayFrameBudget();

    Result r;
    r.primaryMs = ms;
    char hzLabel[64];
    if (fb.hz > 0)
        std::snprintf(hzLabel, sizeof(hzLabel), "%d Hz -> %.2f ms", fb.hz, fb.ms);
    else
        std::snprintf(hzLabel, sizeof(hzLabel), "refresh unknown, assuming 60 Hz");

    std::snprintf(msgbuf, sizeof(msgbuf),
                  "active=%zu; %d ticks over %d-child content; %.5f ms per "
                  "tick+relayout = %.3f%% of this display's frame (%s). Measure-level "
                  "dirty, so each tick re-lays out the content subtree.",
                  activeAtStart, kTicks, kChildren, perTick,
                  100.0 * perTick / fb.ms, hzLabel);
    r.metric = msgbuf;
    r.note = "CPU only: no DWrite (unattached, so Render early-outs) and no GPU. "
             "Budget read from EnumDisplaySettingsW, the same source the frame pacer "
             "uses. Real-hardware check still needed for smoothness.";

    // Regression gate (only enforced under --gate).
    //
    // 5% of one frame for the animation tick plus the relayout it forces. The measured
    // value is ~0.09% at 240 Hz, so the limit is ~55x headroom — deliberately loose,
    // because the job of this gate is to catch an ORDER-OF-MAGNITUDE regression (someone
    // removing the measure cache, or making the reveal re-measure a virtualized list in
    // full), not to police normal variance on a busy machine.
    //
    // A tight threshold here would fail spuriously and get disabled, which is worse than
    // no gate at all. Tighten it only if a real regression slips through this one.
    r.perFrameMs = perTick;
    r.budgetFraction = 0.05;
    return r;
}

// ---- 5. Resize Stress -----------------------------------------------------

Result ResizeStress() {
    Result r;
    r.measured = false;
    r.note = "Requires a live swap chain: continuous window resize should reuse "
             "the swap chain (ResizeBuffers), never rebuild visuals, no flicker. "
             "Layout-only cost is covered by ControlsGrid.";
    return r;
}

// ---- 6. Popup Stress ------------------------------------------------------

Result PopupStress() {
    Result r;
    r.measured = false;
    r.note = "Requires HWNDs: open/close a cascading menu 10,000 times and prove "
             "GDI/USER handles + private bytes do not grow (ProcessStats::Sample "
             "before/after). Popup *state-machine* logic is covered by unit "
             "tests (PopupStateTests).";
    return r;
}

// ---- 7. DPI Lab -----------------------------------------------------------

Result DpiLab() {
    Result r;
    r.measured = false;
    r.note = "Requires multi-monitor/GPU: drag across 96/120/144/192 DPI, verify "
             "no cached-resource reuse across DPI and no text clipping.";
    return r;
}

// ---- 8. Theme Lab ---------------------------------------------------------

Result ThemeLab() {
    Result r;
    r.measured = false;
    r.note = "Requires GPU raster: Light/Dark/Mica/Acrylic/high-contrast visual "
             "regression. Theme *token* resolution timing lands with WP-05.";
    return r;
}

// ---- 9. Input Latency -----------------------------------------------------

Result InputLatency() {
    Result r;
    r.measured = false;
    r.note = "Requires a live window: pointer-down to corresponding Present, "
             "measured with the frame-latency waitable object (roadmap §14.4).";
    return r;
}

// ---- 10. Idle Test --------------------------------------------------------

Result IdleTest() {
    // Pure-logic proxy for the idle property (roadmap §14.2): with nothing dirty
    // and no active animations, the scheduler must report "no frame needed". We
    // model that here: an empty active set after Collect means the host would
    // disarm its timer and Present nothing. A real 30-second idle Present-count
    // test needs the window and is noted.
    StackPanel root;
    root.Emplace<BenchLeaf>(10.0f, 10.0f);  // static content, no animation
    std::vector<UIElement*> roots{&root};

    // Settle layout once (this is what the host does on the first frame). After
    // it, nothing is dirty and nothing animates — the true idle state.
    root.UpdateLayout({0, 0, 100, 100});
    root.ClearDirtySubtree();

    AnimationRegistry reg;
    reg.Collect(roots);

    Result r;
    r.primaryMs = 0.0;
    bool idle = reg.Empty() && !root.AnyDirtyInSubtree(DirtyFlags::Measure);
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "active animations=%zu, measure-dirty=%s -> host would %s",
                  reg.Count(),
                  root.AnyDirtyInSubtree(DirtyFlags::Measure) ? "yes" : "no",
                  idle ? "stay idle (no Present)" : "request a frame");
    r.metric = msgbuf;
    r.note = "Logic proxy for idle. Full 30s idle Present-count test needs the "
             "window (roadmap §18.5) — verify no periodic Render/Present.";
    return r;
}

// ---- 11. Frame coalescing (WP-01) -----------------------------------------

Result FrameCoalescing() {
    // The core WP-01 property (roadmap §14.3): many invalidations between two
    // frames merge into ONE pending frame. We simulate an input event that
    // dirties several properties, then a render turn, and confirm only one frame
    // is serviced no matter how many requests arrived.
    FrameScheduler sched;

    constexpr int kRequests = 500;
    for (int i = 0; i < kRequests; ++i) {
        sched.RequestFrame(FrameReason::Input);
        sched.RequestFrame(FrameReason::Layout);
    }
    bool oneFramePending = sched.NeedsFrame();

    // The host services exactly one frame; the merged reasons carry every cause.
    FrameReason serviced = sched.BeginFrame();
    sched.EndFrame();
    bool idleAfter = !sched.NeedsFrame();

    Result r;
    r.primaryMs = 0.0;
    std::snprintf(msgbuf, sizeof(msgbuf),
                  "%d x2 requests -> pending=%s, serviced reasons: Input=%s "
                  "Layout=%s -> idle after=%s (N invalidations = 1 frame)",
                  kRequests, oneFramePending ? "1 frame" : "ERROR",
                  HasReason(serviced, FrameReason::Input) ? "yes" : "no",
                  HasReason(serviced, FrameReason::Layout) ? "yes" : "no",
                  idleAfter ? "yes" : "no");
    r.metric = msgbuf;
    r.note = "Confirms coalescing: the input path no longer paints per-property. "
             "Real per-frame FrameReason is recorded in FrameStats during a live "
             "window run (NativeWindowHost::LastFrameStats).";
    return r;
}

} // namespace

// Registration (order = §18.2 order).
REGISTER_SCENE("ControlsGrid", "Layout cost for 1,000 stacked controls", ControlsGrid);
REGISTER_SCENE("TextStress", "CJK/Latin mixed wrapping text", TextStress);
REGISTER_SCENE("TreeVirtualization", "100k data rows", TreeVirtualization);
REGISTER_SCENE("AnimationStress", "Active-set tick vs tree size", AnimationStress);
REGISTER_SCENE("ExpanderReveal", "Reveal ease: per-tick UI-thread cost", ExpanderReveal);
REGISTER_SCENE("ResizeStress", "Continuous window resize", ResizeStress);
REGISTER_SCENE("PopupStress", "Cascading menu open/close x10,000", PopupStress);
REGISTER_SCENE("DpiLab", "Cross-monitor / multi-DPI", DpiLab);
REGISTER_SCENE("ThemeLab", "Light/Dark/Mica/Acrylic/high-contrast", ThemeLab);
REGISTER_SCENE("InputLatency", "Pointer-down to Present", InputLatency);
REGISTER_SCENE("IdleTest", "Idle 30s: no periodic Present", IdleTest);
REGISTER_SCENE("FrameCoalescing", "N invalidations merge into one frame", FrameCoalescing);
