// GridTests.cpp — locks the current Grid measure/arrange behavior.
//
// Pure layout math only (no HWND / D3D). Captures how the Grid resolves
// Pixel / Auto / Star tracks, spacing, spans, and cell placement TODAY, so the
// tree refactor has a regression net.

#include "../framework/Test.h"
#include "LayoutTestHelpers.h"
#include "../../FluentUI/layout/Grid.h"

using namespace fluent;

// Two columns: fixed 64px label + star input. Star column takes the leftover.
TEST(Grid, PixelPlusStar_Columns) {
    Grid g;
    g.SetColumns({GridLength::Pixels(64.0f), GridLength::Star()});
    g.SetRows({GridLength::Star()});

    auto* label = g.Emplace<TestLeaf>();
    g.SetCell(label, 0, 0);
    auto* input = g.Emplace<TestLeaf>();
    g.SetCell(input, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 200.0f, 40.0f});

    // Column 0 is 64 wide at x=0; column 1 fills the rest (200-64=136) at x=64.
    EXPECT_NEAR(label->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(label->Bounds().w, 64.0f, 0.01f);
    EXPECT_NEAR(input->Bounds().x, 64.0f, 0.01f);
    EXPECT_NEAR(input->Bounds().w, 136.0f, 0.01f);
}

// Column spacing sits between adjacent tracks and shrinks star leftover.
TEST(Grid, ColumnSpacing_ReducesStar) {
    Grid g;
    g.SetColumns({GridLength::Pixels(50.0f), GridLength::Star()});
    g.SetRows({GridLength::Star()});
    g.SetColumnSpacing(10.0f);

    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 200.0f, 40.0f});

    // used = 50 + 10 spacing; star = 200 - 60 = 140. b starts at 50+10=60.
    EXPECT_NEAR(a->Bounds().w, 50.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 60.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().w, 140.0f, 0.01f);
}

// Two equal-weight star columns split space evenly.
TEST(Grid, TwoStarColumns_SplitEvenly) {
    Grid g;
    g.SetColumns({GridLength::Star(), GridLength::Star()});
    g.SetRows({GridLength::Star()});

    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 300.0f, 40.0f});

    EXPECT_NEAR(a->Bounds().w, 150.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().x, 150.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().w, 150.0f, 0.01f);
}

// Weighted stars split in proportion (1:2 -> 100 / 200 of 300).
TEST(Grid, WeightedStars_Proportional) {
    Grid g;
    g.SetColumns({GridLength::Star(1.0f), GridLength::Star(2.0f)});
    g.SetRows({GridLength::Star()});

    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 0, 1);

    g.UpdateLayout({0.0f, 0.0f, 300.0f, 40.0f});

    EXPECT_NEAR(a->Bounds().w, 100.0f, 0.01f);
    EXPECT_NEAR(b->Bounds().w, 200.0f, 0.01f);
}

// An Auto row sizes to the tallest child in it.
TEST(Grid, AutoRow_SizesToContent) {
    Grid g;
    g.SetColumns({GridLength::Star()});
    g.SetRows({GridLength::Auto(), GridLength::Star()});

    auto* top = g.Emplace<TestLeaf>();
    top->SetHeight(28.0f);
    top->SetVAlign(VAlign::Top);
    g.SetCell(top, 0, 0);
    auto* fill = g.Emplace<TestLeaf>();
    g.SetCell(fill, 1, 0);

    g.UpdateLayout({0.0f, 0.0f, 100.0f, 200.0f});

    // Auto row = 28; star row fills the rest (172) at y=28.
    EXPECT_NEAR(top->Bounds().h, 28.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().y, 28.0f, 0.01f);
    EXPECT_NEAR(fill->Bounds().h, 172.0f, 0.01f);
}

// Column span: a child spanning two columns covers both tracks + interior gap.
TEST(Grid, ColumnSpan_CoversTracksAndGap) {
    Grid g;
    g.SetColumns({GridLength::Pixels(50.0f), GridLength::Pixels(50.0f)});
    g.SetRows({GridLength::Star()});
    g.SetColumnSpacing(10.0f);

    auto* wide = g.Emplace<TestLeaf>();
    g.SetCell(wide, /*row*/ 0, /*col*/ 0, /*rowSpan*/ 1, /*colSpan*/ 2);

    g.UpdateLayout({0.0f, 0.0f, 200.0f, 40.0f});

    // 50 + 10 gap + 50 = 110, starting at x=0.
    EXPECT_NEAR(wide->Bounds().x, 0.0f, 0.01f);
    EXPECT_NEAR(wide->Bounds().w, 110.0f, 0.01f);
}

// No tracks defined: the grid degenerates to a single cell filling its bounds.
TEST(Grid, NoTracks_SingleCellFills) {
    Grid g;

    auto* only = g.Emplace<TestLeaf>();
    g.SetCell(only, 0, 0);

    g.UpdateLayout({5.0f, 7.0f, 120.0f, 80.0f});

    EXPECT_NEAR(only->Bounds().x, 5.0f, 0.01f);
    EXPECT_NEAR(only->Bounds().y, 7.0f, 0.01f);
    EXPECT_NEAR(only->Bounds().w, 120.0f, 0.01f);
    EXPECT_NEAR(only->Bounds().h, 80.0f, 0.01f);
}

// Regression: Auto-height children measured at infinite availH must not report
// inf as their desired size. That poisons track resolution (inf - inf = NaN),
// and the Grid collapses rows to garbage heights. The symptom: cards inside a
// ScrollPanel (which deliberately measures unbounded) rendered at 46~70 DIP with
// overlapping, clipped content. Root cause: FrameworkElement::Measure's default
// path returned availH when Auto, and controls without a Measure override
// (Button, TextBox) inherited that infinite desired size.
TEST(Grid, AutoRowsWithUnboundedHeight_DoNotCollapse) {
    Grid g;
    g.SetColumns({GridLength::Auto()});
    g.SetRows({GridLength::Auto(), GridLength::Auto()});

    // Default-measure leaves: no explicit height, no Measure override, so they
    // hit FrameworkElement::Measure's "fill what's offered" path. Under infinite
    // availH that used to return inf, which summed to inf, then leftover became
    // inf - inf = NaN, and rows resolved to ~46-70 instead of 0.
    auto* a = g.Emplace<TestLeaf>();
    g.SetCell(a, 0, 0);
    auto* b = g.Emplace<TestLeaf>();
    g.SetCell(b, 1, 0);

    // Measure at unbounded height (the ScrollPanel scenario).
    g.Measure(200.0f, std::numeric_limits<float>::infinity());
    const SizeDip& desired = g.Desired();

    // Both leaves have no content and no explicit size, so desired.h should be 0
    // (two rows of 0 height), NOT inf, NOT NaN, NOT a small garbage value.
    EXPECT_TRUE(std::isfinite(desired.h));
    EXPECT_NEAR(desired.h, 0.0f, 0.01f);
}
