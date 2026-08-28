// TextAreaCompositionTests.cpp — headless tests that TextArea drives the overscan
// scroll host correctly in composition mode (Phase 4), and that it falls back to the
// UI-thread scroll path with no backend.
//
// No GPU: a MockHost hands the TextArea a FakeCompositionBackend through the attach
// context. DrawSurface gets a null DC, so these assert on the compositor STATE MACHINE
// — visuals created, the padded text region, wheel starting an OffsetY tween, the caret
// visual and its blink, IME caret placement using the effective offset — not pixels.
// Pixel correctness is a manual on-hardware check.
//
// DWrite IS real here (the factory works without a window), so text metrics, wrapping
// and caret positions are genuine.

#include "../framework/Test.h"
#include "../framework/FakeCompositionBackend.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/layout/Border.h"
#include "../../FluentUI/layout/StackPanel.h"
#include <string>

using namespace fluent;
using fltest::FakeCompositionBackend;
using fltest::FakeCompositionVisual;

namespace {

class MockHost : public WindowServices {
public:
    explicit MockHost(ICompositionBackend* backend) : backend_(backend) {
        dwrite_.Initialize();   // real DWrite: text metrics must be genuine
    }
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
    bool DWriteReady() const { return dwrite_.Valid(); }
private:
    ICompositionBackend* backend_ = nullptr;
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host, AnimationRegistry& anims) {
    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    // TextArea measures its scrollable extent from a real DWrite layout, so the
    // context MUST carry the factory — without it contentHeight_ is 0 and nothing is
    // scrollable, which would silently pass the "no scroll" tests for the wrong reason.
    ctx.dwrite = &host.DWrite();
    ctx.dpiScale = 1.0f;
    return ctx;
}

// Tree: root -> viewport_ -> { clip_ -> content_ [-> caret_], overlay_ }.
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
FakeCompositionVisual* CaretOf(FakeCompositionBackend& b) {
    FakeCompositionVisual* content = ContentOf(b);
    if (!content || content->children.empty()) return nullptr;
    return static_cast<FakeCompositionVisual*>(content->children[0]);
}

// Enough lines to be comfortably taller than the viewport.
std::wstring ManyLines(int n) {
    std::wstring s;
    for (int i = 0; i < n; ++i) s += L"line of text\n";
    return s;
}

// A 300x120 area with 200 lines of text — scrollable by a wide margin.
void Setup(TextArea& area, MockHost& host, AnimationRegistry& anims) {
    area.SetText(ManyLines(200));
    area.SetBounds({20, 40, 300, 120});
    area.AttachToContext(MakeCtx(host, anims));
}

}  // namespace

TEST(TextAreaComposition, AttachBuildsCompositionTree) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;   // no DirectWrite on this box: skip

    TextArea area;
    Setup(area, host, anims);

    EXPECT_EQ(backend.RootCount(), 1);
    // viewport + clip + content + overlay, plus the caret is only built on focus.
    EXPECT_TRUE(backend.createdVisuals >= 4);
    auto* viewport = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    EXPECT_EQ(static_cast<int>(viewport->children.size()), 2);  // clip + overlay
    EXPECT_TRUE(viewport->hasClip);
    EXPECT_TRUE(ContentOf(backend) != nullptr);

    area.DetachFromContext();
}

TEST(TextAreaComposition, AncestorCollapseThroughBorderDetachesCompositionTree) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    StackPanel root;
    auto border = std::make_unique<Border>();
    auto area = std::make_unique<TextArea>();
    TextArea* areaRaw = area.get();
    areaRaw->SetText(ManyLines(200));
    areaRaw->SetBounds({20, 40, 300, 120});
    border->SetChild(std::move(area));
    root.Add(std::move(border));
    root.AttachToContext(MakeCtx(host, anims));
    EXPECT_EQ(backend.RootCount(), 1);

    root.SetVisible(false);
    EXPECT_EQ(backend.RootCount(), 0);
    EXPECT_FALSE(areaRaw->WantsAnimationTick());

    root.SetVisible(true);
    EXPECT_EQ(backend.RootCount(), 1);
    root.DetachFromContext();
}

TEST(TextAreaComposition, TextRegionIsInsetByThePadding) {
    // The text scrolls inside the padding and left of the scrollbar gutter, while the
    // scrollbar / frame paint over the full bounds — that is what the clip node holds.
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);

    FakeCompositionVisual* clip = ClipOf(backend);
    EXPECT_TRUE(clip != nullptr);
    EXPECT_TRUE(clip->hasClip);
    // Padding is 10 (x) / 8 (y) with a 12 DIP scrollbar reserve, so the region is
    // inset from the left/top and narrower than the 300 DIP bounds.
    EXPECT_TRUE(clip->offsetX > 0.0f);
    EXPECT_TRUE(clip->offsetY > 0.0f);
    EXPECT_TRUE(clip->clipR < 300.0f);
    EXPECT_TRUE(clip->clipB < 120.0f);

    area.DetachFromContext();
}

TEST(TextAreaComposition, OverlayCoversTheFullBounds) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);

    auto* viewport = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
    auto* overlay = static_cast<FakeCompositionVisual*>(viewport->children[1]);
    EXPECT_EQ(overlay->lastDrawW, 300u);   // full bounds, not the padded region
    EXPECT_EQ(overlay->lastDrawH, 120u);

    area.DetachFromContext();
}

TEST(TextAreaComposition, WheelStartsCompositorTween) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);

    PointerEventArgs e{};
    e.position = {100.0f, 80.0f};
    e.wheelDelta = -WHEEL_DELTA;    // one notch down
    area.OnPointerWheelChanged(e);

    EXPECT_TRUE(e.handled);
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    // The smooth scroll is an OffsetY tween on the compositor, not a UI-thread loop.
    EXPECT_TRUE(content && content->IsAnimatingOffsetY());
    EXPECT_TRUE(content && content->offsetY < 0.0f);  // surface slid up

    area.DetachFromContext();
}

TEST(TextAreaComposition, WheelNoScrollWhenTextFits) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    area.SetText(L"one line");
    area.SetBounds({20, 40, 300, 120});
    area.AttachToContext(MakeCtx(host, anims));

    PointerEventArgs e{};
    e.position = {100.0f, 80.0f};
    e.wheelDelta = -WHEEL_DELTA;
    area.OnPointerWheelChanged(e);

    // Nothing to scroll: the event bubbles (a parent scroller may take it).
    EXPECT_FALSE(e.handled);
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_FALSE(content && content->IsAnimatingOffsetY());

    area.DetachFromContext();
}

TEST(TextAreaComposition, CaretIsItsOwnVisualUnderTheText) {
    // The caret must be a child of the content visual: that is what makes it ride a
    // compositor scroll tween and blink without re-rasterizing the text surface.
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);
    area.SetFocused(true);

    FakeCompositionVisual* caret = CaretOf(backend);
    EXPECT_TRUE(caret != nullptr);
    EXPECT_TRUE(caret && caret->IsBlinking());        // blink runs on the compositor
    // Guard the optional: without the guard a regression that never starts the blink
    // would dereference an empty optional here (UB) instead of reporting a failure.
    EXPECT_TRUE(caret && caret->opacityAnim.has_value() &&
                caret->opacityAnim->halfPeriodSec > 0.0);

    area.DetachFromContext();
}

TEST(TextAreaComposition, NoWindowBlinkTimerInCompositionMode) {
    // The window's blink timer repaints the WHOLE window every half period; with the
    // compositor owning the blink that cost is not needed.
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);
    EXPECT_FALSE(area.WantsBlink());

    area.DetachFromContext();
    // Detached (no backend) it falls back to the UI-thread blink.
    EXPECT_TRUE(area.WantsBlink());
}

TEST(TextAreaComposition, LosingFocusHidesTheCaret) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);
    area.SetFocused(true);
    FakeCompositionVisual* caret = CaretOf(backend);
    EXPECT_TRUE(caret && caret->IsBlinking());

    area.SetFocused(false);
    EXPECT_FALSE(caret->IsBlinking());
    EXPECT_NEAR(caret->opacity, 0.0f, 0.01f);

    area.DetachFromContext();
}

TEST(TextAreaComposition, CaretRectUsesTheEffectiveOffsetForIme) {
    // The IME candidate window is placed from CaretRectDip. If it read the tween's
    // TARGET instead of the effective offset it would drift away from the visible
    // caret mid-scroll, putting the candidate list in the wrong place.
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);
    area.SetFocused(true);

    RectDip before{};
    EXPECT_TRUE(area.CaretRectDip(before));

    // Start a long fling; the effective offset is still ~0 at t=0, so the reported
    // caret must not have jumped to where it will END UP.
    PointerEventArgs e{};
    e.position = {100.0f, 80.0f};
    e.wheelDelta = -WHEEL_DELTA * 10;
    area.OnPointerWheelChanged(e);

    RectDip during{};
    EXPECT_TRUE(area.CaretRectDip(during));
    EXPECT_NEAR(during.y, before.y, 12.0f);   // still near the pre-fling position

    area.DetachFromContext();
}

TEST(TextAreaComposition, NoBackendFallsBackToUiScroll) {
    // Null backend: content_ stays null and every path uses the UI-thread scroll_,
    // exactly as before Phase 4 (this is the revert-safety valve).
    MockHost host(nullptr);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);

    PointerEventArgs e{};
    e.position = {100.0f, 80.0f};
    e.wheelDelta = -WHEEL_DELTA;
    area.OnPointerWheelChanged(e);
    EXPECT_TRUE(e.handled);          // the UI-thread scroller took it
    EXPECT_TRUE(area.WantsBlink());  // and the window blink timer is still used

    area.DetachFromContext();
}

TEST(TextAreaComposition, TypingKeepsTheCaretInView) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    Setup(area, host, anims);
    area.SetFocused(true);

    // Ctrl+End equivalent: walk the caret to the very end of the buffer.
    KeyEventArgs k{};
    k.vk = VK_END;
    for (int i = 0; i < 3; ++i) area.OnKeyDownRouted(k);

    // Selecting the last line must have scrolled the view down to it.
    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);
    RectDip caretRect{};
    EXPECT_TRUE(area.CaretRectDip(caretRect));
    // The caret has to be inside the control's bounds, not off the bottom.
    EXPECT_TRUE(caretRect.y >= 40.0f - 1.0f);
    EXPECT_TRUE(caretRect.y <= 40.0f + 120.0f + 1.0f);

    area.DetachFromContext();
}

// ---------------------------------------------------------------------------
// Layout-width gating (LayoutWidthKnown / EnsureLayout)
//
// Setting text before a layout pass used to wrap the whole document at ContentWidth()'s
// 1 DIP clamp floor — one line per character, the most expensive wrap available, and
// discarded the moment real bounds arrived. These pin that the build is deferred and
// that nothing downstream is left wrong by the deferral.
// ---------------------------------------------------------------------------

// SetText with no bounds must not produce a scrollable extent — there is no width to
// wrap against yet, so the honest extent is "unknown", not "one line per character".
TEST(TextAreaComposition, TextSetBeforeLayoutDoesNotWrapAtTheClampFloor) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    // Attach FIRST (DWrite available), then set text while bounds are still zero. This
    // is the order the demo hits: content is seeded before the first Arrange.
    area.AttachToContext(MakeCtx(host, anims));
    area.SetText(ManyLines(200));

    FakeCompositionVisual* content = ContentOf(backend);
    EXPECT_TRUE(content != nullptr);

    // THE ASSERTIONS THAT MATTER. No width is known, so no layout was built and no
    // extent was measured. Had it wrapped at the 1 DIP floor, "line of text" would
    // become one visual line per character — ~2400 visual lines for this input — and
    // contentHeight_ would be in the tens of thousands of DIPs.
    EXPECT_TRUE(!area.LayoutWidthKnown());
    EXPECT_NEAR(area.MeasuredContentHeightDip(), 0.0f, 0.001f);

    // And nothing is scrollable yet, so a wheel notch is NOT consumed — it bubbles to an
    // ancestor scroller, which is right for a control that does not know its own size.
    // (Without the extent gate this passed for the wrong reason: a zero-height region
    // plus the bare padding produced a positive MaxOffset.)
    PointerEventArgs e{};
    e.position = {5.0f, 5.0f};
    e.wheelDelta = -WHEEL_DELTA;
    area.OnPointerWheelChanged(e);
    EXPECT_TRUE(!e.handled);

    // The caret query must still answer (the IME path calls it regardless of layout).
    RectDip caretRect{};
    EXPECT_TRUE(area.CaretRectDip(caretRect));

    area.DetachFromContext();
}

// ...and once real bounds arrive, the extent appears. This is the other half: the
// deferral must not leave the control permanently unscrollable.
TEST(TextAreaComposition, ExtentAppearsOnceBoundsArrive) {
    FakeCompositionBackend backend;
    MockHost host(&backend);
    AnimationRegistry anims;
    if (!host.DWriteReady()) return;

    TextArea area;
    area.AttachToContext(MakeCtx(host, anims));
    area.SetText(ManyLines(200));
    EXPECT_TRUE(!area.LayoutWidthKnown());
    // The Arrange that a real layout pass would do.
    area.SetBounds({20, 40, 300, 120});

    // The width is known now, so the extent is measured — and it is a PLAUSIBLE extent
    // for 200 short lines wrapped at ~278 DIP (a few thousand DIP), not the tens of
    // thousands a per-character wrap would have produced.
    EXPECT_TRUE(area.LayoutWidthKnown());
    const float extent = area.MeasuredContentHeightDip();
    EXPECT_TRUE(extent > 120.0f);      // taller than the viewport: scrollable
    EXPECT_TRUE(extent < 10000.0f);    // but nowhere near a 1-DIP wrap

    // And scrolling works: a wheel notch is consumed and starts a compositor tween.
    PointerEventArgs e{};
    e.position = {100.0f, 80.0f};
    e.wheelDelta = -WHEEL_DELTA;
    area.OnPointerWheelChanged(e);
    EXPECT_TRUE(e.handled);

    area.DetachFromContext();
}

// ---------------------------------------------------------------------------
// DisplayText: reference, not copy
// ---------------------------------------------------------------------------

// With no IME composition the layout string IS the buffer, so DisplayText must hand back
// a reference to it and leave the scratch buffer untouched. Returning by value here used
// to copy the entire document on every call, several times per frame.
TEST(TextAreaComposition, DisplayTextAliasesTheBufferWithNoComposition) {
    // Exposes the protected hook so the aliasing is observable from a test.
    struct Probe : TextArea {
        const std::wstring& Display(std::wstring& scratch) const {
            return DisplayText(scratch);
        }
        size_t Length() const { return DisplayLength(); }
        bool Empty() const { return DisplayEmpty(); }
    };
    Probe area;
    area.SetText(L"hello world");

    std::wstring scratch;
    const std::wstring& disp = area.Display(scratch);
    // Same object as the buffer: no copy was made.
    EXPECT_TRUE(&disp == &area.Text());
    EXPECT_TRUE(scratch.empty());
    // The cheap accessors agree with it, without building anything.
    EXPECT_EQ(static_cast<int>(area.Length()), static_cast<int>(disp.size()));
    EXPECT_TRUE(!area.Empty());

    Probe blank;
    EXPECT_TRUE(blank.Empty());
    EXPECT_EQ(static_cast<int>(blank.Length()), 0);
}
