// ScrollContentHostTests.cpp — headless tests for the overscan scroll host
// (Phase 3, roadmap §11.2/§11.5). No GPU: a FakeCompositionBackend records the
// visuals and their offsets/tweens, so the coordinate model — effective offset,
// surface rebase with no screen jump, compositor tween seeding, device-loss state
// preservation — is verified purely as arithmetic.
//
// Coordinate recap (dpiScale = 1 in these tests, so DIP == px): the content
// visual's OffsetY = (surfaceOrigin - effectiveOffset). The logical offset the
// user scrolled to is preserved across a rebase (only surfaceOrigin moves), which
// is exactly "the screen does not jump".

#include "../framework/Test.h"
#include "../framework/FakeCompositionBackend.h"
#include "../../FluentUI/composition/ScrollContentHost.h"

using namespace fluent;
using fltest::FakeCompositionBackend;
using fltest::FakeCompositionVisual;

namespace {

// A content draw callback that just counts calls (headless: dc is null).
struct DrawCounter {
    int calls = 0;
    ScrollContentHost::DrawContentCallback cb() {
        return [this](ID2D1DeviceContext*, float, float) { ++calls; };
    }
};

// Tree: root -> viewport_ -> { clip_ -> content_ [-> caret_], overlay_ }. The clip
// node masks the scrolled content to the scrollable region (see ScrollContentHost.h);
// it is a pass-through when the content inset is zero.
FakeCompositionVisual* ClipOf(FakeCompositionBackend& b) {
    if (b.rootVisuals.empty()) return nullptr;
    auto* viewport = static_cast<FakeCompositionVisual*>(b.rootVisuals[0]);
    if (viewport->children.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(viewport->children[0]);
}

FakeCompositionVisual* ContentOf(FakeCompositionBackend& b) {
    FakeCompositionVisual* clip = ClipOf(b);
    if (!clip || clip->children.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(clip->children[0]);
}

FakeCompositionVisual* OverlayOf(FakeCompositionBackend& b) {
    if (b.rootVisuals.empty()) return nullptr;
    auto* viewport = static_cast<FakeCompositionVisual*>(b.rootVisuals[0]);
    if (viewport->children.size() < 2) return nullptr;
    return static_cast<FakeCompositionVisual*>(viewport->children[1]);
}

// Build a host with a 100x600 viewport at DPI 1 over `content` DIP of rows.
void Setup(ScrollContentHost& host, FakeCompositionBackend& backend,
           float contentHeight = 5000.0f) {
    host.Create(&backend, 1.0f);
    host.SetViewport({0, 0, 100, 600});
    host.SetContentHeight(contentHeight);
}

}  // namespace

TEST(ScrollContentHost, CreateBuildsViewportContentOverlay) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);

    EXPECT_TRUE(host.Valid());
    EXPECT_EQ(backend.createdVisuals, 4);   // viewport + clip + content + overlay
    EXPECT_EQ(backend.RootCount(), 1);      // viewport parented once
    auto* viewport = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    EXPECT_EQ(static_cast<int>(viewport->children.size()), 2);  // clip + overlay
    EXPECT_TRUE(viewport->hasClip);         // clipped to the viewport rect
    // The scrolled surface hangs off the clip node, not the container directly.
    EXPECT_TRUE(ContentOf(backend) != nullptr);
    EXPECT_TRUE(ClipOf(backend)->hasClip);
}

TEST(ScrollContentHost, MaxOffsetIsContentMinusViewport) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    EXPECT_NEAR(host.MaxOffset(), 4400.0f, 0.5f);  // 5000 - 600
}

TEST(ScrollContentHost, SettledEffectiveEqualsTarget) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    DrawCounter d;

    host.SetOffsetImmediate(1000.0f, d.cb());
    EXPECT_FALSE(host.IsAnimating());
    EXPECT_NEAR(host.EffectiveOffset(), 1000.0f, 0.5f);
    EXPECT_NEAR(host.TargetOffset(), 1000.0f, 0.5f);
}

TEST(ScrollContentHost, ImmediateAppliesAffineContentOffset) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    DrawCounter d;

    host.SetOffsetImmediate(1000.0f, d.cb());
    // content OffsetY (px) == (surfaceOrigin - effectiveOffset) * dpi(=1).
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    const float expected = host.SurfaceOrigin() - 1000.0f;
    EXPECT_NEAR(content->offsetY, expected, 0.5f);
    EXPECT_FALSE(content->IsAnimatingOffsetY());  // static offset, no tween
}

TEST(ScrollContentHost, DpiChangeRebuildsAllPixelGeometry) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    host.Create(&backend, 2.0f);
    host.SetViewport({10, 20, 100, 60});
    host.SetContentHeight(500.0f);
    DrawCounter d;
    host.SetOffsetImmediate(25.0f, d.cb());
    host.RedrawOverlay([](ID2D1DeviceContext*, float, float) {});

    auto* viewport = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    FakeCompositionVisual* content = ContentOf(backend);
    FakeCompositionVisual* overlay = OverlayOf(backend);
    EXPECT_NEAR(viewport->offsetX, 20.0f, 0.01f);
    EXPECT_NEAR(viewport->offsetY, 40.0f, 0.01f);
    EXPECT_NEAR(viewport->clipR, 200.0f, 0.01f);
    EXPECT_EQ(overlay->lastDrawW, static_cast<uint32_t>(200));

    host.SetDpiScale(1.0f);
    host.EnsureContent(d.cb(), true);
    host.RedrawOverlay([](ID2D1DeviceContext*, float, float) {});

    EXPECT_NEAR(viewport->offsetX, 10.0f, 0.01f);
    EXPECT_NEAR(viewport->offsetY, 20.0f, 0.01f);
    EXPECT_NEAR(viewport->clipR, 100.0f, 0.01f);
    EXPECT_NEAR(viewport->clipB, 60.0f, 0.01f);
    EXPECT_EQ(overlay->lastDrawW, static_cast<uint32_t>(100));
    EXPECT_EQ(overlay->lastDrawH, static_cast<uint32_t>(60));
    EXPECT_NEAR(content->offsetY,
                host.SurfaceOrigin() - host.TargetOffset(), 0.5f);
}

TEST(ScrollContentHost, ImmediateClampsToRange) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    DrawCounter d;

    host.SetOffsetImmediate(99999.0f, d.cb());
    EXPECT_NEAR(host.EffectiveOffset(), host.MaxOffset(), 0.5f);
    host.SetOffsetImmediate(-50.0f, d.cb());
    EXPECT_NEAR(host.EffectiveOffset(), 0.0f, 0.5f);
}

TEST(ScrollContentHost, AnimateToStartsCompositorTweenToTarget) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    DrawCounter d;

    host.AnimateTo(800.0f, d.cb());
    EXPECT_TRUE(host.IsAnimating());
    EXPECT_NEAR(host.TargetOffset(), 800.0f, 0.5f);

    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content && content->IsAnimatingOffsetY());
    // The tween's destination OffsetY == (surfaceOrigin - target) * dpi.
    const float expectedTo = host.SurfaceOrigin() - 800.0f;
    EXPECT_NEAR(content->offsetYAnim->toPx, expectedTo, 0.5f);
    EXPECT_TRUE(backend.commitRequests >= 1);
}

// A rebase (triggered by scrolling far enough to near the drawn edge) moves the
// surface origin but preserves the logical/effective offset — the screen does not
// jump. This is the core no-jump invariant (§11.2).
TEST(ScrollContentHost, RebasePreservesEffectiveOffset) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 20000.0f);  // tall content so we can jump far
    DrawCounter d;

    host.SetOffsetImmediate(0.0f, d.cb());
    const float origin0 = host.SurfaceOrigin();

    // Jump far: forces a rebase to a new surface origin.
    host.SetOffsetImmediate(8000.0f, d.cb());
    const float origin1 = host.SurfaceOrigin();

    EXPECT_NE(origin0, origin1);                        // surface moved
    EXPECT_NEAR(host.EffectiveOffset(), 8000.0f, 0.5f); // logical offset preserved
    // And the content visual reflects the same effective offset via the affine map.
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_NEAR(content->offsetY, host.SurfaceOrigin() - 8000.0f, 0.5f);
}

TEST(ScrollContentHost, EnsureContentDrawsAndPositions) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    DrawCounter d;

    host.EnsureContent(d.cb());  // first paint: must rasterize the surface once
    // Under the fake backend DrawSurface hands the callback a null DC (nothing to
    // rasterize headless), so the content callback body is skipped by design — the
    // observable signal that a refill happened is the recorded DrawSurface call.
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    EXPECT_TRUE(content && content->drawCount >= 1);
}

TEST(ScrollContentHost, DeviceLostPreservesOffsetAndRestores) {
    // Both backends declared BEFORE host so they outlive it — the host holds a
    // borrowed backend pointer and unparents from it in its destructor, mirroring
    // the real app where the window-owned backend always outlives its controls.
    FakeCompositionBackend backend;
    FakeCompositionBackend backend2;  // fresh device (after a loss)
    ScrollContentHost host;
    Setup(host, backend);
    DrawCounter d;
    host.SetOffsetImmediate(1500.0f, d.cb());

    host.OnDeviceLost();
    EXPECT_FALSE(host.Valid());
    EXPECT_NEAR(host.TargetOffset(), 1500.0f, 0.5f);  // state preserved

    HRESULT hr = host.OnDeviceRestored(&backend2, 1.0f);
    EXPECT_TRUE(SUCCEEDED(hr));
    EXPECT_TRUE(host.Valid());
    EXPECT_NEAR(host.EffectiveOffset(), 1500.0f, 0.5f);  // back where we were
    EXPECT_EQ(backend2.RootCount(), 1);
}

TEST(ScrollContentVisibility, HideDetachesOnceAndShowReattachesOnce) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);

    const int beforeHide = backend.commitRequests;
    host.SetTreeVisible(false);
    EXPECT_FALSE(host.TreeVisible());
    EXPECT_EQ(backend.RootCount(), 0);
    EXPECT_TRUE(backend.commitRequests > beforeHide);

    const int afterHide = backend.commitRequests;
    host.SetTreeVisible(false);
    EXPECT_EQ(backend.RootCount(), 0);
    EXPECT_EQ(backend.commitRequests, afterHide);

    host.SetTreeVisible(true);
    EXPECT_TRUE(host.TreeVisible());
    EXPECT_EQ(backend.RootCount(), 1);
    const int afterShow = backend.commitRequests;
    host.SetTreeVisible(true);
    EXPECT_EQ(backend.RootCount(), 1);
    EXPECT_EQ(backend.commitRequests, afterShow);
}

TEST(ScrollContentVisibility, HiddenHostDoesNotRasterizeOrCommit) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    auto* content = ContentOf(backend);
    auto* overlay = OverlayOf(backend);
    EXPECT_TRUE(content != nullptr);
    EXPECT_TRUE(overlay != nullptr);

    host.SetTreeVisible(false);
    const int contentDraws = content ? content->drawCount : 0;
    const int overlayDraws = overlay ? overlay->drawCount : 0;
    const int commits = backend.commitRequests;
    DrawCounter d;
    host.EnsureContent(d.cb(), true);
    host.RedrawOverlay([](ID2D1DeviceContext*, float, float) {});
    host.SetOffsetImmediate(500.0f, d.cb());

    if (content) EXPECT_EQ(content->drawCount, contentDraws);
    if (overlay) EXPECT_EQ(overlay->drawCount, overlayDraws);
    EXPECT_EQ(backend.commitRequests, commits);
    EXPECT_NEAR(host.TargetOffset(), 500.0f, 0.5f);
}

TEST(ScrollContentVisibility, HideStopsTweenAndCaretBlink) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    DrawCounter d;
    host.AnimateTo(800.0f, d.cb());
    const D2D1_COLOR_F ink = {0.1f, 0.1f, 0.1f, 1.0f};
    host.SetCaret({12.0f, 40.0f, 1.0f, 18.0f}, ink);
    host.SetCaretVisible(true);
    host.StartCaretBlink(0.53);
    auto* content = ContentOf(backend);
    auto* caret = content && !content->children.empty()
        ? static_cast<FakeCompositionVisual*>(content->children[0]) : nullptr;
    EXPECT_TRUE(content && content->IsAnimatingOffsetY());
    EXPECT_TRUE(caret && caret->IsBlinking());

    host.SetTreeVisible(false);
    EXPECT_FALSE(host.IsAnimating());
    EXPECT_FALSE(content && content->IsAnimatingOffsetY());
    EXPECT_FALSE(caret && caret->IsBlinking());

    host.SetTreeVisible(true);
    EXPECT_TRUE(caret && caret->IsBlinking());
}

TEST(ScrollContentVisibility, HiddenStateSurvivesDeviceRestore) {
    FakeCompositionBackend backend;
    FakeCompositionBackend backend2;
    ScrollContentHost host;
    Setup(host, backend);
    host.SetTreeVisible(false);
    host.OnDeviceLost();

    EXPECT_TRUE(SUCCEEDED(host.OnDeviceRestored(&backend2, 1.0f)));
    EXPECT_TRUE(host.Valid());
    EXPECT_FALSE(host.TreeVisible());
    EXPECT_EQ(backend2.RootCount(), 0);

    host.SetTreeVisible(true);
    EXPECT_EQ(backend2.RootCount(), 1);
}

// --- Atlas transform contract (regression: scrollbar/frame flicker) -----------
// A composition surface is a TILE inside a shared atlas texture, and DComp may hand
// back a DIFFERENT tile origin on each BeginDraw. Code that sets its own transform
// must premultiply onto the incoming one; replacing it drops the tile translation, so
// the drawing lands outside the visual's tile and flickers as tiles get reassigned.

TEST(SurfaceTransform, PreservesAtlasTileTranslation) {
    // Tile at (512, 256) in the atlas; control at window origin, DPI 1.
    D2D1_MATRIX_3X2_F base = D2D1::Matrix3x2F::Translation(512.0f, 256.0f);
    D2D1_MATRIX_3X2_F m = SurfaceTransformFromWindowDip(base, 0.0f, 0.0f, 1.0f);
    // The control's (0,0) must map to the tile origin, not to the surface's (0,0).
    EXPECT_NEAR(m.dx, 512.0f, 0.01f);
    EXPECT_NEAR(m.dy, 256.0f, 0.01f);
}

TEST(SurfaceTransform, MapsWindowDipToTileLocalPixels) {
    // Control bounds start at window DIP (40, 90); DPI 2; tile at (8, 4).
    D2D1_MATRIX_3X2_F base = D2D1::Matrix3x2F::Translation(8.0f, 4.0f);
    D2D1_MATRIX_3X2_F m = SurfaceTransformFromWindowDip(base, 40.0f, 90.0f, 2.0f);
    EXPECT_NEAR(m.m11, 2.0f, 0.01f);   // DIP -> px scale survives
    EXPECT_NEAR(m.m22, 2.0f, 0.01f);
    // A point at the control's top-left must land exactly on the tile origin:
    // (40 - 40) * 2 + 8 = 8, (90 - 90) * 2 + 4 = 4.
    EXPECT_NEAR(40.0f * m.m11 + 90.0f * m.m21 + m.dx, 8.0f, 0.01f);
    EXPECT_NEAR(40.0f * m.m12 + 90.0f * m.m22 + m.dy, 4.0f, 0.01f);
    // A point 10 DIP right / 5 DIP down lands 20 px / 10 px into the tile.
    EXPECT_NEAR(50.0f * m.m11 + 95.0f * m.m21 + m.dx, 28.0f, 0.01f);
    EXPECT_NEAR(50.0f * m.m12 + 95.0f * m.m22 + m.dy, 14.0f, 0.01f);
}

TEST(SurfaceTransform, IdentityBaseGivesPlainDipToPixelMap) {
    // With no atlas offset the result is the straightforward map, so the premultiply
    // does not perturb the common case.
    D2D1_MATRIX_3X2_F base = D2D1::Matrix3x2F::Identity();
    D2D1_MATRIX_3X2_F m = SurfaceTransformFromWindowDip(base, 10.0f, 20.0f, 1.5f);
    EXPECT_NEAR(10.0f * m.m11 + m.dx, 0.0f, 0.01f);
    EXPECT_NEAR(20.0f * m.m22 + m.dy, 0.0f, 0.01f);
    EXPECT_NEAR(30.0f * m.m11 + m.dx, 30.0f, 0.01f);  // (30-10)*1.5
}

// --- Overscan buffer coverage (the §11.2 refill/rebase contract) --------------
// Viewport is 600 tall at DPI 1, so overscan = min(600*1.5, 1800) = 900 per side
// and the drawn surface spans 2400 DIP. The refill margin is 600 * 0.35 = 210: the
// view must stay at least that far from a drawn edge, EXCEPT where the drawn edge
// is a real content boundary (content 0 / contentHeight), which never needs a
// refill. (The overscan frac went 1.0 -> 1.5 when fast-wheel flings were found to
// outrun a 1-viewport margin; the assertions below read SurfaceOrigin() live, so
// they stay correct at any frac.)

TEST(ScrollContentHost, ScrollInsideBufferDoesNotRefill) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    DrawCounter d;

    host.SetOffsetImmediate(2000.0f, d.cb());   // rebase centered here
    FakeCompositionVisual* content = ContentOf(backend);
    const int drawsAfterRebase = content->drawCount;
    const float originAfterRebase = host.SurfaceOrigin();

    // A small move stays well inside the drawn surface: reposition only, no redraw.
    host.SetOffsetImmediate(2100.0f, d.cb());
    EXPECT_EQ(content->drawCount, drawsAfterRebase);
    EXPECT_NEAR(host.SurfaceOrigin(), originAfterRebase, 0.5f);
    EXPECT_NEAR(content->offsetY, host.SurfaceOrigin() - 2100.0f, 0.5f);
}

TEST(ScrollContentHost, CrossingRefillMarginRebases) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    DrawCounter d;

    host.SetOffsetImmediate(2000.0f, d.cb());
    FakeCompositionVisual* content = ContentOf(backend);
    const int drawsBefore = content->drawCount;
    const float originBefore = host.SurfaceOrigin();

    // Walk the view's bottom edge past the drawn edge: must refill AND recenter,
    // while the logical offset (what the user sees) is untouched — "no jump".
    host.SetOffsetImmediate(3200.0f, d.cb());
    EXPECT_TRUE(content->drawCount > drawsBefore);
    EXPECT_NE(host.SurfaceOrigin(), originBefore);
    EXPECT_NEAR(host.EffectiveOffset(), 3200.0f, 0.5f);
    EXPECT_NEAR(content->offsetY, host.SurfaceOrigin() - 3200.0f, 0.5f);
}

TEST(ScrollContentHost, RestingAtContentTopNeedsNoRefill) {
    // The surface's top edge IS content 0 here, so being right against it is not a
    // reason to refill — otherwise sitting at the top would redraw every tick.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    DrawCounter d;

    host.SetOffsetImmediate(0.0f, d.cb());
    FakeCompositionVisual* content = ContentOf(backend);
    const int draws = content->drawCount;
    EXPECT_NEAR(host.SurfaceOrigin(), 0.0f, 0.5f);  // clamped, no empty space above

    host.EnsureContent(d.cb());
    host.EnsureContent(d.cb());
    EXPECT_EQ(content->drawCount, draws);           // still no redraw
}

TEST(ScrollContentHost, RestingAtContentBottomNeedsNoRefill) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    DrawCounter d;

    host.SetOffsetImmediate(host.MaxOffset(), d.cb());   // 4400
    FakeCompositionVisual* content = ContentOf(backend);
    const int draws = content->drawCount;
    // Surface bottom lands exactly on contentHeight (origin clamped to 5000 - 1800).
    EXPECT_NEAR(host.SurfaceOrigin() + host.SurfaceHeight(), 5000.0f, 0.5f);

    host.EnsureContent(d.cb());
    host.EnsureContent(d.cb());
    EXPECT_EQ(content->drawCount, draws);
}

TEST(ScrollContentHost, SurfaceOriginStaysWithinContent) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    DrawCounter d;

    // Sweep the whole range; the drawn window must never hang off either end.
    for (float o = 0.0f; o <= 4400.0f; o += 137.0f) {
        host.SetOffsetImmediate(o, d.cb());
        EXPECT_TRUE(host.SurfaceOrigin() >= -0.5f);
        EXPECT_TRUE(host.SurfaceOrigin() + host.SurfaceHeight() <= 5000.5f);
    }
}

TEST(ScrollContentHost, OverscanIsCappedOnATallViewport) {
    // Overscan is capped per side so a huge viewport does not allocate an enormous
    // surface: 3000 tall viewport → 3000 + 2 * 1800, not 4 * 3000.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    host.Create(&backend, 1.0f);
    host.SetViewport({0, 0, 100, 3000});
    host.SetContentHeight(50000.0f);
    // 3000 + 2*1800 = 6600 → ceil(6600/256)*256 = 6656 (quantized)
    EXPECT_NEAR(host.SurfaceHeight(), 6656.0f, 0.5f);
}

TEST(ScrollContentHost, ContentShorterThanViewportPinsToZero) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    host.Create(&backend, 1.0f);
    host.SetViewport({0, 0, 100, 600});
    host.SetContentHeight(200.0f);          // fits: nothing to scroll

    EXPECT_NEAR(host.MaxOffset(), 0.0f, 0.5f);
    host.SetOffsetImmediate(500.0f, noop);  // clamped away
    EXPECT_NEAR(host.EffectiveOffset(), 0.0f, 0.5f);
    EXPECT_NEAR(host.SurfaceOrigin(), 0.0f, 0.5f);
}

TEST(ScrollContentHost, ContentShrinkingBelowViewportReclampsOffset) {
    // Collapsing a big tree group while scrolled deep must not leave the offset past
    // the new extent (the demo's collapse-a-group case).
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    Setup(host, backend, 5000.0f);
    host.SetOffsetImmediate(4000.0f, noop);
    EXPECT_NEAR(host.EffectiveOffset(), 4000.0f, 0.5f);

    host.SetContentHeight(300.0f);   // group collapsed: content now fits
    EXPECT_NEAR(host.MaxOffset(), 0.0f, 0.5f);
    EXPECT_NEAR(host.TargetOffset(), 0.0f, 0.5f);
}

TEST(ScrollContentHost, EnsureContentLeavesAnInFlightTweenAlone) {
    // While the compositor owns OffsetY, a UI-thread EnsureContent must not rebase
    // or write a static offset — that would fight the running animation.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 20000.0f);
    DrawCounter d;

    host.SetOffsetImmediate(0.0f, d.cb());
    host.AnimateTo(6000.0f, d.cb());
    EXPECT_TRUE(host.IsAnimating());
    FakeCompositionVisual* content = ContentOf(backend);
    const int draws = content->drawCount;
    const float origin = host.SurfaceOrigin();

    host.EnsureContent(d.cb(), /*forceRedraw=*/true);
    EXPECT_EQ(content->drawCount, draws);            // no redraw mid-tween
    EXPECT_NEAR(host.SurfaceOrigin(), origin, 0.5f); // no rebase mid-tween
    EXPECT_TRUE(content->IsAnimatingOffsetY());      // tween still owns OffsetY
}

// --- Text caret (Phase 4) -----------------------------------------------------
// The caret is a child of the CONTENT visual, so it inherits the compositor scroll
// offset (it can never drift from the text) and blinks via a compositor opacity
// animation (no UI timer, and no re-rasterizing of the text surface).

namespace {
FakeCompositionVisual* CaretOf(FakeCompositionBackend& b) {
    FakeCompositionVisual* content = ContentOf(b);
    if (!content || content->children.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(content->children[0]);
}
const D2D1_COLOR_F kInk = {0.1f, 0.1f, 0.1f, 1.0f};
}  // namespace

TEST(ScrollContentHostCaret, ParentedUnderContentSoItInheritsScroll) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);

    EXPECT_FALSE(host.HasCaret());
    host.SetCaret({12.0f, 400.0f, 1.0f, 18.0f}, kInk);
    EXPECT_TRUE(host.HasCaret());
    // Child of content_, NOT of viewport_ — this is what makes it ride the tween.
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_EQ(static_cast<int>(content->children.size()), 1);
    EXPECT_TRUE(CaretOf(backend) != nullptr);
}

TEST(ScrollContentHostCaret, OffsetIsRelativeToTheSurfaceOrigin) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    Setup(host, backend, 5000.0f);
    host.SetOffsetImmediate(0.0f, noop);
    EXPECT_NEAR(host.SurfaceOrigin(), 0.0f, 0.5f);

    host.SetCaret({12.0f, 400.0f, 1.0f, 18.0f}, kInk);
    FakeCompositionVisual* caret = CaretOf(backend);
    // surfaceOrigin 0 → local Y == content Y.
    EXPECT_NEAR(caret->offsetX, 12.0f, 0.5f);
    EXPECT_NEAR(caret->offsetY, 400.0f, 0.5f);
}

TEST(ScrollContentHostCaret, RebaseReanchorsTheCaret) {
    // A rebase moves surfaceOrigin_, so the caret's local offset must be recomputed
    // or it would jump away from its character.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    Setup(host, backend, 20000.0f);
    host.SetCaret({12.0f, 9000.0f, 1.0f, 18.0f}, kInk);

    host.SetOffsetImmediate(9000.0f, noop);   // forces a rebase
    const float origin = host.SurfaceOrigin();
    EXPECT_NE(origin, 0.0f);

    FakeCompositionVisual* caret = CaretOf(backend);
    // Still anchored to content Y 9000, now expressed against the new origin.
    EXPECT_NEAR(caret->offsetY, 9000.0f - origin, 0.5f);
}

TEST(ScrollContentHostCaret, SnapsToWholePixelsAtFractionalDpi) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    host.Create(&backend, 1.5f);
    host.SetViewport({0, 0, 100, 600});
    host.SetContentHeight(5000.0f);

    // 11.3 * 1.5 = 16.95 and 100.7 * 1.5 = 151.05 — both must land on integers, or
    // DWM resamples the 1-px bar across two columns (blurry, appears to wobble).
    host.SetCaret({11.3f, 100.7f, 1.0f, 18.0f}, kInk);
    FakeCompositionVisual* caret = CaretOf(backend);
    EXPECT_NEAR(caret->offsetX, std::round(11.3f * 1.5f), 0.01f);
    EXPECT_NEAR(caret->offsetY, std::round(100.7f * 1.5f), 0.01f);
    EXPECT_NEAR(caret->offsetX - std::floor(caret->offsetX), 0.0f, 0.01f);
}

TEST(ScrollContentHostCaret, BlinkRunsOnTheCompositorOpacity) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetCaret({12.0f, 40.0f, 1.0f, 18.0f}, kInk);
    host.SetCaretVisible(true);
    host.StartCaretBlink(0.53);

    FakeCompositionVisual* caret = CaretOf(backend);
    EXPECT_TRUE(caret != nullptr && caret->IsBlinking());
    // Guard the optional: a regression that never starts the blink must FAIL here,
    // not dereference an empty optional (UB).
    if (caret && caret->opacityAnim.has_value())
        EXPECT_NEAR(caret->opacityAnim->halfPeriodSec, 0.53, 1e-9);
}

TEST(ScrollContentHostCaret, RestartingBlinkResetsThePhase) {
    // Typing must make the caret solid immediately rather than leaving it mid-cycle.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetCaret({12.0f, 40.0f, 1.0f, 18.0f}, kInk);
    host.SetCaretVisible(true);
    host.StartCaretBlink(0.53);
    FakeCompositionVisual* caret = CaretOf(backend);
    EXPECT_TRUE(caret != nullptr && caret->opacityAnim.has_value());
    if (!caret || !caret->opacityAnim.has_value()) return;
    const int first = caret->opacityAnim->blinkStarts;

    host.StartCaretBlink(0.53);
    EXPECT_TRUE(caret->opacityAnim->blinkStarts > first);
}

TEST(ScrollContentHostCaret, HidingDropsTheBlinkAnimation) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetCaret({12.0f, 40.0f, 1.0f, 18.0f}, kInk);
    host.SetCaretVisible(true);
    host.StartCaretBlink(0.53);
    FakeCompositionVisual* caret = CaretOf(backend);
    EXPECT_TRUE(caret->IsBlinking());

    host.SetCaretVisible(false);   // focus lost
    EXPECT_FALSE(caret->IsBlinking());
    EXPECT_NEAR(caret->opacity, 0.0f, 0.01f);

    host.SetCaretVisible(true);    // focus back: blink re-arms itself
    EXPECT_TRUE(caret->IsBlinking());
}

TEST(ScrollContentHostCaret, SurvivesDeviceLossWithBlinkRearmed) {
    FakeCompositionBackend backend;
    FakeCompositionBackend backend2;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetCaret({12.0f, 400.0f, 1.0f, 18.0f}, kInk);
    host.SetCaretVisible(true);
    host.StartCaretBlink(0.53);

    host.OnDeviceLost();
    EXPECT_FALSE(host.HasCaret());          // visual gone with the device

    EXPECT_TRUE(SUCCEEDED(host.OnDeviceRestored(&backend2, 1.0f)));
    EXPECT_TRUE(host.HasCaret());           // rebuilt from the retained rect
    FakeCompositionVisual* caret = CaretOf(backend2);
    EXPECT_TRUE(caret != nullptr);
    EXPECT_NEAR(caret->offsetY, 400.0f - host.SurfaceOrigin(), 0.5f);
    EXPECT_TRUE(caret->IsBlinking());       // and still blinking
}

TEST(ScrollContentHostCaret, DpiChangeResizesTheBar) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetCaret({12.0f, 40.0f, 1.0f, 18.0f}, kInk);
    FakeCompositionVisual* caret = CaretOf(backend);
    const uint32_t h1 = caret->lastDrawH;

    host.SetDpiScale(2.0f);
    EXPECT_TRUE(caret->lastDrawH > h1);     // 18 DIP is twice the pixels at 2x
    EXPECT_NEAR(caret->offsetX, 24.0f, 0.5f);
}

TEST(ScrollContentHostCaret, NoCaretByDefaultForNonTextControls) {
    // TreeView never calls SetCaret, so the content visual keeps no children and the
    // caret code costs it nothing.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    EXPECT_FALSE(host.HasCaret());
    EXPECT_EQ(static_cast<int>(ContentOf(backend)->children.size()), 0);
}

// --- Content inset (padded scroll region, e.g. a text editor) -----------------
// A text editor scrolls text inside its padding while the scrollbar and frame still
// paint over the full bounds. The inset describes that padded region; the mask for it
// lives on its own visual because a clip on the scrolled surface would slide with the
// scroll offset (see ScrollContentHost.h).

TEST(ScrollContentInset, DefaultsToTheWholeViewport) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    const RectDip r = host.ContentRegion();
    EXPECT_NEAR(r.x, 0.0f, 0.01f);
    EXPECT_NEAR(r.y, 0.0f, 0.01f);
    EXPECT_NEAR(r.w, 100.0f, 0.01f);
    EXPECT_NEAR(r.h, 600.0f, 0.01f);
    EXPECT_NEAR(host.ScrollableHeight(), 600.0f, 0.01f);
}

TEST(ScrollContentInset, ShrinksTheRegionAndTheScrollableHeight) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});

    const RectDip r = host.ContentRegion();
    EXPECT_NEAR(r.x, 10.0f, 0.01f);
    EXPECT_NEAR(r.y, 8.0f, 0.01f);
    EXPECT_NEAR(r.w, 100.0f - 10.0f - 12.0f, 0.01f);
    EXPECT_NEAR(r.h, 600.0f - 16.0f, 0.01f);
    // One "page" is the region, so the reachable offset grows by the padding.
    EXPECT_NEAR(host.ScrollableHeight(), 584.0f, 0.01f);
    EXPECT_NEAR(host.MaxOffset(), 5000.0f - 584.0f, 0.01f);
}

TEST(ScrollContentInset, ClipNodeCarriesTheRegionOffsetAndMask) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});

    FakeCompositionVisual* clip = ClipOf(backend);
    EXPECT_TRUE(clip != nullptr);
    EXPECT_NEAR(clip->offsetX, 10.0f, 0.5f);   // sits at the region's top-left
    EXPECT_NEAR(clip->offsetY, 8.0f, 0.5f);
    EXPECT_TRUE(clip->hasClip);
    EXPECT_NEAR(clip->clipR, 78.0f, 0.5f);     // masks to the region's size
    EXPECT_NEAR(clip->clipB, 584.0f, 0.5f);
}

TEST(ScrollContentInset, OverlayStillCoversTheFullViewport) {
    // The scrollbar lives in the right gutter and the frame on the border — both
    // OUTSIDE the padded text region, so the overlay must not be inset.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});
    host.RedrawOverlay([](ID2D1DeviceContext*, float, float) {});

    FakeCompositionVisual* overlay = OverlayOf(backend);
    EXPECT_TRUE(overlay != nullptr);
    EXPECT_EQ(overlay->lastDrawW, 100u);   // full viewport, not the 78-wide region
    EXPECT_EQ(overlay->lastDrawH, 600u);
}

TEST(ScrollContentInset, ContentSurfaceIsSizedToTheRegion) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    Setup(host, backend, 5000.0f);
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});
    host.EnsureContent(noop);

    FakeCompositionVisual* content = ContentOf(backend);
    // 78 → ceil(78/256)*256 = 256 (quantized width)
    EXPECT_EQ(content->lastDrawW, 256u);
    // Overscan 584 + 2*min(876,1800) = 2336 → ceil(2336/256)*256 = 2560 (quantized)
    EXPECT_NEAR(host.SurfaceHeight(), 2560.0f, 0.5f);
}

TEST(ScrollContentInset, ContentOffsetStaysMeasuredFromTheRegion) {
    // The inset is invisible to the caller's coordinates: content Y 0 is the region's
    // top, because clip_ (not content_) carries the inset translation.
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    Setup(host, backend, 5000.0f);
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});
    host.SetOffsetImmediate(0.0f, noop);

    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_NEAR(content->offsetX, 0.0f, 0.5f);   // NOT 10 — clip_ holds that
    EXPECT_NEAR(content->offsetY, host.SurfaceOrigin(), 0.5f);
}

TEST(ScrollContentInset, ReclampsOffsetWhenTheRegionShrinks) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    ScrollContentHost::DrawContentCallback noop =
        [](ID2D1DeviceContext*, float, float) {};
    Setup(host, backend, 620.0f);            // barely scrollable: max = 20
    host.SetOffsetImmediate(20.0f, noop);
    EXPECT_NEAR(host.EffectiveOffset(), 20.0f, 0.5f);

    // Padding makes the region shorter, so MaxOffset GROWS — the offset stays valid.
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});
    EXPECT_NEAR(host.MaxOffset(), 620.0f - 584.0f, 0.5f);
    EXPECT_NEAR(host.TargetOffset(), 20.0f, 0.5f);
}

TEST(ScrollContentInset, SurvivesDeviceRestore) {
    FakeCompositionBackend backend;
    FakeCompositionBackend backend2;
    ScrollContentHost host;
    Setup(host, backend, 5000.0f);
    host.SetContentInset({10.0f, 8.0f, 12.0f, 8.0f});

    host.OnDeviceLost();
    EXPECT_TRUE(SUCCEEDED(host.OnDeviceRestored(&backend2, 1.0f)));
    // The inset is plain data, so the rebuilt clip node must carry it again.
    FakeCompositionVisual* clip = ClipOf(backend2);
    EXPECT_TRUE(clip != nullptr);
    EXPECT_NEAR(clip->offsetX, 10.0f, 0.5f);
    EXPECT_NEAR(clip->clipB, 584.0f, 0.5f);
    EXPECT_NEAR(host.ScrollableHeight(), 584.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Sub-tree opacity
// ---------------------------------------------------------------------------
// A composited control paints into its own surfaces, so an opacity folded into the
// host window's DrawingContext never reaches those pixels. It goes onto the
// VIEWPORT visual instead: one property, applied by the compositor to the composed
// result (content + overlay + caret) with no re-rasterization.

namespace {
FakeCompositionVisual* ViewportOf(FakeCompositionBackend& b) {
    if (b.rootVisuals.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(b.rootVisuals[0]);
}
}  // namespace

TEST(ScrollContentOpacity, DefaultsToOpaque) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    EXPECT_NEAR(host.Opacity(), 1.0f, 0.0001f);
    FakeCompositionVisual* vp = ViewportOf(backend);
    EXPECT_TRUE(vp != nullptr);
    if (vp) EXPECT_NEAR(vp->opacity, 1.0f, 0.0001f);
}

// The opacity lands on the VIEWPORT (the container), not on the content visual:
// putting it on content_ would leave the scrollbar/focus overlay fully opaque
// while the rows faded.
TEST(ScrollContentOpacity, AppliesToViewportNotContent) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    host.SetOpacity(0.5f);

    FakeCompositionVisual* vp = ViewportOf(backend);
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(vp != nullptr);
    EXPECT_TRUE(content != nullptr);
    if (vp) EXPECT_NEAR(vp->opacity, 0.5f, 0.0001f);
    // content stays untouched: the compositor fades the composite as a whole.
    if (content) EXPECT_NEAR(content->opacity, 1.0f, 0.0001f);
}

TEST(ScrollContentOpacity, Clamps) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    host.SetOpacity(5.0f);
    EXPECT_NEAR(host.Opacity(), 1.0f, 0.0001f);
    host.SetOpacity(-1.0f);
    EXPECT_NEAR(host.Opacity(), 0.0f, 0.0001f);
}

// Setting opacity must publish: without a commit the change sits in the tree
// unseen until something else happens to commit.
TEST(ScrollContentOpacity, RequestsCommit) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    const int before = backend.commitRequests;
    host.SetOpacity(0.5f);
    EXPECT_TRUE(backend.commitRequests > before);
}

// A redundant set neither re-pushes nor commits (opacity is often re-asserted from
// RefreshComposition, which runs on every scroll tick).
TEST(ScrollContentOpacity, RedundantSetDoesNotCommit) {
    FakeCompositionBackend backend;
    ScrollContentHost host;
    Setup(host, backend);
    host.SetOpacity(0.5f);
    const int after = backend.commitRequests;
    host.SetOpacity(0.5f);
    EXPECT_EQ(backend.commitRequests, after);
}

// Device loss rebuilds every visual from scratch (they default to opaque), so the
// cached opacity must be re-applied or a faded control silently pops back to full.
TEST(ScrollContentOpacity, SurvivesDeviceRestore) {
    FakeCompositionBackend backend;
    FakeCompositionBackend backend2;
    ScrollContentHost host;
    Setup(host, backend);
    host.SetOpacity(0.25f);

    host.OnDeviceLost();
    EXPECT_TRUE(SUCCEEDED(host.OnDeviceRestored(&backend2, 1.0f)));
    FakeCompositionVisual* vp = ViewportOf(backend2);
    EXPECT_TRUE(vp != nullptr);
    if (vp) EXPECT_NEAR(vp->opacity, 0.25f, 0.0001f);
    EXPECT_NEAR(host.Opacity(), 0.25f, 0.0001f);
}
