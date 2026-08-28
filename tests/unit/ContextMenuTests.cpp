// ContextMenuTests.cpp — per-element context-menu resolution (WP-03 follow-up).
// Pure logic: SetContextMenu ownership, ContextMenuOwnerAt walks the parent chain
// to the first element that owns a menu, owner-context fed on attach.

#include "../framework/Test.h"
#include "../../FluentUI/input/InputManager.h"
#include "../../FluentUI/core/UIElement.h"
#include "../../FluentUI/core/IContextMenu.h"
#include "../../FluentUI/core/UIContext.h"

#include <memory>
#include <vector>

using namespace fluent;

namespace {

// A fake menu that records whether it was fed a context and shown.
struct FakeMenu : IContextMenu {
    int contextFeeds = 0;
    int shows = 0;
    const void* lastWindow = nullptr;
    void SetOwnerContext(const UIContext& ctx) override {
        ++contextFeeds;
        lastWindow = ctx.window;
    }
    void ShowAt(int, int) override { ++shows; }
};

// A container with children that hit-tests topmost-first and self-hits in bounds.
struct Node : UIElement {
    std::vector<UIElement*> kids;
    void Add(UIElement* c) { c->SetParent(this); kids.push_back(c); }
    void Render(const DrawingContext&) override {}
    void Measure(float, float) override {}
    void Arrange(const RectDip& r) override { SetBounds(r); }
    UIElement* HitTestDeep(float x, float y) override {
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            if (UIElement* h = (*it)->HitTestDeep(x, y)) return h;
        return HitTest(x, y) ? this : nullptr;
    }
};

}  // namespace

// SetContextMenu stores the menu; HasContextMenu / ContextMenu reflect it.
TEST(ContextMenu, SetAndQuery) {
    Node n;
    EXPECT_FALSE(n.HasContextMenu());
    auto* raw = new FakeMenu();
    n.SetContextMenu(std::unique_ptr<IContextMenu>(raw));
    EXPECT_TRUE(n.HasContextMenu());
    EXPECT_EQ(n.ContextMenu(), static_cast<IContextMenu*>(raw));
}

// Attaching an element feeds its menu the owner context (no manual wiring).
TEST(ContextMenu, OwnerContextFedOnAttach) {
    Node n;
    auto* raw = new FakeMenu();
    n.SetContextMenu(std::unique_ptr<IContextMenu>(raw));
    EXPECT_EQ(raw->contextFeeds, 0);  // not attached yet

    UIContext ctx;
    int windowMarker = 0;
    ctx.window = reinterpret_cast<WindowServices*>(&windowMarker);
    n.AttachToContext(ctx);
    EXPECT_EQ(raw->contextFeeds, 1);
    EXPECT_EQ(raw->lastWindow, static_cast<const void*>(&windowMarker));

    n.DetachFromContext();
}

// SetContextMenu on an already-attached element feeds context immediately.
TEST(ContextMenu, SetWhileAttachedFeedsImmediately) {
    Node n;
    UIContext ctx;
    int windowMarker = 0;
    ctx.window = reinterpret_cast<WindowServices*>(&windowMarker);
    n.AttachToContext(ctx);

    auto* raw = new FakeMenu();
    n.SetContextMenu(std::unique_ptr<IContextMenu>(raw));
    EXPECT_EQ(raw->contextFeeds, 1);

    n.DetachFromContext();
}

// ContextMenuOwnerAt returns the element under the point that owns a menu.
TEST(ContextMenu, OwnerAtDirectHit) {
    Node root;
    Node child;
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});
    child.SetContextMenu(std::make_unique<FakeMenu>());

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    EXPECT_EQ(im.ContextMenuOwnerAt(Point{15, 15}), static_cast<UIElement*>(&child));
}

// A child without a menu inherits the first ancestor that has one.
TEST(ContextMenu, OwnerAtBubblesToParent) {
    Node root;
    Node child;
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});
    root.SetContextMenu(std::make_unique<FakeMenu>());  // only the parent has one

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    // Hit lands on the child (no menu) -> walks up to root.
    EXPECT_EQ(im.ContextMenuOwnerAt(Point{15, 15}), static_cast<UIElement*>(&root));
}

// Nothing under the point (or up its chain) has a menu -> null (window falls back).
TEST(ContextMenu, OwnerAtNoneReturnsNull) {
    Node root;
    root.SetBounds({0, 0, 100, 100});

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    EXPECT_EQ(im.ContextMenuOwnerAt(Point{50, 50}), static_cast<UIElement*>(nullptr));
    // A point outside all roots is also null.
    EXPECT_EQ(im.ContextMenuOwnerAt(Point{500, 500}), static_cast<UIElement*>(nullptr));
}

// --- P1-21: MenuFlyout reuse via SetContextMenuRef (non-owning reference) ---

// SetContextMenuRef sets a non-owning reference; multiple elements can share one menu.
TEST(ContextMenu, SetContextMenuRef_SharedMenu) {
    Node a, b, c;
    FakeMenu sharedMenu;

    a.SetContextMenuRef(&sharedMenu);
    b.SetContextMenuRef(&sharedMenu);
    c.SetContextMenuRef(&sharedMenu);

    EXPECT_TRUE(a.HasContextMenu());
    EXPECT_TRUE(b.HasContextMenu());
    EXPECT_TRUE(c.HasContextMenu());
    EXPECT_EQ(a.ContextMenu(), &sharedMenu);
    EXPECT_EQ(b.ContextMenu(), &sharedMenu);
    EXPECT_EQ(c.ContextMenu(), &sharedMenu);
}

// SetContextMenuRef feeds the menu context on attach, same as SetContextMenu.
TEST(ContextMenu, SetContextMenuRef_FeedsContextOnAttach) {
    Node n;
    FakeMenu sharedMenu;
    n.SetContextMenuRef(&sharedMenu);
    EXPECT_EQ(sharedMenu.contextFeeds, 0);

    UIContext ctx;
    int windowMarker = 0;
    ctx.window = reinterpret_cast<WindowServices*>(&windowMarker);
    n.AttachToContext(ctx);
    EXPECT_EQ(sharedMenu.contextFeeds, 1);
    EXPECT_EQ(sharedMenu.lastWindow, static_cast<const void*>(&windowMarker));

    n.DetachFromContext();
}

// SetContextMenuRef while already attached feeds context immediately.
TEST(ContextMenu, SetContextMenuRef_WhileAttachedFeedsImmediately) {
    Node n;
    UIContext ctx;
    int windowMarker = 0;
    ctx.window = reinterpret_cast<WindowServices*>(&windowMarker);
    n.AttachToContext(ctx);

    FakeMenu sharedMenu;
    n.SetContextMenuRef(&sharedMenu);
    EXPECT_EQ(sharedMenu.contextFeeds, 1);

    n.DetachFromContext();
}

// SetContextMenu clears any ref; SetContextMenuRef clears ownership (mutually exclusive).
TEST(ContextMenu, TwoModesAreMutuallyExclusive) {
    Node n;
    FakeMenu refMenu;
    n.SetContextMenuRef(&refMenu);
    EXPECT_EQ(n.ContextMenu(), &refMenu);

    // SetContextMenu replaces the ref with ownership.
    auto* ownedMenu = new FakeMenu();
    n.SetContextMenu(std::unique_ptr<IContextMenu>(ownedMenu));
    EXPECT_EQ(n.ContextMenu(), ownedMenu);

    // SetContextMenuRef clears ownership and installs a ref.
    n.SetContextMenuRef(&refMenu);
    EXPECT_EQ(n.ContextMenu(), &refMenu);
}

// Shared menu scenario: 3 elements attach, all feed context to the same instance.
// Last-attached wins (which is correct — ShowAt re-feeds at open time anyway).
TEST(ContextMenu, SharedMenuMultipleAttach) {
    Node a, b, c;
    FakeMenu sharedMenu;
    a.SetContextMenuRef(&sharedMenu);
    b.SetContextMenuRef(&sharedMenu);
    c.SetContextMenuRef(&sharedMenu);

    UIContext ctxA, ctxB, ctxC;
    int markerA = 1, markerB = 2, markerC = 3;
    ctxA.window = reinterpret_cast<WindowServices*>(&markerA);
    ctxB.window = reinterpret_cast<WindowServices*>(&markerB);
    ctxC.window = reinterpret_cast<WindowServices*>(&markerC);

    a.AttachToContext(ctxA);
    EXPECT_EQ(sharedMenu.contextFeeds, 1);
    EXPECT_EQ(sharedMenu.lastWindow, &markerA);

    b.AttachToContext(ctxB);
    EXPECT_EQ(sharedMenu.contextFeeds, 2);
    EXPECT_EQ(sharedMenu.lastWindow, &markerB);

    c.AttachToContext(ctxC);
    EXPECT_EQ(sharedMenu.contextFeeds, 3);
    EXPECT_EQ(sharedMenu.lastWindow, &markerC);

    a.DetachFromContext();
    b.DetachFromContext();
    c.DetachFromContext();
}

// ContextMenuOwnerAt resolves correctly when the element uses a shared ref.
TEST(ContextMenu, OwnerAtWorksWithSharedRef) {
    Node root;
    Node child;
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});

    FakeMenu sharedMenu;
    child.SetContextMenuRef(&sharedMenu);  // child has a shared ref

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    EXPECT_EQ(im.ContextMenuOwnerAt(Point{15, 15}), static_cast<UIElement*>(&child));
}

// Hybrid scenario: parent owns a menu, child has a shared ref — child's ref wins.
TEST(ContextMenu, OwnerAtPrefersChildRefOverParentOwned) {
    Node root;
    Node child;
    root.Add(&child);
    root.SetBounds({0, 0, 100, 100});
    child.SetBounds({10, 10, 20, 20});

    root.SetContextMenu(std::make_unique<FakeMenu>());  // parent owns
    FakeMenu sharedMenu;
    child.SetContextMenuRef(&sharedMenu);  // child has ref

    std::vector<UIElement*> roots{&root};
    InputManager im;
    im.SetRoots(&roots);
    // Hit on child -> child's ref, not parent's owned menu.
    EXPECT_EQ(im.ContextMenuOwnerAt(Point{15, 15}), static_cast<UIElement*>(&child));
}

