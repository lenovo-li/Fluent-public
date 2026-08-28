// TextEditInteractionTests.cpp — double/triple-click selection, drag granularity, and
// the selection-changed notification that composited subclasses depend on.
//
// The interesting property here is not "does a double-click select a word" — that is
// WordBoundary's job and is tested there. It is the PLUMBING: that a click count reaches
// the selection logic at all, that a drag started by a double-click keeps extending by
// words, and that a selection change made WITHOUT going through MoveCaret still tells a
// composited subclass to re-rasterize.
//
// That last one is the bug this file exists for. Ctrl+A set the fields and called
// Invalidate(), which for a composited TextArea reaches none of the pixels holding the
// highlight — so select-all appeared to do nothing until an unrelated scroll happened to
// refill the surface. A test that only inspected caret_/selAnchor_ would have passed
// throughout, which is why the assertion below is on the SURFACE DRAW COUNT.
#include "../framework/Test.h"
#include "../framework/FakeCompositionBackend.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/core/UIContext.h"
#include <string>

using namespace fluent;
using fltest::FakeCompositionBackend;
using fltest::FakeCompositionVisual;

namespace {

class MockHost : public WindowServices {
public:
    explicit MockHost(ICompositionBackend* backend = nullptr) : backend_(backend) {
        dwrite_.Initialize();   // real DWrite: hit-test offsets must be genuine
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
    bool Ready() const { return dwrite_.Valid(); }
private:
    ICompositionBackend* backend_ = nullptr;
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host, AnimationRegistry& anims, InputManager& input) {
    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dwrite = &host.DWrite();
    ctx.input = &input;
    ctx.dpiScale = 1.0f;
    return ctx;
}

// A TextArea with no composition backend — the UI-thread path. Enough for everything
// about selection arithmetic; the composited fixture below is only needed for the
// re-rasterize assertion.
struct AreaFixture {
    MockHost host;
    AnimationRegistry anims;
    InputManager input;
    TextArea area;

    bool Ready() const { return host.Ready(); }

    void Open(const std::wstring& text) {
        area.SetWrapMode(TextWrapMode::NoWrap);
        area.SetText(text);
        area.SetBounds({0, 0, 400, 200});
        area.AttachToContext(MakeCtx(host, anims, input));
    }
    ~AreaFixture() { area.DetachFromContext(); }
};

// Composited: the surface draw count is the observable for "the highlight was redrawn".
struct CompositedFixture {
    FakeCompositionBackend backend;
    MockHost host{&backend};
    AnimationRegistry anims;
    InputManager input;
    TextArea area;

    bool Ready() const { return host.Ready(); }

    void Open(const std::wstring& text) {
        area.SetWrapMode(TextWrapMode::NoWrap);
        area.SetText(text);
        area.SetBounds({0, 0, 400, 200});
        area.AttachToContext(MakeCtx(host, anims, input));
    }
    ~CompositedFixture() { area.DetachFromContext(); }

    // Tree: root -> viewport -> { clip -> content, overlay }.
    FakeCompositionVisual* Content() {
        if (backend.rootVisuals.empty()) return nullptr;
        auto* vp = static_cast<FakeCompositionVisual*>(backend.rootVisuals[0]);
        if (vp->children.empty()) return nullptr;
        auto* clip = static_cast<FakeCompositionVisual*>(vp->children[0]);
        if (clip->children.empty()) return nullptr;
        return static_cast<FakeCompositionVisual*>(clip->children[0]);
    }
};

// Synthesize a press at a character offset's own X position, so the hit-test lands
// exactly where intended rather than at a guessed coordinate.
PointerEventArgs PressAt(float x, float y, int clickCount) {
    PointerEventArgs e;
    e.position = {x, y};
    e.button = PointerButton::Left;
    e.clickCount = clickCount;
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// Double / triple click through the control
// ---------------------------------------------------------------------------

TEST(TextEditInteraction, DoubleClickSelectsWordInTextArea) {
    AreaFixture f;
    if (!f.Ready()) return;
    f.Open(L"hello world again");
    // Click inside "world". Padding is 10 DIP; a mid-word X is well past it. The exact
    // pixel does not matter as long as it lands in the second word, and the assertion is
    // on the resulting RANGE, which is what the feature promises.
    auto e = PressAt(60.0f, 15.0f, 2);
    f.area.OnPointerPressed(e);

    UINT32 start = 0, len = 0;
    // Selection must be a whole word: non-empty, and bounded by spaces in the buffer.
    EXPECT_TRUE(f.area.Text().size() > 0);
    start = std::min(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    len = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest()) - start;
    EXPECT_TRUE(len > 0);
    const std::wstring sel = f.area.Text().substr(start, len);
    // Whatever word was hit, it must contain no space — that is the word-granularity
    // promise, and it fails loudly if clickCount never reached the selection logic
    // (a single-click selection would have length 0).
    EXPECT_TRUE(sel.find(L' ') == std::wstring::npos);
}

TEST(TextEditInteraction, SingleClickDoesNotSelect) {
    // The control side of the same plumbing: clickCount 1 must place a caret, not a
    // selection. Without this, a bug that treated every press as a double-click would
    // be invisible in the test above (which only asserts a word WAS selected).
    AreaFixture f;
    if (!f.Ready()) return;
    f.Open(L"hello world");
    auto e = PressAt(60.0f, 15.0f, 1);
    f.area.OnPointerPressed(e);
    EXPECT_EQ(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
}

TEST(TextEditInteraction, TripleClickSelectsWholeLogicalLine) {
    AreaFixture f;
    if (!f.Ready()) return;
    f.Open(L"first line\nsecond line\nthird line");
    // Second row: y past one line height.
    const float y = 10.0f + f.area.LineHeightDip() * 1.5f;
    auto e = PressAt(40.0f, y, 3);
    f.area.OnPointerPressed(e);

    const UINT32 lo = std::min(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    const std::wstring sel = f.area.Text().substr(lo, hi - lo);
    EXPECT_EQ(sel, std::wstring(L"second line"));
}

TEST(TextEditInteraction, TripleClickExcludesTheNewline) {
    // A selected trailing '\n' would extend the highlight past the end of the line and,
    // worse, a Cut would join two lines.
    AreaFixture f;
    if (!f.Ready()) return;
    f.Open(L"alpha\nbeta");
    auto e = PressAt(20.0f, 15.0f, 3);
    f.area.OnPointerPressed(e);
    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    EXPECT_EQ(hi, UINT32{5});   // 'alpha', not 'alpha\n'
}

TEST(TextEditInteraction, DoubleClickSelectsWordInTextBox) {
    // Same plumbing on the single-line control: the logic is shared, but the pointer
    // path is the subclass's, so both need covering.
    MockHost host;
    if (!host.Ready()) return;
    AnimationRegistry anims;
    InputManager input;
    TextBox box;
    box.SetText(L"foo bar");
    box.SetBounds({0, 0, 200, 32});
    box.AttachToContext(MakeCtx(host, anims, input));

    auto e = PressAt(40.0f, 16.0f, 2);
    box.OnPointerPressed(e);
    const UINT32 lo = std::min(box.SelectionStartForTest(), box.SelectionEndForTest());
    const UINT32 hi = std::max(box.SelectionStartForTest(), box.SelectionEndForTest());
    EXPECT_TRUE(hi > lo);
    EXPECT_TRUE(box.Text().substr(lo, hi - lo).find(L' ') == std::wstring::npos);
    box.DetachFromContext();
}

// ---------------------------------------------------------------------------
// Ctrl+A must reach the composited surface
// ---------------------------------------------------------------------------

TEST(TextEditInteraction, SelectAllRedrawsCompositedSurface) {
    // THE REGRESSION THIS FILE EXISTS FOR. Ctrl+A used to call only Invalidate(), which
    // repaints the window frame; the selection highlight lives on the composition content
    // surface, so nothing on screen changed. Asserting on caret_/selAnchor_ would pass
    // either way — the draw count is what distinguishes them.
    CompositedFixture f;
    if (!f.Ready()) return;
    f.Open(L"some text to select");
    auto* content = f.Content();
    EXPECT_TRUE(content != nullptr);
    if (!content) return;

    const int before = content->drawCount;
    f.area.SelectAllForTest();
    EXPECT_TRUE(content->drawCount > before);
}

TEST(TextEditInteraction, SelectAllCoversWholeBuffer) {
    AreaFixture f;
    if (!f.Ready()) return;
    const std::wstring text = L"line one\nline two";
    f.Open(text);
    f.area.SelectAllForTest();
    const UINT32 lo = std::min(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    EXPECT_EQ(lo, UINT32{0});
    EXPECT_EQ(hi, static_cast<UINT32>(text.size()));
}

// ---------------------------------------------------------------------------
// Drag granularity — the part that is easy to get wrong
// ---------------------------------------------------------------------------

TEST(TextEditInteraction, WordDragForwardKeepsFirstWordWhole) {
    // Double-click a word, then drag right. Both the original word and the one under the
    // pointer must be fully covered — a character-granularity drag would cut the first
    // word at the click point.
    AreaFixture f;
    if (!f.Ready()) return;
    f.Open(L"alpha beta gamma");

    auto press = PressAt(20.0f, 15.0f, 2);      // inside "alpha"
    f.area.OnPointerPressed(press);
    const UINT32 anchorAfterPress = f.area.SelectionStartForTest();

    auto move = PressAt(80.0f, 15.0f, 1);       // out into a later word
    f.area.OnPointerMoved(move);

    const UINT32 lo = std::min(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    // The anchor must still be the START of the first word, and the range must have grown.
    EXPECT_EQ(lo, anchorAfterPress);
    EXPECT_TRUE(hi > lo);
    const std::wstring sel = f.area.Text().substr(lo, hi - lo);
    // Word granularity: the selection cannot END mid-word, i.e. the character just past
    // it is a space or the buffer end.
    EXPECT_TRUE(hi == f.area.Text().size() || f.area.Text()[hi] == L' ');
    // And it must start at a word start.
    EXPECT_TRUE(lo == 0 || f.area.Text()[lo - 1] == L' ');
}

TEST(TextEditInteraction, WordDragBackwardKeepsFirstWordWhole) {
    // The reversal case, and the reason dragStartLo_/dragStartHi_ exist as a PAIR rather
    // than a single anchor index: dragging left must pin the original word's END, so that
    // word stays fully selected. With one anchor it collapses to a caret.
    AreaFixture f;
    if (!f.Ready()) return;
    const std::wstring text = L"alpha beta gamma";
    f.Open(text);

    auto press = PressAt(80.0f, 15.0f, 2);      // inside a later word
    f.area.OnPointerPressed(press);
    const UINT32 startHi = std::max(f.area.SelectionStartForTest(),
                                    f.area.SelectionEndForTest());

    auto move = PressAt(15.0f, 15.0f, 1);       // drag back to the first word
    f.area.OnPointerMoved(move);

    const UINT32 lo = std::min(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    // The far edge must still be the originally clicked word's end.
    EXPECT_EQ(hi, startHi);
    EXPECT_TRUE(lo < hi);
    // Word granularity at the near end too.
    EXPECT_TRUE(lo == 0 || text[lo - 1] == L' ');
}

TEST(TextEditInteraction, CharacterDragStaysCharacterGranular) {
    // A plain single-click drag must NOT snap to words — otherwise precise selection
    // becomes impossible. The mid-word end position is the observable.
    AreaFixture f;
    if (!f.Ready()) return;
    f.Open(L"abcdefghij klmnop");
    auto press = PressAt(12.0f, 15.0f, 1);
    f.area.OnPointerPressed(press);
    auto move = PressAt(45.0f, 15.0f, 1);
    f.area.OnPointerMoved(move);

    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    // Landing strictly inside the first word proves no word snapping happened. (The word
    // spans [0,10); a word-granular drag would have produced exactly 0 or 10.)
    EXPECT_TRUE(hi > 0 && hi < 10);
}

TEST(TextEditInteraction, LineDragExtendsByWholeLines) {
    AreaFixture f;
    if (!f.Ready()) return;
    const std::wstring text = L"first\nsecond\nthird";
    f.Open(text);

    auto press = PressAt(20.0f, 15.0f, 3);      // triple-click line 0
    f.area.OnPointerPressed(press);
    auto move = PressAt(20.0f, 10.0f + f.area.LineHeightDip() * 1.5f, 1);  // drag to line 1
    f.area.OnPointerMoved(move);

    const UINT32 lo = std::min(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    const UINT32 hi = std::max(f.area.SelectionStartForTest(), f.area.SelectionEndForTest());
    EXPECT_EQ(lo, UINT32{0});
    // Through the end of "second" — a line-granular extension, not a partial one.
    EXPECT_EQ(hi, UINT32{12});
}

// ---------------------------------------------------------------------------
// The click count must survive the routing layer
// ---------------------------------------------------------------------------

TEST(TextEditInteraction, InputManagerPropagatesClickCount) {
    // Every other test in this file calls OnPointerPressed directly, which BYPASSES
    // InputManager — so none of them can see whether the count survives routing. A
    // mutation that hard-coded args.clickCount = 1 in InputManager::PointerPressed left
    // all of them green. This one goes through the real entry point.
    MockHost host;
    if (!host.Ready()) return;
    AnimationRegistry anims;
    InputManager input;
    TextArea area;
    area.SetWrapMode(TextWrapMode::NoWrap);
    area.SetText(L"hello world");
    area.SetBounds({0, 0, 400, 200});
    area.AttachToContext(MakeCtx(host, anims, input));

    // The manager hit-tests against a root list, so give it one containing the control.
    std::vector<UIElement*> roots{&area};
    input.SetRoots(&roots);

    input.PointerPressed(Point{60.0f, 15.0f}, PointerButton::Left, ModifierKeys::None,
                         /*clickCount=*/2);
    // A double-click must have produced a non-empty selection. With the count dropped to
    // 1 on the way through, this is a bare caret placement and the range is empty.
    EXPECT_TRUE(area.SelectionStartForTest() != area.SelectionEndForTest());
    area.DetachFromContext();
}

TEST(TextEditInteraction, DoubleClickRedrawsCompositedSurface) {
    // Same requirement for word select: it also sets the fields directly.
    CompositedFixture f;
    if (!f.Ready()) return;
    f.Open(L"hello world");
    auto* content = f.Content();
    if (!content) return;
    const int before = content->drawCount;
    auto e = PressAt(60.0f, 15.0f, 2);
    f.area.OnPointerPressed(e);
    EXPECT_TRUE(content->drawCount > before);
}
