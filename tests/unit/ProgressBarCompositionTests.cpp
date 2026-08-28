// ProgressBarCompositionTests.cpp — headless tests that an indeterminate
// ProgressBar drives the composition backend correctly (Phase 2).
//
// The PoC proved the sweep on real hardware; these lock the state machine that
// wires it, without a GPU: a MockHost hands the ProgressBar a
// FakeCompositionBackend through the attach context, and the tests assert on the
// recorded compositor calls. They also pin the fallback: with no backend the bar
// keeps its historical UI-thread sweep (WantsAnimationTick() == true).
//
// What is intentionally NOT tested here: pixel output (the fake rasterizes
// nothing) and the exact phase math (covered by the sweep's own logic / hardware
// confirmation). We test *which* compositor operations happen and *when*.

#include "../framework/Test.h"
#include "../framework/FakeCompositionBackend.h"
#include "../../FluentUI/controls/ProgressBar.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;
using fltest::FakeCompositionBackend;
using fltest::FakeCompositionVisual;

namespace {

// A WindowServices double whose only live capability is Composition(): it returns
// the fake backend (or null, to exercise the fallback path). Everything else is a
// stub — these tests never touch D2D/DWrite/popups.
class MockHost : public WindowServices {
public:
    explicit MockHost(ICompositionBackend* backend) : backend_(backend) {}

    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }

    ICompositionBackend* Composition() override { return backend_; }

    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override {
        return Subscription();
    }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override {
        return Subscription();
    }

private:
    ICompositionBackend* backend_ = nullptr;
    D2DContext d2d_;      // uninitialized; never used by these tests
    DWriteContext dwrite_;
};

// Build an attach context wired to `host` + `anims`, at DPI 1.0.
UIContext MakeCtx(MockHost& host, AnimationRegistry& anims) {
    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dpiScale = 1.0f;
    return ctx;
}

// The segment is the animated child; the container is the clipped root. Find the
// child of the single root visual (the sweep parents segment under container).
FakeCompositionVisual* SegmentOf(FakeCompositionBackend& backend) {
    if (backend.rootVisuals.empty()) return nullptr;
    auto* container = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    if (container->children.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(container->children[0]);
}

}  // namespace

// Attaching in indeterminate mode creates the container+segment, parents the
// container on the root, starts the compositor sweep, and reports that no
// UI-thread tick is needed (the compositor owns the motion).
TEST(ProgressBarComposition, IndeterminateAttachStartsCompositorSweep) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    ProgressBar pb;
    pb.SetBounds({0, 0, 200, 20});
    pb.SetIndeterminate(true);
    pb.AttachToContext(MakeCtx(host, anims));

    EXPECT_EQ(backend.RootCount(), 1);            // container parented once
    EXPECT_EQ(backend.createdVisuals, 2);         // container + segment
    FakeCompositionVisual* seg = SegmentOf(backend);
    EXPECT_TRUE(seg != nullptr);
    EXPECT_TRUE(seg && seg->IsAnimatingOffsetX()); // sweep running on the segment
    EXPECT_TRUE(backend.commitRequests >= 1);      // published
    EXPECT_FALSE(pb.WantsAnimationTick());         // compositor owns it, no UI tick

    pb.DetachFromContext();  // clean teardown (no live device to worry about)
}

// The container is clipped to the track rect so the pill never spills past the
// bar ends.
TEST(ProgressBarComposition, ContainerIsClippedToTrack) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    ProgressBar pb;
    pb.SetBounds({0, 0, 200, 20});
    pb.SetIndeterminate(true);
    pb.AttachToContext(MakeCtx(host, anims));

    auto* container = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    EXPECT_TRUE(container->hasClip);
    EXPECT_TRUE(container->clipR > 0.0f);   // some positive track width

    pb.DetachFromContext();
}

// A resize re-fits the sweep in place: the root still holds exactly one visual
// (no duplicate parenting) and the segment is still animating (phase-continuous
// re-fit, not a fresh start/stop).
TEST(ProgressBarComposition, ResizeKeepsSingleRootAndRunningSweep) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    ProgressBar pb;
    pb.SetBounds({0, 0, 200, 20});
    pb.SetIndeterminate(true);
    pb.AttachToContext(MakeCtx(host, anims));

    const int visualsAfterAttach = backend.createdVisuals;
    pb.SetBounds({0, 0, 320, 20});   // wider: triggers OnBoundsChanged → re-fit

    EXPECT_EQ(backend.RootCount(), 1);                  // not re-parented
    EXPECT_EQ(backend.createdVisuals, visualsAfterAttach);  // no new visuals
    FakeCompositionVisual* seg = SegmentOf(backend);
    EXPECT_TRUE(seg && seg->IsAnimatingOffsetX());       // still sweeping

    pb.DetachFromContext();
}

// Leaving indeterminate mode removes the sweep visual from the root and requests
// a commit, so the determinate bar draws on the content swap chain alone.
TEST(ProgressBarComposition, LeavingIndeterminateRemovesVisual) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    ProgressBar pb;
    pb.SetBounds({0, 0, 200, 20});
    pb.SetIndeterminate(true);
    pb.AttachToContext(MakeCtx(host, anims));
    EXPECT_EQ(backend.RootCount(), 1);

    const int commitsBefore = backend.commitRequests;
    pb.SetValue(0.5f);   // exits indeterminate mode

    EXPECT_EQ(backend.RootCount(), 0);                  // visual removed
    EXPECT_TRUE(backend.commitRequests > commitsBefore); // change published

    pb.DetachFromContext();
}

// With no backend (host returns null) the bar keeps its historical UI-thread
// sweep: it is not "compositor active", so it still wants a per-frame tick.
TEST(ProgressBarComposition, NoBackendFallsBackToUiSweep) {
    MockHost host(nullptr);  // Composition() returns null
    AnimationRegistry anims;

    ProgressBar pb;
    pb.SetBounds({0, 0, 200, 20});
    pb.SetIndeterminate(true);
    pb.AttachToContext(MakeCtx(host, anims));

    EXPECT_TRUE(pb.WantsAnimationTick());  // UI-thread sweep still drives it

    pb.DetachFromContext();
}

// An unattached indeterminate bar (no host at all) also keeps the UI sweep — this
// mirrors the existing RangeBase test and must stay true after the migration.
TEST(ProgressBarComposition, UnattachedIndeterminateWantsTick) {
    ProgressBar pb;
    pb.SetIndeterminate(true);
    EXPECT_TRUE(pb.WantsAnimationTick());
}

TEST(ProgressBarComposition, AncestorCollapseStopsAndRemovesSweep) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    StackPanel root;
    auto* pb = root.Emplace<ProgressBar>();
    pb->SetBounds({0, 0, 200, 20});
    pb->SetIndeterminate(true);
    root.AttachToContext(MakeCtx(host, anims));
    EXPECT_EQ(backend.RootCount(), 1);

    root.SetVisible(false);
    EXPECT_EQ(backend.RootCount(), 0);
    EXPECT_FALSE(pb->WantsAnimationTick());

    root.SetVisible(true);
    EXPECT_EQ(backend.RootCount(), 1);
    EXPECT_FALSE(pb->WantsAnimationTick());
    root.DetachFromContext();
}
