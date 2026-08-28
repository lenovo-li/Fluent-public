// UniformGridTests.cpp — headless tests for UniformGrid auto-wrap, uniform cell
// sizing, FirstColumn offset, and explicit Rows/Columns.

#include "../framework/Test.h"
#include "../../FluentUI/layout/UniformGrid.h"
#include "LayoutTestHelpers.h"

using namespace fluent;

// --- Auto columns: fit as many as will fill availW ------------------------

TEST(UniformGrid, AutoColumnsWrapsByAvailWidth) {
    // 4 children, each 100×50. availW=250 → fits 2 columns → 2 rows.
    UniformGrid grid;
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));

    grid.Measure(250.0f, 500.0f);
    const SizeDip d = grid.Desired();
    EXPECT_NEAR(d.w, 200.0f, 0.01f);  // 2 cols × 100
    EXPECT_NEAR(d.h, 100.0f, 0.01f);  // 2 rows × 50
}

TEST(UniformGrid, AutoColumnsWrapsEvenWhenChildrenVaryInSize) {
    // The cell is 150×100 (max of all children). availW=320 → 2 columns.
    UniformGrid grid;
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(150.0f, 100.0f));
    grid.Add(std::make_unique<TestLeaf>(120.0f, 80.0f));

    grid.Measure(320.0f, 500.0f);
    const SizeDip d = grid.Desired();
    EXPECT_NEAR(d.w, 300.0f, 0.01f);  // 2 cols × 150
    EXPECT_NEAR(d.h, 200.0f, 0.01f);  // 2 rows × 100
}

// --- Explicit Columns overrides auto-fit ---------------------------------

TEST(UniformGrid, ExplicitColumnsPreventsAutoWrap) {
    UniformGrid grid;
    grid.SetColumns(3);
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));

    // availW=150 would fit only 1 col if auto, but Columns=3 forces 3.
    grid.Measure(150.0f, 500.0f);
    const SizeDip d = grid.Desired();
    EXPECT_NEAR(d.w, 300.0f, 0.01f);  // 3 cols × 100 (ignores availW)
    EXPECT_NEAR(d.h, 100.0f, 0.01f);  // 2 rows × 50
}

TEST(UniformGrid, ColumnsChangeInvalidatesMeasure) {
    UniformGrid grid;
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Measure(500.0f, 500.0f);
    grid.ClearDirtySubtree();
    EXPECT_FALSE(Has(grid.Dirty(), DirtyFlags::Measure));

    grid.SetColumns(2);
    EXPECT_TRUE(Has(grid.Dirty(), DirtyFlags::Measure));
}

// --- Explicit Rows overrides auto-calculate from child count -------------

TEST(UniformGrid, ExplicitRowsOverridesAutoCalculation) {
    UniformGrid grid;
    grid.SetColumns(2);
    grid.SetRows(3);  // 2×3 = 6 cells, but only 4 children.
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));

    grid.Measure(500.0f, 500.0f);
    const SizeDip d = grid.Desired();
    EXPECT_NEAR(d.w, 200.0f, 0.01f);  // 2 cols
    EXPECT_NEAR(d.h, 150.0f, 0.01f);  // 3 rows (not 2)
}

TEST(UniformGrid, RowsChangeInvalidatesMeasure) {
    UniformGrid grid;
    grid.SetColumns(2);
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Measure(500.0f, 500.0f);
    grid.ClearDirtySubtree();
    EXPECT_FALSE(Has(grid.Dirty(), DirtyFlags::Measure));

    grid.SetRows(3);
    EXPECT_TRUE(Has(grid.Dirty(), DirtyFlags::Measure));
}

// --- FirstColumn offsets the first row -----------------------------------

TEST(UniformGrid, FirstColumnOffsetsFirstRow) {
    // Columns=3, FirstColumn=1 → first child starts at column 1, leaving column 0
    // empty. So the first row has only 2 children, and the 3rd child wraps to row 2.
    UniformGrid grid;
    grid.SetColumns(3);
    grid.SetFirstColumn(1);
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));

    grid.Measure(500.0f, 500.0f);
    const SizeDip d = grid.Desired();
    EXPECT_NEAR(d.w, 300.0f, 0.01f);  // 3 cols
    EXPECT_NEAR(d.h, 100.0f, 0.01f);  // 2 rows (first row has 2, second has 1)
}

TEST(UniformGrid, FirstColumnChangeInvalidatesArrange) {
    UniformGrid grid;
    grid.SetColumns(3);
    grid.Add(std::make_unique<TestLeaf>(100.0f, 50.0f));
    grid.Measure(500.0f, 500.0f);
    grid.Arrange(RectDip{0.0f, 0.0f, 300.0f, 100.0f});
    grid.ClearDirtySubtree();
    EXPECT_FALSE(Has(grid.Dirty(), DirtyFlags::Arrange));

    // FirstColumn only moves children within already-sized cells, so it dirties
    // Arrange rather than Measure — the grid's own desired size is unchanged.
    grid.SetFirstColumn(1);
    EXPECT_TRUE(Has(grid.Dirty(), DirtyFlags::Arrange));
}

// --- Arrange positions children in cells ----------------------------------

TEST(UniformGrid, ArrangePositionsChildrenInCells) {
    UniformGrid grid;
    grid.SetColumns(2);
    auto c1 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    auto c2 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    auto c3 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* p1 = c1.get();
    TestLeaf* p2 = c2.get();
    TestLeaf* p3 = c3.get();
    grid.Add(std::move(c1));
    grid.Add(std::move(c2));
    grid.Add(std::move(c3));

    grid.Measure(500.0f, 500.0f);
    grid.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});

    // 2 cols: first row has c1 (0,0) and c2 (100,0), second row has c3 (0,50).
    const RectDip& r1 = p1->Bounds();
    const RectDip& r2 = p2->Bounds();
    const RectDip& r3 = p3->Bounds();
    EXPECT_NEAR(r1.x, 0.0f, 0.01f);
    EXPECT_NEAR(r1.y, 0.0f, 0.01f);
    EXPECT_NEAR(r2.x, 100.0f, 0.01f);
    EXPECT_NEAR(r2.y, 0.0f, 0.01f);
    EXPECT_NEAR(r3.x, 0.0f, 0.01f);
    EXPECT_NEAR(r3.y, 50.0f, 0.01f);
}

TEST(UniformGrid, ArrangeRespectsFirstColumn) {
    UniformGrid grid;
    grid.SetColumns(3);
    grid.SetFirstColumn(1);
    auto c1 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    auto c2 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* p1 = c1.get();
    TestLeaf* p2 = c2.get();
    grid.Add(std::move(c1));
    grid.Add(std::move(c2));

    grid.Measure(500.0f, 500.0f);
    grid.Arrange(RectDip{0.0f, 0.0f, 300.0f, 100.0f});

    // c1 starts at column 1 (x=100), c2 at column 2 (x=200).
    const RectDip& r1 = p1->Bounds();
    const RectDip& r2 = p2->Bounds();
    EXPECT_NEAR(r1.x, 100.0f, 0.01f);
    EXPECT_NEAR(r1.y, 0.0f, 0.01f);
    EXPECT_NEAR(r2.x, 200.0f, 0.01f);
    EXPECT_NEAR(r2.y, 0.0f, 0.01f);
}

// --- Empty or invisible children are skipped ------------------------------

TEST(UniformGrid, SkipsInvisibleChildren) {
    UniformGrid grid;
    grid.SetColumns(2);
    auto c1 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    auto c2 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    c2->SetVisible(false);
    auto c3 = std::make_unique<TestLeaf>(100.0f, 50.0f);
    TestLeaf* p1 = c1.get();
    TestLeaf* p3 = c3.get();
    grid.Add(std::move(c1));
    grid.Add(std::move(c2));
    grid.Add(std::move(c3));

    grid.Measure(500.0f, 500.0f);
    grid.Arrange(RectDip{0.0f, 0.0f, 200.0f, 100.0f});

    // Only c1 and c3 are visible, so they go in the first row.
    const RectDip& r1 = p1->Bounds();
    const RectDip& r3 = p3->Bounds();
    EXPECT_NEAR(r1.x, 0.0f, 0.01f);
    EXPECT_NEAR(r1.y, 0.0f, 0.01f);
    EXPECT_NEAR(r3.x, 100.0f, 0.01f);
    EXPECT_NEAR(r3.y, 0.0f, 0.01f);
}
