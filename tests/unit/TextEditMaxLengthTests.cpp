// TextEditMaxLengthTests.cpp — MaxLength enforcement across TextBox/TextArea.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/TextArea.h"

using namespace fluent;

// --- TextBox MaxLength ---

TEST(TextEditMaxLength, TextBox_RejectsExcessTyping) {
    TextBox tb;
    tb.SetMaxLength(5);
    tb.SetText(L"abc");
    tb.TestSetCaret(3);  // end
    tb.OnTextInput(L'd');
    tb.OnTextInput(L'e');
    EXPECT_EQ(tb.Text(), L"abcde");
    tb.OnTextInput(L'f');  // rejected
    EXPECT_EQ(tb.Text(), L"abcde");
}

TEST(TextEditMaxLength, TextBox_TruncatesPasteToRoom) {
    TextBox tb;
    tb.SetMaxLength(10);
    tb.SetText(L"hello");
    tb.TestSetCaret(5);  // end
    // Room for 5 more, paste 8 → takes first 5
    for (wchar_t c : std::wstring(L"12345678"))
        tb.OnTextInput(c);
    EXPECT_EQ(tb.Text(), L"hello12345");
}

TEST(TextEditMaxLength, TextBox_OverwriteSelectionUsesRoomFreed) {
    TextBox tb;
    tb.SetMaxLength(6);
    tb.SetText(L"abcdef");  // full
    tb.TestSetCaret(3);
    tb.SelectAllForTest();  // select all 6
    // Overwriting all 6 frees 6, so typing 6 new chars is allowed
    for (wchar_t c : std::wstring(L"xyz123"))
        tb.OnTextInput(c);
    EXPECT_EQ(tb.Text(), L"xyz123");
}

TEST(TextEditMaxLength, TextBox_DoesNotClampSetText) {
    TextBox tb;
    tb.SetMaxLength(5);
    tb.SetText(L"initial content longer than 5");
    // SetText is NOT clamped (sets initial content)
    EXPECT_EQ(tb.Text(), L"initial content longer than 5");
    // But typing is
    tb.TestSetCaret(static_cast<UINT32>(tb.Text().size()));
    tb.OnTextInput(L'x');
    EXPECT_EQ(tb.Text(), L"initial content longer than 5");  // unchanged
}

TEST(TextEditMaxLength, TextBox_ZeroMeansUnlimited) {
    TextBox tb;
    tb.SetMaxLength(0);  // default
    tb.SetText(L"");
    for (int i = 0; i < 1000; ++i)
        tb.OnTextInput(L'a');
    EXPECT_EQ(tb.Text().size(), 1000u);
}

// NOTE on surrogate pairs: the truncation guard in InsertText (drop a trailing high
// surrogate after resize) only fires on MULTI-unit insertions — paste and IME commit,
// which reach InsertText directly. The typing path cannot exercise it: OnTextInput
// delivers one wchar_t at a time, so a pair arrives as two separate single-unit
// insertions and is never split by the resize. Reaching the guard from a test needs a
// clipboard (Paste) or a live IME, neither of which is available headless, so it is
// covered by inspection only — deliberately NOT asserted here rather than asserted
// against a path that does not run it.

// --- TextArea MaxLength (same logic, different subclass) ---

TEST(TextEditMaxLength, TextArea_RejectsExcessTyping) {
    TextArea ta;
    ta.SetMaxLength(5);
    ta.SetText(L"abc");
    ta.TestSetCaret(3);
    ta.OnTextInput(L'd');
    ta.OnTextInput(L'e');
    EXPECT_EQ(ta.Text(), L"abcde");
    ta.OnTextInput(L'f');
    EXPECT_EQ(ta.Text(), L"abcde");
}

TEST(TextEditMaxLength, TextArea_TruncatesPasteToRoom) {
    TextArea ta;
    ta.SetMaxLength(10);
    ta.SetText(L"hello");
    ta.TestSetCaret(5);
    for (wchar_t c : std::wstring(L"12345678"))
        ta.OnTextInput(c);
    EXPECT_EQ(ta.Text(), L"hello12345");
}
