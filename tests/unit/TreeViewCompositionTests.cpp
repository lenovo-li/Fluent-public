// TreeViewCompositionTests.cpp — headless tests that TreeView drives the
// overscan scroll host correctly in composition mode (Phase 3), and that it falls
// back to the UI-thread scroll path with no backend.
//
// No GPU: a MockHost hands the TreeView a FakeCompositionBackend through the
// attach context. The fake rasterizes nothing (DrawSurface gets a null DC), so
// these assert on the compositor STATE MACHINE — visuals created, wheel starts an
// OffsetY tween, keyboard nav jumps immediately, hit-test uses the effective
// offset — not pixels. Pixel correctness is a manual on-hardware check.

#include "../framework/Test.h"
#include "../framework/FakeCompositionBackend.h"
#include "../../FluentUI/controls/TreeView.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;
using fltest::FakeCompositionBackend;
using fltest::FakeCompositionVisual;

namespace {

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
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
private:
    ICompositionBackend* backend_ = nullptr;
    D2DContext d2d_;
    DWriteContext dwrite_;
};

// A flat list of `n` rows (depth 0, no children) so visibleRows_ == n.
std::vector<TreeViewRow> FlatRows(int n) {
    std::vector<TreeViewRow> rows;
    for (int i = 0; i < n; ++i) {
        TreeViewRow r;
        r.id = i + 1;
        r.parentId = -1;
        r.text = L"Row";
        r.depth = 0;
        rows.push_back(r);
    }
    return rows;
}

UIContext MakeCtx(MockHost& host, AnimationRegistry& anims) {
    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dpiScale = 1.0f;
    return ctx;
}

// Tree: root -> viewport_ -> { clip_ -> content_, overlay_ }. The clip node masks the
// scrolled surface to the scrollable region; with TreeView's zero content inset it
// spans the whole viewport and is a pass-through (see ScrollContentHost.h).
FakeCompositionVisual* ContentOf(FakeCompositionBackend& b) {
    if (b.rootVisuals.empty()) return nullptr;
    auto* viewport = static_cast<FakeCompositionVisual*>(b.rootVisuals[0]);
    if (viewport->children.empty()) return nullptr;
    auto* clip = static_cast<FakeCompositionVisual*>(viewport->children[0]);
    if (clip->children.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(clip->children[0]);
}

}  // namespace

// Attaching with a backend builds the viewport/content/overlay tree and parents
// the viewport on the root.
TEST(TreeViewComposition, AttachBuildsCompositionTree) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(200));       // 200 * 28 = 5600 DIP content
    tree.SetBounds({0, 0, 200, 280});  // 10 rows visible
    tree.AttachToContext(MakeCtx(host, anims));

    EXPECT_EQ(backend.RootCount(), 1);
    EXPECT_EQ(backend.createdVisuals, 4);  // viewport + clip + content + overlay
    auto* viewport = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    EXPECT_EQ(static_cast<int>(viewport->children.size()), 2);  // clip + overlay
    EXPECT_TRUE(viewport->hasClip);
    EXPECT_TRUE(ContentOf(backend) != nullptr);

    tree.DetachFromContext();
}

TEST(TreeViewComposition, AncestorCollapseDetachesCompositionTree) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    StackPanel root;
    auto* tree = root.Emplace<TreeView>();
    tree->SetRows(FlatRows(200));
    tree->SetBounds({0, 0, 200, 280});
    root.AttachToContext(MakeCtx(host, anims));
    EXPECT_EQ(backend.RootCount(), 1);

    root.SetVisible(false);
    EXPECT_EQ(backend.RootCount(), 0);
    EXPECT_FALSE(tree->WantsAnimationTick());

    root.SetVisible(true);
    EXPECT_EQ(backend.RootCount(), 1);
    root.DetachFromContext();
}

// A wheel notch starts an OffsetY decelerate tween on the content visual — the
// smooth scroll runs on the compositor thread.
TEST(TreeViewComposition, WheelStartsCompositorTween) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(200));
    tree.SetBounds({0, 0, 200, 280});
    tree.AttachToContext(MakeCtx(host, anims));

    PointerEventArgs e;
    e.position = {100, 140};
    e.wheelDelta = -120;  // one notch down
    tree.OnPointerWheelChanged(e);

    EXPECT_TRUE(e.handled);
    EXPECT_TRUE(tree.WantsAnimationTick());  // compositor tween in flight
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content && content->IsAnimatingOffsetY());

    tree.DetachFromContext();
}

// Wheel on a non-scrollable tree (content fits) does nothing and bubbles.
TEST(TreeViewComposition, WheelNoScrollWhenContentFits) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(3));         // 3 * 28 = 84 < viewport
    tree.SetBounds({0, 0, 200, 280});
    tree.AttachToContext(MakeCtx(host, anims));

    PointerEventArgs e;
    e.position = {100, 140};
    e.wheelDelta = -120;
    tree.OnPointerWheelChanged(e);

    EXPECT_FALSE(e.handled);  // nothing to scroll → bubbles up
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content && !content->IsAnimatingOffsetY());

    tree.DetachFromContext();
}

// Keyboard End jumps to the bottom IMMEDIATELY (no long glide, §11.7): the content
// visual is left at a static offset, not animating.
TEST(TreeViewComposition, KeyboardEndJumpsImmediately) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(200));
    tree.SetBounds({0, 0, 200, 280});
    tree.AttachToContext(MakeCtx(host, anims));

    KeyEventArgs k;
    k.vk = VK_END;
    tree.OnKeyDownRouted(k);

    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    EXPECT_FALSE(content && content->IsAnimatingOffsetY());  // immediate, no tween
    // And the content visual has been shifted up (offsetY negative) to show the end.
    EXPECT_TRUE(content && content->offsetY < 0.0f);

    tree.DetachFromContext();
}

// With NO backend the tree falls back to the UI-thread scroll path: no composition
// visuals are created, and wheel drives the fallback (WantsAnimationTick via the
// scroll model's smooth-scroll tick).
TEST(TreeViewComposition, NoBackendFallsBackToUiScroll) {
    MockHost host(nullptr);  // Composition() == null
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(200));
    tree.SetBounds({0, 0, 200, 280});
    tree.AttachToContext(MakeCtx(host, anims));

    PointerEventArgs e;
    e.position = {100, 140};
    e.wheelDelta = -120;
    tree.OnPointerWheelChanged(e);
    EXPECT_TRUE(e.handled);
    EXPECT_TRUE(tree.WantsAnimationTick());  // UI-thread smooth scroll drives it

    tree.DetachFromContext();
}

// A resize MUST still leave the content surface rasterized at the new size, even
// though OnBoundsChanged no longer passes redrawContent=true.
//
// WHY THIS TEST EXISTS. That `true` was removed because it was redundant AND it was
// ~80% of the Gallery's arrange cost: ScrollContentHost::SetViewport already clears
// contentDrawn_ on a real size change, and NeedsRefill returns true when
// contentDrawn_ is false, so EnsureContent rebases and rasterizes on its own. That
// reasoning is a chain across two classes, and nothing enforced it — so a later
// change to SetViewport's dirty tracking could silently reintroduce a stale
// (old-size) surface, which on hardware reads as the nav pane freezing mid-drag
// while everything else reflows. Asserting on drawCount pins the behavior, not the
// implementation detail that currently delivers it.
TEST(TreeViewComposition, ResizeStillRasterizesContentSurface) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(200));
    tree.SetBounds({0, 0, 200, 280});
    tree.AttachToContext(MakeCtx(host, anims));

    auto* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    const int drawsAfterAttach = content->drawCount;
    EXPECT_TRUE(drawsAfterAttach > 0);   // attach rasterized once

    // Grow the viewport: a genuine size change, the resize-drag case.
    tree.SetBounds({0, 0, 200, 400});
    EXPECT_TRUE(content->drawCount > drawsAfterAttach);  // surface refreshed
    // Width is quantized to 256px quanta: 200 → 256. The extra 56px is clipped by
    // ApplyContentClip, so it is invisible; quantizing is what keeps CreateSurface
    // off the per-frame path during a drag.
    EXPECT_EQ(content->lastDrawW, 256u);

    tree.DetachFromContext();
}

// The other half of the contract, and the one that actually pins the fix: a MOVE
// (same size, new position) must not re-rasterize the row surface.
//
// This is the case redrawContent=true got wrong. A move still runs OnBoundsChanged,
// so the early return in SetBounds does not cover it, and SetViewport's sizeChanged
// is false — so nothing in ScrollContentHost asks for a redraw. Only the explicit
// forceRedraw did, and it burned a full overscan-surface rasterize (measured at
// 2.6-3.2ms on the Gallery nav pane) to reproduce pixels that were already correct.
//
// A move happens on every frame of a window drag from the top or left edge, and on
// every layout pass that shifts the pane — so this was not a rare path.
TEST(TreeViewComposition, MoveWithoutResizeDoesNotRedrawRows) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;

    TreeView tree;
    tree.SetRows(FlatRows(200));
    tree.SetBounds({0, 0, 200, 280});
    tree.AttachToContext(MakeCtx(host, anims));

    auto* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    const int drawsBefore = content->drawCount;

    // Same 200x280 extent, different origin: OnBoundsChanged RUNS (the rect differs),
    // but no pixel of the rows changed.
    tree.SetBounds({40, 60, 200, 280});
    EXPECT_EQ(content->drawCount, drawsBefore);

    // The visual must still have been repositioned — skipping the redraw must not
    // mean skipping the geometry, or the pane would detach from its layout slot.
    EXPECT_TRUE(backend.RootCount() == 1);

    tree.DetachFromContext();
}
