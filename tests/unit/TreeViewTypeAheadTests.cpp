// TreeViewTypeAheadTests.cpp — regression tests for TreeView keyboard type-ahead
// search. Guards the crash where OnTextInput recursed into itself on a
// no-match multi-character buffer and blew the stack (an unbounded self-call
// that re-appended the same char every time). Pure logic: no DWrite / GPU — the
// type-ahead search only touches the row model and selection index.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TreeView.h"

using namespace fluent;

namespace {
// Populate a small flat tree (no DWrite needed): three visible rows. TreeView is
// move-only (UIElement deletes copy and declares no move), so fill in place.
void FillTree(TreeView& tv) {
    std::vector<TreeViewRow> rows;
    TreeViewRow a; a.id = 1; a.text = L"Apple";  rows.push_back(a);
    TreeViewRow b; b.id = 2; b.text = L"Banana"; rows.push_back(b);
    TreeViewRow c; c.id = 3; c.text = L"Cherry"; rows.push_back(c);
    tv.SetRows(std::move(rows));
}
}  // namespace

// A matching key selects the row starting with that letter.
TEST(TreeViewTypeAhead, SingleCharSelects) {
    TreeView tv; FillTree(tv);
    tv.OnTextInput(L'b');
    EXPECT_EQ(tv.SelectedIndex(), 1);  // Banana
}

// The original crash repro: type a matching letter, then a letter that does NOT
// extend the current match. Must not recurse/stack-overflow, and must fall back
// to selecting a row starting with the new letter.
TEST(TreeViewTypeAhead, NoMatchFallbackDoesNotCrash) {
    TreeView tv; FillTree(tv);
    tv.OnTextInput(L'a');            // "a" -> Apple (index 0)
    EXPECT_EQ(tv.SelectedIndex(), 0);
    tv.OnTextInput(L'c');            // "ac" no match -> fall back to "c" -> Cherry
    EXPECT_EQ(tv.SelectedIndex(), 2);
}

// A truly unmatched key (no row starts with it) after a match: no crash, and the
// selection is left unchanged (nothing to jump to).
TEST(TreeViewTypeAhead, TotallyUnmatchedKeepsSelection) {
    TreeView tv; FillTree(tv);
    tv.OnTextInput(L'b');            // Banana (index 1)
    EXPECT_EQ(tv.SelectedIndex(), 1);
    tv.OnTextInput(L'z');            // "bz" no match, "z" no match -> unchanged
    EXPECT_EQ(tv.SelectedIndex(), 1);
}

// Hammer many unmatched keys: the old bug overflowed the stack after a few
// presses. This must complete without recursion.
TEST(TreeViewTypeAhead, ManyUnmatchedKeysAreBounded) {
    TreeView tv; FillTree(tv);
    tv.OnTextInput(L'a');
    for (int i = 0; i < 200; ++i)
        tv.OnTextInput(L'z');        // each: "az.." no match, "z" no match
    EXPECT_EQ(tv.SelectedIndex(), 0);  // still Apple; no crash
}

// Extending a match narrows correctly ("ba" still matches Banana).
TEST(TreeViewTypeAhead, ExtendingPrefixKeepsMatch) {
    TreeView tv; FillTree(tv);
    tv.OnTextInput(L'b');
    tv.OnTextInput(L'a');            // "ba" -> Banana
    EXPECT_EQ(tv.SelectedIndex(), 1);
}
