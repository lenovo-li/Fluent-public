// InputManagerCaptureTests.cpp — pointer capture semantics (WP-03, roadmap §9.3).
// Pure logic: capture routes move/release only to the captor; up releases;
// window-deactivate and element-detach release with a canceling release; the base
// click gesture presses/captures/clicks-inside/cancels-outside.

#include "../framework/Test.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIElement.h"

#include <vector>

using namespace fluent;

namespace {

// A clickable leaf that counts clicks and observes moves/releases + press state
// via the base gesture (SetClickable), plus wires the InputManager through a
// minimal context so CapturePointer/ReleaseCapture work.
struct ClickEl : UIElement {
    int clicks = 0;
    int moves = 0;
    int releases = 0;
    ClickEl() { SetClickable(true); SetFocusable(true); }
    void Render(const DrawingContext&) override {}
    void Measure(float, float) override { desired_ = {20, 20}; }
    void Arrange(const RectDip& r) override { SetBounds(r); }
    void OnClickRouted(PointerEventArgs&) override { ++clicks; }
    void OnPointerMoved(PointerEventArgs&) override { ++moves; }
    void OnPointerReleased(PointerEventArgs& e) override {
        ++releases;
        UIElement::OnPointerReleased(e);  // keep the base gesture (click + release-capture)
    }
    bool IsPressedState() const { return State() == VisualState::Pressed; }
};

// Give an element a context whose input pointer is `im`, so its base gesture can
// call CapturePointer/ReleaseCapture. Uses a real attach so Context() is live.
void WireInput(UIElement& e, InputManager* im) {
    UIContext ctx;
    ctx.input = im;
    e.AttachToContext(ctx);
}

}  // namespace

// A press on a clickable element captures the pointer and enters Pressed state.
TEST(InputManagerCapture, PressCapturesAndPresses) {
    InputManager im;
    ClickEl el;
    el.SetBounds({0, 0, 20, 20});
    WireInput(el, &im);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerPressed(Point{5, 5}, PointerButton::Left, ModifierKeys::None);
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(&el));
    EXPECT_TRUE(el.IsPressedState());

    el.DetachFromContext();
}

// While captured, moves outside the element still route to the captor only.
TEST(InputManagerCapture, CapturedGetsMovesRegardlessOfPosition) {
    InputManager im;
    ClickEl el;
    el.SetBounds({0, 0, 20, 20});
    WireInput(el, &im);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerPressed(Point{5, 5}, PointerButton::Left, ModifierKeys::None);
    im.PointerMoved(Point{500, 500}, ModifierKeys::None);  // far outside
    EXPECT_EQ(el.moves, 1);  // still delivered to the captor

    el.DetachFromContext();
}

// Release inside fires a click and releases capture.
TEST(InputManagerCapture, ReleaseInsideClicksAndReleases) {
    InputManager im;
    ClickEl el;
    el.SetBounds({0, 0, 20, 20});
    WireInput(el, &im);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerPressed(Point{5, 5}, PointerButton::Left, ModifierKeys::None);
    im.PointerReleased(Point{6, 6}, PointerButton::Left, ModifierKeys::None);
    EXPECT_EQ(el.clicks, 1);
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(nullptr));
    EXPECT_FALSE(el.IsPressedState());

    el.DetachFromContext();
}

// Release outside releases capture but does NOT fire a click.
TEST(InputManagerCapture, ReleaseOutsideCancelsClick) {
    InputManager im;
    ClickEl el;
    el.SetBounds({0, 0, 20, 20});
    WireInput(el, &im);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerPressed(Point{5, 5}, PointerButton::Left, ModifierKeys::None);
    im.PointerReleased(Point{500, 500}, PointerButton::Left, ModifierKeys::None);
    EXPECT_EQ(el.clicks, 0);  // released off-bounds -> no click
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(nullptr));

    el.DetachFromContext();
}

// Window deactivation releases capture and delivers a canceling release.
TEST(InputManagerCapture, DeactivateReleasesCapture) {
    InputManager im;
    ClickEl el;
    el.SetBounds({0, 0, 20, 20});
    WireInput(el, &im);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);

    im.PointerPressed(Point{5, 5}, PointerButton::Left, ModifierKeys::None);
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(&el));
    im.OnWindowDeactivated();
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(nullptr));
    EXPECT_FALSE(el.IsPressedState());  // canceling release left pressed state
    EXPECT_EQ(el.clicks, 0);            // cancel is not a click

    el.DetachFromContext();
}

// Detaching the captured element clears capture (no dangling captor).
TEST(InputManagerCapture, DetachClearsCapture) {
    InputManager im;
    FocusManager fm;
    im.SetFocusManager(&fm);
    ClickEl el;
    el.SetBounds({0, 0, 20, 20});
    WireInput(el, &im);
    std::vector<UIElement*> roots{&el};
    im.SetRoots(&roots);
    fm.SetRoots(&roots);

    im.PointerPressed(Point{5, 5}, PointerButton::Left, ModifierKeys::None);
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(&el));

    im.OnElementDetached(&el);
    EXPECT_EQ(im.Captured(), static_cast<UIElement*>(nullptr));
    EXPECT_EQ(im.Hot(), static_cast<UIElement*>(nullptr));

    el.DetachFromContext();
}
