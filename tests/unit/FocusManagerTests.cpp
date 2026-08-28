// FocusManagerTests.cpp — the single focus authority (WP-03). Pure logic:
// SetFocus single-truth (element bits follow), Tab order with wrap, detach clears
// focus, basic directional navigation, FocusChanged event payload.

#include "../framework/Test.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIElement.h"

#include <vector>

using namespace fluent;

namespace {

// A focusable leaf placed at a fixed rect (for directional tests).
struct FocEl : UIElement {
    FocEl() { SetFocusable(true); }
    void Render(const DrawingContext&) override {}
    void Measure(float, float) override { desired_ = {10, 10}; }
    void Arrange(const RectDip& r) override { SetBounds(r); }
};

}  // namespace

// SetFocus is the single truth: the element's IsFocused bit follows, and the old
// element is cleared.
TEST(FocusManager, SetFocusIsSingleTruth) {
    FocEl a, b;
    a.SetBounds({0, 0, 10, 10});
    b.SetBounds({0, 20, 10, 10});
    std::vector<UIElement*> roots{&a, &b};
    FocusManager fm;
    fm.SetRoots(&roots);

    fm.SetFocus(&a);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&a));
    EXPECT_TRUE(a.IsFocused());
    EXPECT_FALSE(b.IsFocused());

    fm.SetFocus(&b);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&b));
    EXPECT_FALSE(a.IsFocused());  // old focus cleared
    EXPECT_TRUE(b.IsFocused());
}

// A non-focusable element cannot take focus (SetFocus clears instead).
TEST(FocusManager, NonFocusableClearsFocus) {
    FocEl a;
    struct : UIElement {
        void Render(const DrawingContext&) override {}
        void Measure(float, float) override {}
        void Arrange(const RectDip& r) override { SetBounds(r); }
    } plain;  // focusable_ defaults false
    std::vector<UIElement*> roots{&a, &plain};
    FocusManager fm;
    fm.SetRoots(&roots);

    fm.SetFocus(&a);
    fm.SetFocus(&plain);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(nullptr));
    EXPECT_FALSE(a.IsFocused());
}

// Tab moves forward with wrap-around; Shift+Tab moves backward.
TEST(FocusManager, TabOrderWraps) {
    FocEl a, b, c;
    std::vector<UIElement*> roots{&a, &b, &c};
    FocusManager fm;
    fm.SetRoots(&roots);

    fm.MoveNext(false);  // nothing focused -> first
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&a));
    fm.MoveNext(false);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&b));
    fm.MoveNext(false);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&c));
    fm.MoveNext(false);  // wrap to first
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&a));
    fm.MoveNext(true);   // backward wraps to last
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&c));
}

// Detaching the focused element clears focus.
TEST(FocusManager, DetachClearsFocus) {
    FocEl a, b;
    std::vector<UIElement*> roots{&a, &b};
    FocusManager fm;
    fm.SetRoots(&roots);
    fm.SetFocus(&a);

    fm.OnElementDetached(&b);  // not focused -> no change
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&a));

    fm.OnElementDetached(&a);  // focused -> cleared
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(nullptr));
}

// FocusChanged carries the old and new focus.
TEST(FocusManager, FocusChangedEventPayload) {
    FocEl a, b;
    std::vector<UIElement*> roots{&a, &b};
    FocusManager fm;
    fm.SetRoots(&roots);

    struct Cap {
        UIElement* oldF = reinterpret_cast<UIElement*>(1);
        UIElement* newF = reinterpret_cast<UIElement*>(1);
        int count = 0;
    } cap;
    auto sub = fm.FocusChanged().Subscribe(&cap,
        [](void* owner, FocusManager&, FocusChangedArgs& e) {
            auto* c = static_cast<Cap*>(owner);
            c->oldF = e.oldFocus;
            c->newF = e.newFocus;
            ++c->count;
        });

    fm.SetFocus(&a);
    EXPECT_EQ(cap.count, 1);
    EXPECT_EQ(cap.oldF, static_cast<UIElement*>(nullptr));
    EXPECT_EQ(cap.newF, static_cast<UIElement*>(&a));

    fm.SetFocus(&b);
    EXPECT_EQ(cap.count, 2);
    EXPECT_EQ(cap.oldF, static_cast<UIElement*>(&a));
    EXPECT_EQ(cap.newF, static_cast<UIElement*>(&b));
}

// Basic directional navigation: nearest focusable in the requested direction.
TEST(FocusManager, DirectionalNavigation) {
    // Layout: a at top, b below a, c to the right of a.
    FocEl a, b, c;
    a.SetBounds({0, 0, 10, 10});
    b.SetBounds({0, 50, 10, 10});    // directly below a
    c.SetBounds({50, 0, 10, 10});    // directly right of a
    std::vector<UIElement*> roots{&a, &b, &c};
    FocusManager fm;
    fm.SetRoots(&roots);

    fm.SetFocus(&a);
    fm.MoveDirectional(FocusDirection::Down);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&b));

    fm.SetFocus(&a);
    fm.MoveDirectional(FocusDirection::Right);
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&c));

    fm.SetFocus(&a);
    fm.MoveDirectional(FocusDirection::Up);  // nothing above -> no change
    EXPECT_EQ(fm.Focused(), static_cast<UIElement*>(&a));
}
