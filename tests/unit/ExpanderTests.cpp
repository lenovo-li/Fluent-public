// ExpanderTests.cpp — headless tests for Expander's collapsed-means-detached
// invariant and header hit region.
//
// TWO DISTINCT RELATIONSHIPS, easy to conflate:
//
//   Parent()            — the tree-structure link. Set by SetContent and kept for
//                         the content's whole lifetime, regardless of expanded state.
//   IsContentAttached() — the live-context link. True only when the content holds a
//                         UIContext, which requires BOTH that the Expander itself is
//                         attached AND that it is expanded.
//
// A collapsed Expander's content therefore still has a Parent but no context: it is
// reachable by ownership, not by the tree walks that render/hit-test/animate. That is
// exactly the property that makes a large collapsed section cost nothing.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Expander.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include <memory>

using namespace fluent;

namespace {

// Minimal host so an Expander can be attached to something. Only DWrite is really
// used (header text measurement); everything else is a null stub.
class MockHost : public WindowServices {
public:
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host) {
    UIContext ctx;
    ctx.window = &host;
    ctx.dpiScale = 1.0f;
    return ctx;
}

}  // namespace

// --- Ownership vs attachment ----------------------------------------------

// SetContent establishes the Parent link immediately, in both states. This is the
// assertion that keeps the two relationships from being conflated: a reader who
// assumes "collapsed means no parent" would write code that leaks the content.
TEST(Expander, SetContentSetsParentRegardlessOfExpandedState) {
    Expander collapsed;
    auto a = std::make_unique<StackPanel>();
    auto* aPtr = a.get();
    collapsed.SetContent(std::move(a));
    EXPECT_FALSE(collapsed.IsExpanded());
    EXPECT_EQ(aPtr->Parent(), &collapsed);

    Expander expanded;
    expanded.SetExpanded(true);
    auto b = std::make_unique<StackPanel>();
    auto* bPtr = b.get();
    expanded.SetContent(std::move(b));
    EXPECT_TRUE(expanded.IsExpanded());
    EXPECT_EQ(bPtr->Parent(), &expanded);
}

// An Expander that is not itself attached cannot attach its content — there is no
// context to hand down. Expanding a detached Expander must therefore NOT attach.
TEST(Expander, DetachedExpanderNeverAttachesContent) {
    Expander exp;
    exp.SetContent(std::make_unique<StackPanel>());

    EXPECT_FALSE(exp.IsContentAttached());
    exp.SetExpanded(true);
    EXPECT_TRUE(exp.IsExpanded());
    EXPECT_FALSE(exp.IsContentAttached());  // still no context to attach to
}

// --- The core invariant: collapsed content holds no context ---------------

TEST(Expander, AttachedAndCollapsedLeavesContentDetached) {
    MockHost host;
    Expander exp;
    exp.SetContent(std::make_unique<StackPanel>());
    exp.AttachToContext(MakeCtx(host));

    EXPECT_TRUE(exp.IsAttached());
    EXPECT_FALSE(exp.IsExpanded());
    EXPECT_FALSE(exp.IsContentAttached());
}

TEST(Expander, AttachedAndExpandedAttachesContent) {
    MockHost host;
    Expander exp;
    exp.SetContent(std::make_unique<StackPanel>());
    exp.AttachToContext(MakeCtx(host));
    exp.SetExpanded(true);

    EXPECT_TRUE(exp.IsContentAttached());
}

// Expanding attaches, collapsing detaches, repeatedly. This is the behaviour a user
// clicking the header over and over exercises, and the one where a missing
// detach would silently leave every previously-opened section live.
TEST(Expander, ToggleAttachesAndDetachesContentRepeatedly) {
    MockHost host;
    Expander exp;
    exp.SetContent(std::make_unique<StackPanel>());
    exp.AttachToContext(MakeCtx(host));

    for (int round = 0; round < 3; ++round) {
        exp.SetExpanded(true);
        EXPECT_TRUE(exp.IsContentAttached());
        exp.SetExpanded(false);
        EXPECT_FALSE(exp.IsContentAttached());
    }
}

// Replacing the content while expanded must attach the NEW content, not leave it
// dormant until the next toggle.
TEST(Expander, SetContentWhileAttachedAndExpandedAttachesImmediately) {
    MockHost host;
    Expander exp;
    exp.AttachToContext(MakeCtx(host));
    exp.SetExpanded(true);

    exp.SetContent(std::make_unique<Button>());
    EXPECT_TRUE(exp.IsContentAttached());
}

// ...and replacing it while collapsed must leave it detached.
TEST(Expander, SetContentWhileAttachedAndCollapsedStaysDetached) {
    MockHost host;
    Expander exp;
    exp.AttachToContext(MakeCtx(host));

    exp.SetContent(std::make_unique<Button>());
    EXPECT_FALSE(exp.IsContentAttached());
}

// Detaching the Expander must take the content's context with it, or the content
// would outlive the tree it thinks it belongs to.
TEST(Expander, DetachingExpanderDetachesExpandedContent) {
    MockHost host;
    Expander exp;
    exp.SetContent(std::make_unique<StackPanel>());
    exp.AttachToContext(MakeCtx(host));
    exp.SetExpanded(true);
    EXPECT_TRUE(exp.IsContentAttached());

    exp.DetachFromContext();
    EXPECT_FALSE(exp.IsContentAttached());
}

// --- Header geometry -----------------------------------------------------
// HeaderRect() is public so a headless test can verify the click region without a
// device. HeaderHeight() is bodySize + 2*kHeaderPadV, so it is theme-derived rather
// than a literal — the assertions bound it instead of pinning an exact number, which
// would break on any typography change without indicating a real defect.

TEST(Expander, HeaderRectSpansFullWidthAtTopOfBounds) {
    Expander exp;
    exp.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});

    const RectDip header = exp.HeaderRect();
    EXPECT_EQ(header.x, 0.0f);
    EXPECT_EQ(header.y, 0.0f);
    EXPECT_EQ(header.w, 200.0f);
    EXPECT_TRUE(header.h > 20.0f);
    EXPECT_TRUE(header.h < 60.0f);
    // The header must not swallow the whole control, or there would be no content area.
    EXPECT_TRUE(header.h < 100.0f);
}

TEST(Expander, HeaderRectFollowsBoundsOrigin) {
    Expander exp;
    exp.Arrange(RectDip{10.0f, 20.0f, 300.0f, 150.0f});

    const RectDip header = exp.HeaderRect();
    EXPECT_EQ(header.x, 10.0f);
    EXPECT_EQ(header.y, 20.0f);
    EXPECT_EQ(header.w, 300.0f);
}

// The header height must not depend on the expanded state — the row is the same
// size open or closed, only the content below it changes.
TEST(Expander, HeaderHeightIsIndependentOfExpandedState) {
    Expander exp;
    exp.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});
    const float collapsedH = exp.HeaderRect().h;

    exp.SetExpanded(true);
    exp.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});
    const float expandedH = exp.HeaderRect().h;

    EXPECT_EQ(collapsedH, expandedH);
}
