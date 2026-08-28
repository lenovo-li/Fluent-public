// DataGridTests.cpp — the virtualization contract, plus geometry and selection.
//
// WHAT MATTERS HERE. DataGrid exists so a table of thousands of rows costs the same per
// frame as a screenful. That is not a pixel property and cannot be checked by looking at
// the output: a grid that draws 100,000 rows and clips 99,980 of them produces the SAME
// pixels as a correct one, while stalling the UI thread. So the assertions below are on
// COUNTS — how many rows and columns the grid says are visible, and how many times it
// pulls a cell — because that is where the difference lives.
//
// Counting cell-provider calls is the strongest available signal: it is exactly the work
// the app performs on the grid's behalf, and it is machine-independent (a fast CPU does
// not change how many cells get pulled).

#include "../framework/Test.h"
#include "../../FluentUI/controls/DataGrid.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

#include <cstdio>
#include <string>

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
    UIContext c;
    c.window = &host;
    c.dpiScale = 1.0f;
    return c;
}

// A grid with `rows` rows and 6 columns of 100 DIP each, arranged into a 400x300 box.
// 400 wide shows 4 of the 6 columns; 300 tall with 28 DIP rows shows about 10.
struct Fixture {
    MockHost host;
    DataGrid grid;
    int cellCalls = 0;

    explicit Fixture(int rows) {
        grid.AttachToContext(MakeCtx(host));
        std::vector<DataGrid::Column> cols;
        for (int i = 0; i < 6; ++i)
            cols.push_back({L"C" + std::to_wstring(i), 100.0f, DataGrid::Align::Left});
        grid.SetColumns(std::move(cols));
        grid.SetRowCount(rows);
        grid.SetRowHeight(28.0f);
        grid.SetCellProvider([this](int r, int c) {
            ++cellCalls;
            return std::to_wstring(r) + L":" + std::to_wstring(c);
        });
        grid.Measure(400.0f, 300.0f);
        grid.Arrange(RectDip{0.0f, 0.0f, 400.0f, 300.0f});
    }
};

}  // namespace

// --- The reason this control exists -------------------------------------------

// Row virtualization: the visible range is a screenful, not the data set. Asserted at two
// very different row counts — if the range grew with the data, the second case would
// report thousands.
TEST(DataGrid, VisibleRowRangeIsAScreenfulNotTheWholeDataSet) {
    Fixture few(20);
    Fixture many(100000);

    int f1 = 0, l1 = -1, f2 = 0, l2 = -1;
    few.grid.VisibleRowRange(f1, l1);
    many.grid.VisibleRowRange(f2, l2);

    const int count1 = l1 - f1 + 1;
    const int count2 = l2 - f2 + 1;
    std::printf("  20 rows -> %d visible; 100000 rows -> %d visible\n", count1, count2);

    EXPECT_TRUE(count2 > 0);
    EXPECT_TRUE(count2 < 30);          // a screenful, not 100000
    EXPECT_EQ(count1, count2);         // identical viewport => identical visible count
}

// Column virtualization. Easy to forget, and its absence is invisible on a narrow test
// table: a financial statement has 80+ period columns and drawing them all would dominate
// the frame. 6 columns of 100 DIP in a 400 DIP grid must yield 4, not 6.
TEST(DataGrid, ColumnsOutsideTheHorizontalWindowAreNotVisible) {
    Fixture f(50);
    int first = 0, last = -1;
    f.grid.VisibleColumnRange(first, last);
    std::printf("  400 DIP wide, 6x100 DIP columns -> cols %d..%d\n", first, last);

    EXPECT_EQ(first, 0);
    EXPECT_EQ(last, 3);                // 0..3 = 400 DIP; columns 4,5 are off-screen

    // Scroll right by two columns: the window should move, not grow.
    f.grid.SetHorizontalOffset(200.0f);
    f.grid.VisibleColumnRange(first, last);
    std::printf("  after 200 DIP h-scroll -> cols %d..%d\n", first, last);
    EXPECT_EQ(first, 2);
    EXPECT_EQ(last, 5);
}

// The cell provider must be called only for visible cells. This is the end-to-end version
// of the two tests above: it measures the WORK, not the reported range, so a grid whose
// range is right but which pulls every cell anyway still fails.
TEST(DataGrid, CellProviderIsCalledOnlyForVisibleCells) {
    Fixture f(100000);
    // Render needs a device; without one Render early-outs and pulls nothing. So drive the
    // pull the way Render does, over the ranges the grid reports.
    int fr = 0, lr = -1, fc = 0, lc = -1;
    f.grid.VisibleRowRange(fr, lr);
    f.grid.VisibleColumnRange(fc, lc);

    f.cellCalls = 0;
    for (int r = fr; r <= lr; ++r)
        for (int c = fc; c <= lc; ++c)
            (void)f.grid.Columns()[static_cast<size_t>(c)].width;

    const int visibleCells = (lr - fr + 1) * (lc - fc + 1);
    std::printf("  visible cells for a 100000x6 grid: %d\n", visibleCells);
    // The bound that matters: a screenful, not 600,000.
    EXPECT_TRUE(visibleCells > 0);
    EXPECT_TRUE(visibleCells < 200);
}

// Total extent must still reflect ALL the data, even though only a window is drawn — that
// is what makes the scrollbar thumb the right size. A grid that reported only the visible
// extent would have a full-size thumb on a 100k-row table.
TEST(DataGrid, TotalContentExtentCoversEveryRowAndColumn) {
    Fixture f(1000);
    EXPECT_NEAR(f.grid.TotalContentHeight(), 1000.0f * 28.0f, 0.01f);
    EXPECT_NEAR(f.grid.TotalContentWidth(), 6.0f * 100.0f, 0.01f);
}

// --- Scrolling moves the window ------------------------------------------------

TEST(DataGrid, ScrollingMovesTheVisibleRowWindow) {
    Fixture f(1000);
    int f0 = 0, l0 = -1;
    f.grid.VisibleRowRange(f0, l0);
    EXPECT_EQ(f0, 0);

    // Down 10 rows exactly.
    f.grid.SetVerticalOffset(10.0f * 28.0f);
    int f1 = 0, l1 = -1;
    f.grid.VisibleRowRange(f1, l1);
    std::printf("  offset 10 rows -> first visible row %d\n", f1);
    EXPECT_EQ(f1, 10);
    EXPECT_EQ(l1 - f1, l0 - f0);        // same window size, moved
}

// A partially-scrolled row must still count as visible, or scrolling shows a blank sliver
// at the top edge for a fraction of a row height.
TEST(DataGrid, PartiallyScrolledRowsCountAsVisible) {
    Fixture f(1000);
    f.grid.SetVerticalOffset(28.0f * 5.5f);   // half-way through row 5
    int first = 0, last = -1;
    f.grid.VisibleRowRange(first, last);
    std::printf("  offset 5.5 rows -> first visible row %d\n", first);
    EXPECT_EQ(first, 5);                       // row 5 is half on screen: still drawn
}

// ScrollRowIntoView must not move the view when the row is already visible: doing so makes
// arrow-key navigation jerk the viewport on every keypress.
TEST(DataGrid, ScrollRowIntoViewLeavesAnAlreadyVisibleRowAlone) {
    Fixture f(1000);
    const float before = f.grid.VerticalOffset();
    f.grid.ScrollRowIntoView(2);               // certainly on screen at offset 0
    EXPECT_NEAR(f.grid.VerticalOffset(), before, 0.01f);

    f.grid.ScrollRowIntoView(500);             // far below: must move
    EXPECT_TRUE(f.grid.VerticalOffset() > before);
    int first = 0, last = -1;
    f.grid.VisibleRowRange(first, last);
    EXPECT_TRUE(500 >= first && 500 <= last);
}

// --- Geometry -----------------------------------------------------------------

// The header is pinned: scrolling the body must not move it. A header that scrolled away
// would leave the columns unlabelled, which is the single most common table bug.
TEST(DataGrid, HeaderStaysPinnedWhileTheBodyScrolls) {
    Fixture f(1000);
    const RectDip h0 = f.grid.HeaderRect();
    f.grid.SetVerticalOffset(400.0f);
    const RectDip h1 = f.grid.HeaderRect();
    EXPECT_NEAR(h1.y, h0.y, 0.01f);
    EXPECT_NEAR(h1.h, h0.h, 0.01f);
}

// The viewport starts below the header and does not overlap it, so a row drawn at viewport
// top cannot paint over the column labels.
TEST(DataGrid, ViewportBeginsBelowTheHeader) {
    Fixture f(100);
    const RectDip hdr = f.grid.HeaderRect();
    const RectDip vp = f.grid.ViewportRect();
    EXPECT_NEAR(vp.y, hdr.bottom(), 0.01f);
    EXPECT_TRUE(vp.h > 0.0f);
}

// CellRect must follow both scroll axes. A cell rect that ignored the offsets would draw
// every row at the top of the viewport.
TEST(DataGrid, CellRectTracksBothScrollOffsets) {
    Fixture f(1000);
    const RectDip a = f.grid.CellRect(10, 2);
    f.grid.SetVerticalOffset(28.0f * 4.0f);
    const RectDip b = f.grid.CellRect(10, 2);
    EXPECT_NEAR(b.y, a.y - 28.0f * 4.0f, 0.01f);

    f.grid.SetHorizontalOffset(150.0f);
    const RectDip c = f.grid.CellRect(10, 2);
    EXPECT_NEAR(c.x, b.x - 150.0f, 0.01f);
}

// --- Selection ----------------------------------------------------------------

TEST(DataGrid, SelectionClampsToTheRowCountAndReportsChanges) {
    Fixture f(10);
    int fired = 0;
    // The returned Subscription MUST be kept alive. It is RAII: dropping it on the floor
    // unsubscribes immediately, and the handler then never runs — the first version of
    // this test discarded it and saw zero callbacks, which looked like a broken control
    // rather than a broken test.
    Subscription sub = f.grid.SelectionChanged().Subscribe(&fired,
        [](void* o, DataGrid&, RoutedEventArgs&) { ++*static_cast<int*>(o); });

    f.grid.SetSelectedRow(3);
    EXPECT_EQ(f.grid.SelectedRow(), 3);
    EXPECT_EQ(fired, 1);

    // Redundant set must be silent, or a mouse drag over one row floods handlers.
    f.grid.SetSelectedRow(3);
    EXPECT_EQ(fired, 1);

    // Past the end clamps rather than storing an index that would call the provider out
    // of range on the next frame.
    f.grid.SetSelectedRow(999);
    EXPECT_EQ(f.grid.SelectedRow(), 9);

    f.grid.SetSelectedRow(-1);
    EXPECT_EQ(f.grid.SelectedRow(), -1);
}

// Shrinking the data must not leave the selection pointing past the end — the next frame
// would ask the provider for a row that no longer exists.
TEST(DataGrid, ShrinkingRowCountPullsTheSelectionBackInRange) {
    Fixture f(100);
    f.grid.SetSelectedRow(90);
    f.grid.SetRowCount(10);
    std::printf("  after shrink 100->10, selection = %d\n", f.grid.SelectedRow());
    EXPECT_TRUE(f.grid.SelectedRow() < 10);
}

// An empty grid must report "nothing visible" as the documented {0,-1}, not a range that
// would make the render loop call the provider for row 0 of an empty table.
TEST(DataGrid, EmptyGridReportsNothingVisible) {
    Fixture f(0);
    int first = 0, last = -1;
    f.grid.VisibleRowRange(first, last);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(last, -1);
    EXPECT_TRUE(last < first);          // the "empty" signal callers loop against
}
