// ComboBoxTests.cpp — unit tests for ComboBox editable mode.
//
// The non-editable ComboBox is covered by SelectorTests (the base ItemsControl/
// Selector triad). These tests focus exclusively on the new editable-mode
// behaviour: text manipulation, caret movement, TextChanged events, and the
// toggle between modes.
//
// Headless: no popup is opened (requires a window HWND); only the text state
// and key handling are exercised.

#include "../framework/Test.h"
#include "../../FluentUI/controls/ComboBox.h"

using namespace fluent;

namespace {

KeyEventArgs MakeKey(UINT vk, ModifierKeys mods = ModifierKeys::None) {
    KeyEventArgs e;
    e.vk = vk;
    e.modifiers = mods;
    return e;
}

void CountTextChanged(void* ctx, ComboBox&, std::wstring& txt) {
    auto* state = static_cast<std::pair<int, std::wstring>*>(ctx);
    state->first++;
    state->second = txt;
}

}  // namespace

// --- Defaults ----------------------------------------------------------------

TEST(ComboBoxEditable, DefaultIsNotEditable) {
    ComboBox cb;
    EXPECT_FALSE(cb.IsEditable());
    EXPECT_TRUE(cb.Text().empty());
}

TEST(ComboBoxEditable, SetEditableSetsFlag) {
    ComboBox cb;
    cb.SetEditable(true);
    EXPECT_TRUE(cb.IsEditable());
}

// --- SetText / Text() --------------------------------------------------------

TEST(ComboBoxEditable, SetTextStoresText) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"hello");
    EXPECT_TRUE(cb.Text() == L"hello");
    EXPECT_EQ(cb.CaretIndex(), 5u);  // caret placed at end
}

TEST(ComboBoxEditable, SetTextInReadonlyModeIgnored) {
    ComboBox cb;
    cb.SetEditable(false);
    cb.SetText(L"hello");
    EXPECT_TRUE(cb.Text().empty());
}

TEST(ComboBoxEditable, TextInReadonlyReturnsSelectedItem) {
    ComboBox cb;
    cb.SetItems({L"Apple", L"Banana"});
    cb.SetSelectedIndex(1);
    EXPECT_TRUE(cb.Text() == L"Banana");
}

TEST(ComboBoxEditable, TextInReadonlyReturnsEmptyWhenNoSelection) {
    ComboBox cb;
    cb.SetItems({L"Apple", L"Banana"});
    EXPECT_TRUE(cb.Text().empty());  // selectedIndex_ == -1
}

TEST(ComboBoxEditable, SwitchToEditableSeedsFromSelection) {
    ComboBox cb;
    cb.SetItems({L"Apple", L"Banana"});
    cb.SetSelectedIndex(0);
    cb.SetEditable(true);
    EXPECT_TRUE(cb.Text() == L"Apple");
    EXPECT_EQ(cb.CaretIndex(), 5u);
}

TEST(ComboBoxEditable, SwitchBackToReadonlyDropsTypedText) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"typed");
    cb.SetEditable(false);
    // No selection → Text() is empty; the typed text is gone.
    EXPECT_TRUE(cb.Text().empty());
}

// --- OnTextInput -------------------------------------------------------------

TEST(ComboBoxEditable, TextInputInsertsCharacters) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.OnTextInput(L'a');
    cb.OnTextInput(L'b');
    cb.OnTextInput(L'c');
    EXPECT_TRUE(cb.Text() == L"abc");
    EXPECT_EQ(cb.CaretIndex(), 3u);
}

TEST(ComboBoxEditable, TextInputIgnoredWhenNotEditable) {
    ComboBox cb;
    cb.SetEditable(false);
    cb.OnTextInput(L'x');
    EXPECT_TRUE(cb.Text().empty());
}

TEST(ComboBoxEditable, ControlCharactersAreIgnored) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.OnTextInput(L'\x01');  // SOH — control char
    EXPECT_TRUE(cb.Text().empty());
}

// --- Backspace / Delete ------------------------------------------------------

TEST(ComboBoxEditable, BackspaceDeletesCharBeforeCaret) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"abc");
    // caret at 3 after SetText
    KeyEventArgs e = MakeKey(VK_BACK);
    cb.OnKeyDownRouted(e);
    EXPECT_TRUE(cb.Text() == L"ab");
    EXPECT_EQ(cb.CaretIndex(), 2u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, BackspaceAtStartIsNoOp) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"a");
    // Move caret to 0
    KeyEventArgs home = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(home);
    KeyEventArgs e = MakeKey(VK_BACK);
    cb.OnKeyDownRouted(e);
    EXPECT_TRUE(cb.Text() == L"a");
    EXPECT_TRUE(e.handled);  // consumed even though nothing erased
}

TEST(ComboBoxEditable, DeleteRemovesCharAtCaret) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"abc");
    KeyEventArgs home = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(home);  // caret → 0
    KeyEventArgs e = MakeKey(VK_DELETE);
    cb.OnKeyDownRouted(e);
    EXPECT_TRUE(cb.Text() == L"bc");
    EXPECT_EQ(cb.CaretIndex(), 0u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, DeleteAtEndIsNoOp) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"a");  // caret at 1 = end
    KeyEventArgs e = MakeKey(VK_DELETE);
    cb.OnKeyDownRouted(e);
    EXPECT_TRUE(cb.Text() == L"a");
    EXPECT_EQ(cb.CaretIndex(), 1u);
    EXPECT_TRUE(e.handled);
}

// --- Caret navigation --------------------------------------------------------

TEST(ComboBoxEditable, LeftMovesCaretLeft) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"abc");
    KeyEventArgs e = MakeKey(VK_LEFT);
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(cb.CaretIndex(), 2u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, LeftAtStartIsConsumed) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"a");
    KeyEventArgs home = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(home);
    KeyEventArgs e = MakeKey(VK_LEFT);
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(cb.CaretIndex(), 0u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, RightMovesCaretRight) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"abc");
    KeyEventArgs home = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(home);
    KeyEventArgs e = MakeKey(VK_RIGHT);
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(cb.CaretIndex(), 1u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, RightAtEndConsumedSoDropdownNotOpened) {
    // Right at end must be consumed so the popup does not open inadvertently
    // when the user is just navigating to the end of the typed text.
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"a");  // caret at 1
    KeyEventArgs e = MakeKey(VK_RIGHT);
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(cb.CaretIndex(), 1u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, HomeMovesCaretToStart) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"abc");
    KeyEventArgs e = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(cb.CaretIndex(), 0u);
    EXPECT_TRUE(e.handled);
}

TEST(ComboBoxEditable, EndMovesCaretToEnd) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"abc");
    KeyEventArgs home = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(home);
    KeyEventArgs e = MakeKey(VK_END);
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(cb.CaretIndex(), 3u);
    EXPECT_TRUE(e.handled);
}

// --- TextChanged event -------------------------------------------------------

TEST(ComboBoxEditable, TextChangedRaisedOnTyping) {
    ComboBox cb;
    cb.SetEditable(true);
    std::pair<int, std::wstring> state{0, {}};
    auto sub = cb.TextChanged().Subscribe(&state, CountTextChanged);
    cb.OnTextInput(L'x');
    EXPECT_EQ(state.first, 1);
    EXPECT_TRUE(state.second == L"x");
}

TEST(ComboBoxEditable, TextChangedRaisedOnBackspace) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"ab");
    std::pair<int, std::wstring> state{0, {}};
    auto sub = cb.TextChanged().Subscribe(&state, CountTextChanged);
    KeyEventArgs back = MakeKey(VK_BACK);
    cb.OnKeyDownRouted(back);
    EXPECT_EQ(state.first, 1);
    EXPECT_TRUE(state.second == L"a");
}

TEST(ComboBoxEditable, TextChangedRaisedOnDelete) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"ab");
    KeyEventArgs home = MakeKey(VK_HOME);
    cb.OnKeyDownRouted(home);
    std::pair<int, std::wstring> state{0, {}};
    auto sub = cb.TextChanged().Subscribe(&state, CountTextChanged);
    KeyEventArgs del = MakeKey(VK_DELETE);
    cb.OnKeyDownRouted(del);
    EXPECT_EQ(state.first, 1);
    EXPECT_TRUE(state.second == L"b");
}

TEST(ComboBoxEditable, TextChangedNotRaisedBySetText) {
    ComboBox cb;
    cb.SetEditable(true);
    std::pair<int, std::wstring> state{0, {}};
    auto sub = cb.TextChanged().Subscribe(&state, CountTextChanged);
    cb.SetText(L"programmatic");
    EXPECT_EQ(state.first, 0);
}

TEST(ComboBoxEditable, TextChangedNotRaisedByCaretMove) {
    ComboBox cb;
    cb.SetEditable(true);
    cb.SetText(L"ab");
    std::pair<int, std::wstring> state{0, {}};
    auto sub = cb.TextChanged().Subscribe(&state, CountTextChanged);
    KeyEventArgs left = MakeKey(VK_LEFT);
    cb.OnKeyDownRouted(left);
    EXPECT_EQ(state.first, 0);
}

// --- Blink / cursor ----------------------------------------------------------

TEST(ComboBoxEditable, WantsBlinkWhenEditable) {
    ComboBox cb;
    cb.SetEditable(true);
    EXPECT_TRUE(cb.WantsBlink());
}

TEST(ComboBoxEditable, NoBlinkWhenReadonly) {
    ComboBox cb;
    cb.SetEditable(false);
    EXPECT_FALSE(cb.WantsBlink());
}

TEST(ComboBoxEditable, CursorIsIBeamWhenEditable) {
    ComboBox cb;
    cb.SetEditable(true);
    EXPECT_TRUE(cb.Cursor() != nullptr);
}

TEST(ComboBoxEditable, CursorIsNullWhenReadonly) {
    ComboBox cb;
    cb.SetEditable(false);
    EXPECT_EQ(cb.Cursor(), (HCURSOR)nullptr);
}

// --- Picking an item from the dropdown must update the editable text ----------
//
// THE BUG. In editable mode HeaderText() returns text_ (the typed buffer), but
// SetSelectedIndex only moved selectedIndex_. So choosing an item from the popup
// changed the selection while the box kept showing whatever was typed -- to the user
// the dropdown looked like it did nothing at all. Read-only mode was unaffected,
// because there HeaderText() reads SelectedItem() directly.
//
// The fix syncs text_ (and the caret) whenever the selection moves while editable.
// These tests drive the PUBLIC selection API, which is what the popup's item-click
// path funnels into, so they cover the reported gesture without needing a real popup.
TEST(ComboBoxEditable, SelectingAnItemUpdatesTheEditableText) {
    ComboBox cb;
    cb.SetItems({L"Shenzhen", L"Shanghai", L"Beijing"});
    cb.SetEditable(true);
    cb.SetText(L"Shen");            // user typed a partial filter

    cb.SetSelectedIndex(2);         // then picked "Beijing" from the list
    EXPECT_TRUE(cb.Text() == L"Beijing");
    EXPECT_EQ(cb.CaretIndex(), 7u); // caret lands at the end, ready to keep typing
}

TEST(ComboBoxEditable, SelectionSyncWorksRepeatedlyNotJustOnce) {
    ComboBox cb;
    cb.SetItems({L"Shenzhen", L"Shanghai", L"Beijing"});
    cb.SetEditable(true);

    cb.SetSelectedIndex(0);
    EXPECT_TRUE(cb.Text() == L"Shenzhen");
    cb.SetSelectedIndex(1);
    EXPECT_TRUE(cb.Text() == L"Shanghai");
    cb.SetSelectedIndex(2);
    EXPECT_TRUE(cb.Text() == L"Beijing");
}

// Typing must still win afterwards: the sync happens on selection, and must not
// pin the box to the selected item or the control would stop being editable.
TEST(ComboBoxEditable, TypingAfterSelectingStillOverridesTheText) {
    ComboBox cb;
    cb.SetItems({L"Shenzhen", L"Beijing"});
    cb.SetEditable(true);
    cb.SetSelectedIndex(1);
    EXPECT_TRUE(cb.Text() == L"Beijing");

    cb.SetText(L"Guangzhou");        // user keeps editing
    EXPECT_TRUE(cb.Text() == L"Guangzhou");
    EXPECT_EQ(cb.SelectedIndex(), 1);   // selection is untouched by typing
}

// Clearing the selection must not wipe text the user is in the middle of typing.
TEST(ComboBoxEditable, DeselectingDoesNotClearTypedText) {
    ComboBox cb;
    cb.SetItems({L"Shenzhen", L"Beijing"});
    cb.SetEditable(true);
    cb.SetText(L"Guang");

    cb.SetSelectedIndex(-1);         // "no selection" is not an item to copy in
    EXPECT_TRUE(cb.Text() == L"Guang");
}

// Read-only mode must be completely unaffected by the fix.
TEST(ComboBoxEditable, ReadonlySelectionStillReadsFromTheItemList) {
    ComboBox cb;
    cb.SetItems({L"Shenzhen", L"Beijing"});
    cb.SetSelectedIndex(1);
    EXPECT_TRUE(cb.Text() == L"Beijing");
    cb.SetSelectedIndex(0);
    EXPECT_TRUE(cb.Text() == L"Shenzhen");
}
