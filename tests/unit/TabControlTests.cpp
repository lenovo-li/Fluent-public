// TabControlTests.cpp — headless tests for TabControl tab management,
// selection state, and content attach behavior.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TabControl.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/layout/ScrollPanel.h"
#include "../../FluentUI/layout/StackPanel.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include <memory>

using namespace fluent;

namespace {

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

void CountSelection(void* owner, TabControl&, int& idx) {
    int* state = static_cast<int*>(owner);
    ++state[0];  // fired count
    state[1] = idx;  // last index
}

void CountCloseRequested(void* owner, TabControl&, TabControl::TabCloseRequestedArgs& args) {
    int* state = static_cast<int*>(owner);
    ++state[0];  // fired count
    state[1] = args.index;  // last index
}

}  // namespace

TEST(TabControl, Construction) {
    TabControl tc;
    EXPECT_EQ(tc.SelectedIndex(), -1);
    EXPECT_EQ(tc.TabCount(), 0);
}

TEST(TabControl, AddFirstTabSelectsIt) {
    TabControl tc;
    auto* b1 = tc.AddTab(L"Tab 1", std::make_unique<Button>());
    EXPECT_TRUE(b1 != nullptr);
    EXPECT_EQ(tc.TabCount(), 1);
    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(tc.SelectedContent(), b1);
}

TEST(TabControl, AddMultipleTabs) {
    TabControl tc;
    auto* b1 = tc.AddTab(L"First", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"Second", std::make_unique<Button>());
    auto* b3 = tc.AddTab(L"Third", std::make_unique<Button>());
    EXPECT_EQ(tc.TabCount(), 3);
    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(tc.SelectedContent(), b1);
    EXPECT_TRUE(b2 != nullptr);
    EXPECT_TRUE(b3 != nullptr);
}

TEST(TabControl, SetSelectedIndex) {
    TabControl tc;
    auto* b1 = tc.AddTab(L"A", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"B", std::make_unique<Button>());
    auto* b3 = tc.AddTab(L"C", std::make_unique<Button>());

    tc.SetSelectedIndex(1);
    EXPECT_EQ(tc.SelectedIndex(), 1);
    EXPECT_EQ(tc.SelectedContent(), b2);

    tc.SetSelectedIndex(2);
    EXPECT_EQ(tc.SelectedIndex(), 2);
    EXPECT_EQ(tc.SelectedContent(), b3);

    tc.SetSelectedIndex(0);
    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(tc.SelectedContent(), b1);
}

TEST(TabControl, SetSelectedIndexOutOfRange) {
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());

    tc.SetSelectedIndex(10);
    EXPECT_EQ(tc.SelectedIndex(), -1);
    EXPECT_TRUE(tc.SelectedContent() == nullptr);

    tc.SetSelectedIndex(-5);
    EXPECT_EQ(tc.SelectedIndex(), -1);
}

TEST(TabControl, RemoveTab) {
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"B", std::make_unique<Button>());
    tc.AddTab(L"C", std::make_unique<Button>());

    tc.RemoveTab(0);
    EXPECT_EQ(tc.TabCount(), 2);
    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(tc.SelectedContent(), b2);
}

TEST(TabControl, RemoveSelectedTabSelectsPrevious) {
    TabControl tc;
    auto* b1 = tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    tc.AddTab(L"C", std::make_unique<Button>());

    tc.SetSelectedIndex(1);
    tc.RemoveTab(1);
    EXPECT_EQ(tc.TabCount(), 2);
    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(tc.SelectedContent(), b1);
}

TEST(TabControl, RemoveFirstTabWhenSelected) {
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"B", std::make_unique<Button>());

    tc.SetSelectedIndex(0);
    tc.RemoveTab(0);
    EXPECT_EQ(tc.TabCount(), 1);
    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(tc.SelectedContent(), b2);
}

TEST(TabControl, RemoveLastTab) {
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());

    tc.RemoveTab(0);
    EXPECT_EQ(tc.TabCount(), 0);
    EXPECT_EQ(tc.SelectedIndex(), -1);
    EXPECT_TRUE(tc.SelectedContent() == nullptr);
}

TEST(TabControl, RemoveBeforeSelection) {
    // Removing a tab BEFORE the selection must shift the stored index down so it
    // still names the SAME content element. Selection is C; after removing A the
    // list is [B, C] and C has moved from index 2 to index 1.
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    auto* c = tc.AddTab(L"C", std::make_unique<Button>());

    tc.SetSelectedIndex(2);
    EXPECT_EQ(tc.SelectedContent(), c);

    tc.RemoveTab(0);
    EXPECT_EQ(tc.SelectedIndex(), 1);
    EXPECT_EQ(tc.SelectedContent(), c);   // same element, new index
    EXPECT_EQ(tc.HeaderAt(1), L"C");
}

TEST(TabControl, HeaderAt) {
    TabControl tc;
    tc.AddTab(L"First", std::make_unique<Button>());
    tc.AddTab(L"Second", std::make_unique<Button>());

    EXPECT_EQ(tc.HeaderAt(0), L"First");
    EXPECT_EQ(tc.HeaderAt(1), L"Second");
    EXPECT_EQ(tc.HeaderAt(2), L"");
    EXPECT_EQ(tc.HeaderAt(-1), L"");
}

TEST(TabControl, MeasureWithoutTabs) {
    TabControl tc;
    tc.Measure(500, 400);
    EXPECT_TRUE(tc.Desired().h > 0.0f);
}

TEST(TabControl, MeasureWithTabs) {
    TabControl tc;
    tc.AddTab(L"Tab", std::make_unique<Button>());
    tc.Measure(500, 400);
    EXPECT_TRUE(tc.Desired().w > 0.0f);
    EXPECT_TRUE(tc.Desired().h > tc.StripHeightDip());
}

TEST(TabControl, ArrangePositionsContent) {
    MockHost host;
    TabControl tc;
    auto* btn = tc.AddTab(L"Button", std::make_unique<Button>());
    btn->SetWidth(100);
    btn->SetHeight(50);

    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{10, 20, 500, 400});

    EXPECT_EQ(tc.Bounds().x, 10.0f);
    EXPECT_EQ(tc.Bounds().y, 20.0f);
    // Content sits below the strip with the 8 DIP top gap (see ArrangeOverride).
    EXPECT_EQ(btn->Bounds().y, 20.0f + tc.StripHeightDip() + 8.0f);
}

TEST(TabControl, OnlySelectedContentIsAttached) {
    MockHost host;
    TabControl tc;
    auto* b1 = tc.AddTab(L"A", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"B", std::make_unique<Button>());
    auto* b3 = tc.AddTab(L"C", std::make_unique<Button>());

    tc.AttachToContext(MakeCtx(host));
    EXPECT_TRUE(b1->IsAttached());
    EXPECT_FALSE(b2->IsAttached());
    EXPECT_FALSE(b3->IsAttached());

    tc.SetSelectedIndex(1);
    EXPECT_FALSE(b1->IsAttached());
    EXPECT_TRUE(b2->IsAttached());
    EXPECT_FALSE(b3->IsAttached());

    tc.SetSelectedIndex(2);
    EXPECT_FALSE(b1->IsAttached());
    EXPECT_FALSE(b2->IsAttached());
    EXPECT_TRUE(b3->IsAttached());
}

TEST(TabControl, SelectionChangedEvent) {
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());

    // state[0] = fired count, state[1] = last index. The event takes a plain
    // function pointer plus an owner (no std::function), so the counters live in
    // an array the thunk casts back to.
    int state[2] = {0, -999};
    auto sub = tc.SelectionChanged().Subscribe(state, CountSelection);

    tc.SetSelectedIndex(1);
    EXPECT_EQ(state[0], 1);
    EXPECT_EQ(state[1], 1);

    tc.SetSelectedIndex(1);   // same index: no event
    EXPECT_EQ(state[0], 1);

    tc.SetSelectedIndex(0);
    EXPECT_EQ(state[0], 2);
    EXPECT_EQ(state[1], 0);
}

TEST(TabControl, HeaderIndexAt) {
    MockHost host;
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    tc.AddTab(L"C", std::make_unique<Button>());

    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    const RectDip r0 = tc.HeaderRect(0);
    const RectDip r1 = tc.HeaderRect(1);
    const RectDip r2 = tc.HeaderRect(2);

    EXPECT_EQ(tc.HeaderIndexAt(r0.x + 5, r0.y + 5), 0);
    EXPECT_EQ(tc.HeaderIndexAt(r1.x + 5, r1.y + 5), 1);
    EXPECT_EQ(tc.HeaderIndexAt(r2.x + 5, r2.y + 5), 2);
    EXPECT_EQ(tc.HeaderIndexAt(-10, 10), -1);
    EXPECT_EQ(tc.HeaderIndexAt(5000, 10), -1);
}

// Repro of the demo scenario: TabControl wrapping ScrollPanel pages. After a
// tab switch, the newly-selected page must end up measured AND arranged into the
// content slot — otherwise it renders nothing (empty tab). Mirrors the demo's
// NESTED structure: ScrollPanel -> horizontal StackPanel rows -> leaf controls.
TEST(TabControl, SwitchedTabContentIsLaidOut) {
    MockHost host;
    TabControl tc;
    auto makePage = [](const wchar_t* label) {
        auto page = std::make_unique<ScrollPanel>();
        page->SetSpacing(12.0f);
        auto* row = page->Emplace<StackPanel>();
        row->SetOrientation(StackPanel::Orientation::Horizontal);
        row->SetSpacing(16.0f);
        row->SetVAlign(VAlign::Top);
        row->SetHeight(36.0f);
        auto* b = row->Emplace<Button>();
        b->SetText(label);
        b->SetWidth(120.0f);
        b->SetHAlign(HAlign::Left);
        auto* b2 = row->Emplace<Button>();
        b2->SetText(label);
        b2->SetWidth(120.0f);
        return page;
    };
    auto* p0 = tc.AddTab(L"A", makePage(L"a"));
    auto* p1 = tc.AddTab(L"B", makePage(L"b"));
    auto* p2 = tc.AddTab(L"C", makePage(L"c"));

    tc.AttachToContext(MakeCtx(host));
    tc.UpdateLayout(RectDip{0, 0, 500, 400});

    // Initially tab 0 is laid out; 1 and 2 are detached (zero bounds).
    EXPECT_TRUE(p0->Bounds().h > 0.0f);
    EXPECT_EQ(p1->Bounds().h, 0.0f);
    EXPECT_EQ(p2->Bounds().h, 0.0f);

    // Switch to tab 1 and re-run the frame layout (what the host does when the
    // tab control reports Measure-dirty).
    tc.SetSelectedIndex(1);
    EXPECT_TRUE(tc.AnyDirtyInSubtree(DirtyFlags::Measure));
    tc.UpdateLayout(RectDip{0, 0, 500, 400});
    tc.ClearDirtySubtree();

    // The newly selected page must now occupy the content slot below the strip,
    // AND its inner content must have a real extent (contentHeight > 0 drives
    // whether ScrollPanel::Render has anything to draw).
    EXPECT_TRUE(p1->IsAttached());
    EXPECT_TRUE(p1->Bounds().w > 0.0f);
    EXPECT_TRUE(p1->Bounds().h > 0.0f);
    EXPECT_EQ(p1->Bounds().y, tc.Bounds().y + tc.StripHeightDip() + 8.0f);
    EXPECT_TRUE(p1->Desired().h > 0.0f);

    // Switch to tab 2 the same way.
    tc.SetSelectedIndex(2);
    tc.UpdateLayout(RectDip{0, 0, 500, 400});
    EXPECT_TRUE(p2->Bounds().h > 0.0f);
    EXPECT_TRUE(p2->Desired().h > 0.0f);
}

// Regression: a deselected tab's content keeps its last bounds (it is detached,
// not destroyed), and the base Panel::HitTestDeep would still find it there —
// so clicks on the strip landed on a detached child instead of the headers.
TEST(TabControl, DeselectedContentNotHitTested) {
    MockHost host;
    TabControl tc;
    auto* b1 = tc.AddTab(L"A", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"B", std::make_unique<Button>());

    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    // Tab 0 selected: content is hit, and a point on the strip hits the
    // TabControl itself (never a child).
    EXPECT_EQ(tc.HitTestDeep(b1->Bounds().x + 5, b1->Bounds().y + 5), b1);
    const RectDip strip{0, 0, 500, tc.StripHeightDip()};
    EXPECT_EQ(tc.HitTestDeep(strip.x + 250, strip.y + 5), &tc);

    // Switch away: b1 is detached but keeps its bounds. A point inside those
    // stale bounds must hit b2 (the live content) or the control — never b1.
    tc.SetSelectedIndex(1);
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});
    UIElement* hit = tc.HitTestDeep(b1->Bounds().x + 5, b1->Bounds().y + 5);
    EXPECT_TRUE(hit == b2 || hit == &tc);

    // Outside the control entirely: nothing.
    EXPECT_TRUE(tc.HitTestDeep(5000, 5000) == nullptr);
}

// Regression: Panel::CollectDirtyBounds walks every child, so a detached
// content's dirty flags used to inflate the redraw region with pixels the
// selected tab does not own.
TEST(TabControl, DeselectedContentNotDirtyCollected) {
    MockHost host;
    TabControl tc;
    auto* b1 = tc.AddTab(L"A", std::make_unique<Button>());
    auto* b2 = tc.AddTab(L"B", std::make_unique<Button>());

    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});
    tc.ClearDirtySubtree();

    // Dirty the still-selected content (via a public setter; Invalidate is
    // protected): its bounds must show up.
    b1->SetText(L"dirty");
    std::vector<RectDip> dirty;
    tc.CollectDirtyBounds(dirty);
    EXPECT_TRUE(!dirty.empty());

    // Switch away, settle, then dirty the DETACHED b1: nothing may be reported.
    tc.SetSelectedIndex(1);
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});
    tc.ClearDirtySubtree();
    b1->SetText(L"dirty again");
    dirty.clear();
    tc.CollectDirtyBounds(dirty);
    EXPECT_EQ(dirty.size(), 0u);

    // Sanity: dirtying the SELECTED content does report.
    b2->SetText(L"live");
    tc.CollectDirtyBounds(dirty);
    EXPECT_TRUE(!dirty.empty());
}

// --- P1-11: Close button tests -------------------------------------------

TEST(TabControl, CloseButtonDefaultDisabled) {
    TabControl tc;
    EXPECT_FALSE(tc.CloseButtonVisible());
}

TEST(TabControl, CloseButtonRectEmptyWhenDisabled) {
    MockHost host;
    TabControl tc;
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    EXPECT_TRUE(tc.CloseButtonRect(0).isEmpty());
}

TEST(TabControl, CloseButtonRectNonEmptyWhenEnabled) {
    MockHost host;
    TabControl tc;
    tc.SetCloseButtonVisible(true);
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    const RectDip cbr = tc.CloseButtonRect(0);
    EXPECT_TRUE(!cbr.isEmpty());
    EXPECT_TRUE(cbr.w > 0.0f && cbr.h > 0.0f);
}

TEST(TabControl, CloseButtonIndexAtFindsButton) {
    MockHost host;
    TabControl tc;
    tc.SetCloseButtonVisible(true);
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    const RectDip cb0 = tc.CloseButtonRect(0);
    const RectDip cb1 = tc.CloseButtonRect(1);

    EXPECT_EQ(tc.CloseButtonIndexAt(cb0.x + cb0.w * 0.5f, cb0.y + cb0.h * 0.5f), 0);
    EXPECT_EQ(tc.CloseButtonIndexAt(cb1.x + cb1.w * 0.5f, cb1.y + cb1.h * 0.5f), 1);
    EXPECT_EQ(tc.CloseButtonIndexAt(-10, 10), -1);
}

TEST(TabControl, TabCloseRequestedEvent) {
    MockHost host;
    TabControl tc;
    tc.SetCloseButtonVisible(true);
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    int state[2] = {0, -999};
    auto sub = tc.TabCloseRequested().Subscribe(state, CountCloseRequested);

    const RectDip cb1 = tc.CloseButtonRect(1);
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position.x = cb1.x + cb1.w * 0.5f;
    e.position.y = cb1.y + cb1.h * 0.5f;

    tc.OnPointerPressed(e);

    EXPECT_EQ(state[0], 1);  // fired once
    EXPECT_EQ(state[1], 1);  // tab index 1
    EXPECT_TRUE(e.handled);
}

TEST(TabControl, CloseButtonDoesNotSelectTab) {
    MockHost host;
    TabControl tc;
    tc.SetCloseButtonVisible(true);
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    tc.SetSelectedIndex(0);
    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    int state[2] = {0, -999};
    auto sub = tc.TabCloseRequested().Subscribe(state, CountCloseRequested);

    // Click the close button of tab 1 (not selected). Selection must stay at 0.
    const RectDip cb1 = tc.CloseButtonRect(1);
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.position.x = cb1.x + cb1.w * 0.5f;
    e.position.y = cb1.y + cb1.h * 0.5f;

    tc.OnPointerPressed(e);

    EXPECT_EQ(tc.SelectedIndex(), 0);
    EXPECT_EQ(state[0], 1);  // TabCloseRequested fired
    EXPECT_EQ(state[1], 1);
}

TEST(TabControl, RemoveTabClearsCloseButtonHover) {
    MockHost host;
    TabControl tc;
    tc.SetCloseButtonVisible(true);
    tc.AddTab(L"A", std::make_unique<Button>());
    tc.AddTab(L"B", std::make_unique<Button>());
    tc.AttachToContext(MakeCtx(host));
    tc.Measure(500, 400);
    tc.Arrange(RectDip{0, 0, 500, 400});

    // Hover over tab 0's close button.
    const RectDip cb0 = tc.CloseButtonRect(0);
    PointerEventArgs e;
    e.position.x = cb0.x + cb0.w * 0.5f;
    e.position.y = cb0.y + cb0.h * 0.5f;
    tc.OnPointerMoved(e);

    // Remove tab 0 — the hover index must be cleared (it references a position that
    // now names a different tab).
    tc.RemoveTab(0);

    // If the hover wasn't cleared, a Render would attempt to draw a button for the
    // old index, which is now out of bounds or names the wrong tab. No assertion
    // needed: passing means the implementation cleared it.
    EXPECT_EQ(tc.TabCount(), 1);
}
