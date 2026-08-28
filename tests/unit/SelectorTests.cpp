// SelectorTests.cpp — unit tests for ItemsControl / Selector via ComboBox and
// TreeView (WP-06 Stage 3).
//
// Headless: no GPU, no PopupHost open. ComboBox's popup is created only on tree
// attach, so these exercise the item store + selection triad without a window.

#include "../framework/Test.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/controls/TreeView.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace fluent;

namespace {
void CountComboSel(void* ctx, ComboBox&, int& idx) {
    int* p = static_cast<int*>(ctx);
    p[0]++;      // fired count
    p[1] = idx;  // last index
}
void CountTreeSel(void* ctx, TreeView&, TreeSelection& s) {
    int* p = static_cast<int*>(ctx);
    p[0]++;               // fired count
    p[1] = s.visibleIndex; // last visible index
}
std::vector<TreeViewRow> MakeFlatRows(int n) {
    std::vector<TreeViewRow> rows;
    for (int i = 0; i < n; ++i) {
        TreeViewRow r;
        r.id = i + 1;
        r.parentId = -1;
        r.text = L"row" + std::to_wstring(i);
        rows.push_back(r);
    }
    return rows;
}
} // namespace

// ---------------------------------------------------------------------------
// ItemsControl (via ComboBox)
// ---------------------------------------------------------------------------

TEST(ItemsControl, SetItemsStoresAndCounts) {
    ComboBox cb;
    cb.SetItems({L"a", L"b", L"c"});
    EXPECT_EQ(cb.ItemCount(), 3);
    EXPECT_TRUE(cb.ItemAt(0) != nullptr);
    EXPECT_TRUE(*cb.ItemAt(1) == L"b");
}

TEST(ItemsControl, ItemAtOutOfRangeReturnsNull) {
    ComboBox cb;
    cb.SetItems({L"a"});
    EXPECT_TRUE(cb.ItemAt(-1) == nullptr);
    EXPECT_TRUE(cb.ItemAt(5) == nullptr);
}

// Replacing the items re-clamps a now-out-of-range selection.
TEST(ItemsControl, SetItemsReclampsSelection) {
    ComboBox cb;
    cb.SetItems({L"a", L"b", L"c", L"d"});
    cb.SetSelectedIndex(3);
    EXPECT_EQ(cb.SelectedIndex(), 3);
    cb.SetItems({L"x", L"y"});  // shrink
    EXPECT_EQ(cb.SelectedIndex(), 1);  // clamped to last valid
}

// ---------------------------------------------------------------------------
// Selector (via ComboBox)
// ---------------------------------------------------------------------------

TEST(Selector, DefaultNoSelection) {
    ComboBox cb;
    EXPECT_EQ(cb.SelectedIndex(), -1);
    EXPECT_TRUE(cb.SelectedItem() == nullptr);
}

TEST(Selector, SetSelectedIndexClampsAndDedups) {
    ComboBox cb;
    cb.SetItems({L"a", L"b", L"c"});
    int state[2] = {0, -1};
    auto sub = cb.SelectionChanged().Subscribe(state, CountComboSel);

    cb.SetSelectedIndex(1);
    EXPECT_EQ(cb.SelectedIndex(), 1);
    EXPECT_EQ(state[0], 1);   // fired once
    EXPECT_EQ(state[1], 1);

    cb.SetSelectedIndex(1);   // same index — no re-fire
    EXPECT_EQ(state[0], 1);

    cb.SetSelectedIndex(99);  // clamp to last
    EXPECT_EQ(cb.SelectedIndex(), 2);
    EXPECT_EQ(state[0], 2);
}

TEST(Selector, SelectedItemReturnsPointer) {
    ComboBox cb;
    cb.SetItems({L"apple", L"banana"});
    cb.SetSelectedIndex(1);
    const std::wstring* item = cb.SelectedItem();
    EXPECT_TRUE(item != nullptr);
    EXPECT_TRUE(*item == L"banana");
}

TEST(Selector, NegativeIndexClearsSelection) {
    ComboBox cb;
    cb.SetItems({L"a", L"b"});
    cb.SetSelectedIndex(1);
    cb.SetSelectedIndex(-5);   // clamps to -1
    EXPECT_EQ(cb.SelectedIndex(), -1);
    EXPECT_TRUE(cb.SelectedItem() == nullptr);
}

// ---------------------------------------------------------------------------
// TreeView: visible-index API over row-index storage
// ---------------------------------------------------------------------------

TEST(Selector, TreeViewSetRowsStoresItems) {
    TreeView tv;
    tv.SetRows(MakeFlatRows(3));
    EXPECT_EQ(tv.ItemCount(), 3);
    // SetRows auto-selects the first visible row.
    EXPECT_EQ(tv.SelectedIndex(), 0);
}

// SetSelectedIndex takes a visible index; for a flat tree it maps 1:1 to the row
// index, and the SelectionChanged payload carries that visible index + row ptr.
TEST(Selector, TreeViewSelectionFiresRichEvent) {
    TreeView tv;
    tv.SetRows(MakeFlatRows(4));
    int state[2] = {0, -1};
    auto sub = tv.SelectionChanged().Subscribe(state, CountTreeSel);

    tv.SetSelectedIndex(2);
    EXPECT_EQ(tv.SelectedIndex(), 2);  // row index (flat: == visible index)
    EXPECT_EQ(state[0], 1);
    EXPECT_EQ(state[1], 2);            // payload visible index

    const TreeViewRow* row = tv.SelectedRow();
    EXPECT_TRUE(row != nullptr);
    EXPECT_TRUE(row->text == L"row2");
}

// Re-selecting the same visible index does not re-fire.
TEST(Selector, TreeViewReselectNoOp) {
    TreeView tv;
    tv.SetRows(MakeFlatRows(3));
    tv.SetSelectedIndex(1);
    int state[2] = {0, -1};
    auto sub = tv.SelectionChanged().Subscribe(state, CountTreeSel);
    tv.SetSelectedIndex(1);   // already selected
    EXPECT_EQ(state[0], 0);
}

// SetRows restores selection by row id across a row-set replacement.
TEST(Selector, TreeViewSetRowsRestoresSelectionById) {
    TreeView tv;
    tv.SetRows(MakeFlatRows(5));
    tv.SetSelectedIndex(3);   // selects id=4
    int selId = tv.SelectedRow()->id;
    EXPECT_EQ(selId, 4);

    // Replace with a new set that still contains id=4 (at a different position).
    std::vector<TreeViewRow> rows2 = MakeFlatRows(5);
    // reverse order so id=4 lands at index 0
    std::reverse(rows2.begin(), rows2.end());
    tv.SetRows(std::move(rows2));
    // Selection restored to the row with id=4 (now at row index 1).
    EXPECT_TRUE(tv.SelectedRow() != nullptr);
    EXPECT_EQ(tv.SelectedRow()->id, 4);
}
