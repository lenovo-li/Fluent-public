// RoutedEventTests.cpp — pointer/key routing through the element chain (WP-03).
// Pure logic: deepest hit, bubble target->root, tunnel root->target, handled
// short-circuit, child click bubbling to a parent. Drives InputManager over a
// small hand-built tree (no HWND/GPU).

#include "../framework/Test.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/input/FocusManager.h"
#include "../../FluentUI/core/UIElement.h"

#include <string>
#include <vector>

using namespace fluent;

namespace {

// A UIElement that can both hold children and record the routing virtuals it
// sees, so a test can build an arbitrary chain by hand and observe order. It hit-
// tests topmost-first into children, else returns itself when the point is in
// bounds — the same rule Panel uses, but self-hittable so an inner node can be a
// route target.
struct Node : UIElement {
    std::string tag;
    std::vector<std::string>* log = nullptr;
    std::vector<UIElement*> kids;
    bool handlePressed = false;
    bool handleMoved = false;

    Node(const char* t, std::vector<std::string>* l) : tag(t), log(l) {}

    void Add(Node* c) { c->SetParent(this); kids.push_back(c); }
    void Render(const DrawingContext&) override {}
    void Measure(float, float) override { desired_ = {10, 10}; }
    void Arrange(const RectDip& r) override { SetBounds(r); }

    UIElement* HitTestDeep(float x, float y) override {
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            if (UIElement* h = (*it)->HitTestDeep(x, y)) return h;
        return HitTest(x, y) ? this : nullptr;
    }

    void OnPreviewPointerPressed(PointerEventArgs&) override {
        if (log) log->push_back(tag + ":preview");
    }
    void OnPointerPressed(PointerEventArgs& e) override {
        if (log) log->push_back(tag + ":pressed");
        if (handlePressed) e.handled = true;
    }
    void OnPointerMoved(PointerEventArgs& e) override {
        if (log) log->push_back(tag + ":moved");
        if (handleMoved) e.handled = true;
    }
};

}  // namespace

// The deepest element under the point is the hit target.
TEST(RoutedEvent, HitTestsDeepestElement) {
    std::vector<std::string> log;
    Node root("root", &log);
    Node child("child", &log);
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    EXPECT_EQ(im.HitTest(Point{15, 15}), static_cast<UIElement*>(&child));
    EXPECT_EQ(im.HitTest(Point{5, 5}), static_cast<UIElement*>(&root));
}

// Main phase bubbles target -> root.
TEST(RoutedEvent, BubblesTargetToRoot) {
    std::vector<std::string> log;
    Node root("root", &log);
    Node child("child", &log);
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    im.PointerMoved(Point{15, 15}, ModifierKeys::None);
    EXPECT_EQ(log.size(), size_t{2});
    EXPECT_EQ(log[0], std::string("child:moved"));
    EXPECT_EQ(log[1], std::string("root:moved"));
}

// Preview tunnels root -> target, before the target->root bubble.
TEST(RoutedEvent, PreviewTunnelsRootToTarget) {
    std::vector<std::string> log;
    Node root("root", &log);
    Node child("child", &log);
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    im.PointerPressed(Point{15, 15}, PointerButton::Left, ModifierKeys::None);
    // Tunnel root->child, then bubble child->root.
    EXPECT_EQ(log.size(), size_t{4});
    EXPECT_EQ(log[0], std::string("root:preview"));
    EXPECT_EQ(log[1], std::string("child:preview"));
    EXPECT_EQ(log[2], std::string("child:pressed"));
    EXPECT_EQ(log[3], std::string("root:pressed"));
}

// A handler setting handled stops the rest of the route (parent never sees it).
TEST(RoutedEvent, HandledShortCircuits) {
    std::vector<std::string> log;
    Node root("root", &log);
    Node mid("mid", &log);
    Node leaf("leaf", &log);
    root.Add(&mid);
    mid.Add(&leaf);
    mid.handlePressed = true;  // mid consumes during the bubble
    root.SetBounds({0, 0, 100, 100});
    mid.SetBounds({0, 0, 100, 100});
    leaf.SetBounds({10, 10, 20, 20});

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    im.PointerPressed(Point{15, 15}, PointerButton::Left, ModifierKeys::None);

    bool sawMidPressed = false, sawRootPressed = false;
    for (auto& s : log) {
        if (s == "mid:pressed") sawMidPressed = true;
        if (s == "root:pressed") sawRootPressed = true;
    }
    EXPECT_TRUE(sawMidPressed);
    EXPECT_FALSE(sawRootPressed);  // handled by mid, never bubbled to root
}

// A click on a child bubbles up so a parent can act on it (roadmap acceptance:
// "子内容点击能冒泡到父 Button"). Here the parent marks the bubbling press handled.
TEST(RoutedEvent, ChildPressBubblesToParent) {
    std::vector<std::string> log;
    Node parent("parent", &log);
    Node inner("inner", &log);
    parent.Add(&inner);
    parent.handlePressed = true;  // parent is the logical "button"
    parent.SetBounds({0, 0, 100, 100});
    inner.SetBounds({10, 10, 20, 20});

    std::vector<UIElement*> roots{&parent};
    InputManager im;
    im.SetRoots(&roots);
    im.PointerPressed(Point{15, 15}, PointerButton::Left, ModifierKeys::None);

    bool sawInner = false, sawParent = false;
    for (auto& s : log) {
        if (s == "inner:pressed") sawInner = true;
        if (s == "parent:pressed") sawParent = true;
    }
    EXPECT_TRUE(sawInner);
    EXPECT_TRUE(sawParent);
}
