// CanvasZIndexTests.cpp — headless tests for Canvas ZIndex layering.
//
// Paint order is observed, not assumed. An earlier version of this file asserted
// only that GetZIndex returned what SetZIndex stored and left comments saying
// "visual check needed" — which meant deleting the whole Canvas::Render override
// kept every test green. A RenderOrderProbe fixes that: it appends its own label
// to a shared vector when Render runs, so the sequence Canvas produced is a value
// the test can compare against. That is enough to pin ordering headless; only the
// resulting pixels need a real device.

#include "../framework/Test.h"
#include "../../FluentUI/layout/Canvas.h"
#include "../../FluentUI/graphics/DrawingContext.h"
#include "../../FluentUI/controls/Button.h"
#include "LayoutTestHelpers.h"
#include <string>
#include <vector>

using namespace fluent;

namespace {

// A leaf that records the order it was painted in. Bounds come from Canvas's
// arrange, so it needs a real size to survive the viewport cull in Canvas::Render.
class RenderOrderProbe : public FrameworkElement {
public:
    RenderOrderProbe(std::vector<std::wstring>* log, std::wstring label, float w, float h)
        : log_(log), label_(std::move(label)) {
        SetWidth(w);
        SetHeight(h);
    }
    void Render(const DrawingContext&) override {
        if (log_) log_->push_back(label_);
    }

private:
    std::vector<std::wstring>* log_;
    std::wstring label_;
};

// Canvas::Render culls children against its own bounds_, so the Canvas must be
// arranged to a real rect for anything to paint. A null dc/brush is fine — the
// probe never draws.
DrawingContext MakeDc() { return DrawingContext{nullptr, nullptr, 1.0f}; }

// Add a probe and hand back the borrowed pointer, so a test can set attached
// properties on it without a ChildAt accessor (Panel deliberately exposes only
// ChildCount — children are owned, not indexed).
RenderOrderProbe* AddProbe(Canvas& canvas, std::vector<std::wstring>* log,
                           std::wstring label, float w = 100.0f, float h = 100.0f) {
    return canvas.Add(std::make_unique<RenderOrderProbe>(log, std::move(label), w, h));
}

} // namespace

// --- Paint order ----------------------------------------------------------

TEST(Canvas, ZIndexDeterminesPaintOrder) {
    // Added in the order 2, 0, 1 — so insertion order and ZIndex order disagree,
    // and a Canvas that ignored ZIndex would paint 2/0/1 instead of 0/1/2.
    std::vector<std::wstring> painted;
    Canvas canvas;
    auto* z2 = AddProbe(canvas, &painted, L"z2");
    auto* z0 = AddProbe(canvas, &painted, L"z0");
    auto* z1 = AddProbe(canvas, &painted, L"z1");

    Canvas::SetZIndex(z2, 2);
    Canvas::SetZIndex(z0, 0);
    Canvas::SetZIndex(z1, 1);

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});
    DrawingContext dc = MakeDc();
    canvas.Render(dc);

    // Ascending ZIndex: the highest paints last, so it lands on top.
    EXPECT_EQ(painted.size(), size_t(3));
    EXPECT_TRUE(painted[0] == L"z0");
    EXPECT_TRUE(painted[1] == L"z1");
    EXPECT_TRUE(painted[2] == L"z2");
}

TEST(Canvas, SameZIndexPaintsInInsertionOrder) {
    // All default to ZIndex 0, so the sort must be STABLE — insertion order is the
    // tie-break. A plain sort could reorder equal elements and would flip this.
    std::vector<std::wstring> painted;
    Canvas canvas;
    AddProbe(canvas, &painted, L"first");
    AddProbe(canvas, &painted, L"second");
    AddProbe(canvas, &painted, L"third");

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});
    DrawingContext dc = MakeDc();
    canvas.Render(dc);

    EXPECT_EQ(painted.size(), size_t(3));
    EXPECT_TRUE(painted[0] == L"first");
    EXPECT_TRUE(painted[1] == L"second");
    EXPECT_TRUE(painted[2] == L"third");
}

TEST(Canvas, NegativeZIndexPaintsBelowZero) {
    // Negative ZIndex is meaningful: it puts a child under one that never set a
    // ZIndex at all. Added first-but-negative, so insertion order alone cannot
    // produce the expected result either way — the values have to be compared as
    // signed ints (an unsigned comparison would sort -1 as huge and paint it last).
    std::vector<std::wstring> painted;
    Canvas canvas;
    AddProbe(canvas, &painted, L"default");
    auto* below = AddProbe(canvas, &painted, L"below");

    Canvas::SetZIndex(below, -1);

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});
    DrawingContext dc = MakeDc();
    canvas.Render(dc);

    EXPECT_EQ(painted.size(), size_t(2));
    EXPECT_TRUE(painted[0] == L"below");    // z = -1 paints first
    EXPECT_TRUE(painted[1] == L"default");  // z = 0 paints on top
}

TEST(Canvas, ChangingZIndexReordersASubsequentPaint) {
    // The z-order is cached, so a ZIndex change after the first paint must
    // invalidate it. Without the generation counter the second Render would reuse
    // the stale order and this test would see the first order twice.
    std::vector<std::wstring> painted;
    Canvas canvas;
    auto* a = AddProbe(canvas, &painted, L"a");
    auto* b = AddProbe(canvas, &painted, L"b");

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});
    DrawingContext dc = MakeDc();

    canvas.Render(dc);
    EXPECT_EQ(painted.size(), size_t(2));
    EXPECT_TRUE(painted[0] == L"a");  // insertion order, both at z = 0

    painted.clear();
    Canvas::SetZIndex(a, 5);  // push "a" to the top
    canvas.Render(dc);

    EXPECT_EQ(painted.size(), size_t(2));
    EXPECT_TRUE(painted[0] == L"b");  // "a" now paints last
    EXPECT_TRUE(painted[1] == L"a");
}

TEST(Canvas, InvisibleChildIsNotPainted) {
    // Canvas::Render delegates the visibility check to RenderWithOpacity rather
    // than filtering the z-order itself. This pins that the delegation actually
    // happens — a hidden child must not paint, whatever its ZIndex.
    std::vector<std::wstring> painted;
    Canvas canvas;
    AddProbe(canvas, &painted, L"shown");
    auto* hidden = AddProbe(canvas, &painted, L"hidden");

    Canvas::SetZIndex(hidden, 10);  // topmost, and still must not paint
    hidden->SetVisible(false);

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});
    DrawingContext dc = MakeDc();
    canvas.Render(dc);

    EXPECT_EQ(painted.size(), size_t(1));
    EXPECT_TRUE(painted[0] == L"shown");
}

// --- Hit-test order -------------------------------------------------------

TEST(Canvas, HitTestRespectsZIndexDescending) {
    // Two children fully overlapping at (0,0). The point hits both, so the winner
    // is decided purely by ZIndex: the topmost must be returned. Added in the order
    // that makes insertion order give the WRONG answer.
    Canvas canvas;
    auto top = std::make_unique<Button>();
    top->SetWidth(100.0f); top->SetHeight(100.0f);
    auto bottom = std::make_unique<Button>();
    bottom->SetWidth(100.0f); bottom->SetHeight(100.0f);
    Button* topPtr = top.get();
    Button* bottomPtr = bottom.get();

    canvas.Add(std::move(top));     // added first
    canvas.Add(std::move(bottom));  // added second — would win on insertion order

    Canvas::SetZIndex(topPtr, 1);
    Canvas::SetZIndex(bottomPtr, 0);

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});

    EXPECT_EQ(canvas.HitTestDeep(10.0f, 10.0f), topPtr);  // higher ZIndex wins
}

TEST(Canvas, HitTestWithSameZIndexReturnsLastAdded) {
    // Equal ZIndex falls back to reverse insertion order, matching Panel: the last
    // child added paints last, so it is on top and takes the input.
    Canvas canvas;
    auto first = std::make_unique<Button>();
    first->SetWidth(100.0f); first->SetHeight(100.0f);
    auto second = std::make_unique<Button>();
    second->SetWidth(100.0f); second->SetHeight(100.0f);
    Button* secondPtr = second.get();

    canvas.Add(std::move(first));
    canvas.Add(std::move(second));
    // Both stay at the default ZIndex 0.

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});

    EXPECT_EQ(canvas.HitTestDeep(10.0f, 10.0f), secondPtr);
}

TEST(Canvas, HitTestSkipsInvisibleTopmostChild) {
    // A hidden child keeps the bounds it was last arranged at, so it still contains
    // the point. It must not swallow the click — the visible child below it wins.
    Canvas canvas;
    auto visible = std::make_unique<Button>();
    visible->SetWidth(100.0f); visible->SetHeight(100.0f);
    auto hidden = std::make_unique<Button>();
    hidden->SetWidth(100.0f); hidden->SetHeight(100.0f);
    Button* visiblePtr = visible.get();
    Button* hiddenPtr = hidden.get();

    canvas.Add(std::move(visible));
    canvas.Add(std::move(hidden));

    Canvas::SetZIndex(hiddenPtr, 5);  // on top
    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});
    hiddenPtr->SetVisible(false);  // hide AFTER arrange, so bounds are kept

    EXPECT_EQ(canvas.HitTestDeep(10.0f, 10.0f), visiblePtr);
}

TEST(Canvas, HitTestMissReturnsNull) {
    // Canvas is not itself an interactive target: a point outside every child
    // yields null rather than the Canvas.
    Canvas canvas;
    auto child = std::make_unique<Button>();
    child->SetWidth(50.0f); child->SetHeight(50.0f);
    Button* ptr = child.get();
    canvas.Add(std::move(child));
    Canvas::SetLeft(ptr, 0.0f);
    Canvas::SetTop(ptr, 0.0f);

    canvas.Measure(300.0f, 300.0f);
    canvas.Arrange(RectDip{0.0f, 0.0f, 300.0f, 300.0f});

    EXPECT_EQ(canvas.HitTestDeep(200.0f, 200.0f), nullptr);
}

// --- The stored property --------------------------------------------------

TEST(Canvas, GetZIndexReturnsZeroByDefault) {
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));

    EXPECT_EQ(Canvas::GetZIndex(ptr), 0);
}

TEST(Canvas, SetZIndexStoresValue) {
    Canvas canvas;
    auto leaf = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* ptr = leaf.get();
    canvas.Add(std::move(leaf));

    Canvas::SetZIndex(ptr, 42);
    EXPECT_EQ(Canvas::GetZIndex(ptr), 42);
    Canvas::SetZIndex(ptr, -7);
    EXPECT_EQ(Canvas::GetZIndex(ptr), -7);
}

TEST(Canvas, ZIndexOnNullElementIsIgnored) {
    // The setters guard against null; the getters return the documented default.
    Canvas::SetZIndex(nullptr, 3);  // must not crash
    EXPECT_EQ(Canvas::GetZIndex(nullptr), 0);
}
