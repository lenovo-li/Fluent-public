// WordBoundaryTests.cpp — double-click word selection and Ctrl+Arrow navigation.
//
// Pure arithmetic over strings, so everything here is exact rather than approximate.
// The interesting cases are all BOUNDARIES — this is the kind of function whose bugs
// live entirely at the ends of the string, on class transitions, and around newlines,
// so those get the coverage rather than the happy path in the middle of a word.
#include "../framework/Test.h"
#include "../../FluentUI/text/WordBoundary.h"
#include <string>

using namespace fluent;

// ---------------------------------------------------------------------------
// ClassifyChar
// ---------------------------------------------------------------------------

TEST(WordBoundary, ClassifiesAsciiLettersAndDigitsAsWord) {
    EXPECT_TRUE(ClassifyChar(L'a') == CharClass::Word);
    EXPECT_TRUE(ClassifyChar(L'Z') == CharClass::Word);
    EXPECT_TRUE(ClassifyChar(L'7') == CharClass::Word);
}

TEST(WordBoundary, ClassifiesUnderscoreAsWord) {
    // Deliberate: identifiers are one word, so double-clicking foo_bar selects it whole.
    EXPECT_TRUE(ClassifyChar(L'_') == CharClass::Word);
}

TEST(WordBoundary, ClassifiesWhitespaceAsSpace) {
    EXPECT_TRUE(ClassifyChar(L' ') == CharClass::Space);
    EXPECT_TRUE(ClassifyChar(L'\t') == CharClass::Space);
    EXPECT_TRUE(ClassifyChar(L'\n') == CharClass::Space);
    EXPECT_TRUE(ClassifyChar(L'\r') == CharClass::Space);
}

TEST(WordBoundary, ClassifiesPunctuationAsPunct) {
    EXPECT_TRUE(ClassifyChar(L'.') == CharClass::Punct);
    EXPECT_TRUE(ClassifyChar(L'(') == CharClass::Punct);
    EXPECT_TRUE(ClassifyChar(L'=') == CharClass::Punct);
}

TEST(WordBoundary, ClassifiesNonAsciiAsWord) {
    // CJK and accented Latin both land here. The accented case is the one that would
    // silently break words if the rule were "ASCII letters only": é inside "café"
    // would be a Punct boundary, so double-clicking would select only "caf".
    EXPECT_TRUE(ClassifyChar(L'你') == CharClass::Word);   // 你
    EXPECT_TRUE(ClassifyChar(L'é') == CharClass::Word);   // é
    EXPECT_TRUE(ClassifyChar(L'Ж') == CharClass::Word);   // Ж
}

// ---------------------------------------------------------------------------
// WordRangeAt — the double-click rule
// ---------------------------------------------------------------------------

TEST(WordBoundary, SelectsWholeWordFromTheMiddle) {
    const std::wstring t = L"hello world";
    auto [s, e] = WordRangeAt(t, 3);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{5});
}

TEST(WordBoundary, SelectsIdentifierWithUnderscoreWhole) {
    const std::wstring t = L"foo_bar baz";
    auto [s, e] = WordRangeAt(t, 4);   // inside "bar" half of foo_bar
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{7});
}

TEST(WordBoundary, StopsAtPunctuation) {
    // "foo.bar" clicked on foo must select only foo — the '.' is a boundary.
    const std::wstring t = L"foo.bar";
    auto [s, e] = WordRangeAt(t, 1);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{3});
}

TEST(WordBoundary, SelectsRunOfPunctuation) {
    // Clicking on punctuation selects the punctuation run, not the neighbouring word.
    const std::wstring t = L"a+++b";
    auto [s, e] = WordRangeAt(t, 2);
    EXPECT_EQ(s, size_t{1});
    EXPECT_EQ(e, size_t{4});
}

TEST(WordBoundary, SelectsRunOfSpaces) {
    const std::wstring t = L"a    b";
    auto [s, e] = WordRangeAt(t, 2);
    EXPECT_EQ(s, size_t{1});
    EXPECT_EQ(e, size_t{5});
}

TEST(WordBoundary, SelectsCjkRunWhole) {
    // Documented limitation, asserted so a future change to ClassifyChar cannot alter
    // this behaviour silently: CJK is one run, not segmented into words.
    const std::wstring t = L"你好世界";   // 你好世界
    auto [s, e] = WordRangeAt(t, 2);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{4});
}

TEST(WordBoundary, WordRangeAtStartOfText) {
    const std::wstring t = L"abc def";
    auto [s, e] = WordRangeAt(t, 0);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{3});
}

TEST(WordBoundary, WordRangeAtEndCaretSelectsLastRun) {
    // A caret one past the last character (clicking in the blank after the text) has no
    // character AT the index; it must select the run that ENDS there.
    const std::wstring t = L"abc def";
    auto [s, e] = WordRangeAt(t, t.size());
    EXPECT_EQ(s, size_t{4});
    EXPECT_EQ(e, size_t{7});
}

TEST(WordBoundary, WordRangeOnEmptyTextIsEmpty) {
    auto [s, e] = WordRangeAt(L"", 0);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{0});
}

TEST(WordBoundary, WordRangeClampsOutOfRangeIndex) {
    const std::wstring t = L"abc";
    auto [s, e] = WordRangeAt(t, 999);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{3});
}

TEST(WordBoundary, WordRangeNeverCrossesNewlineForward) {
    // "ab\ncd": clicking in "ab" must not pull in "cd". Without the newline guard the
    // two lines' words are separated only by a Space run, so a Space-class click would
    // span both — a double-click that highlights across a line break.
    const std::wstring t = L"ab\ncd";
    auto [s, e] = WordRangeAt(t, 1);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{2});
}

TEST(WordBoundary, WordRangeNeverCrossesNewlineBackward) {
    const std::wstring t = L"ab\ncd";
    auto [s, e] = WordRangeAt(t, 4);   // inside "cd"
    EXPECT_EQ(s, size_t{3});
    EXPECT_EQ(e, size_t{5});
}

TEST(WordBoundary, WordRangeOnNewlineItselfIsEmpty) {
    // Clicking exactly on the line break selects nothing. Selecting the '\n' would draw
    // a highlight hanging past the end of the line; picking one of the two adjacent
    // words would be arbitrary.
    const std::wstring t = L"ab\ncd";
    auto [s, e] = WordRangeAt(t, 2);
    EXPECT_EQ(s, e);
}

TEST(WordBoundary, SpaceRunStopsAtNewline) {
    // Spaces on both sides of a newline are ONE Space run by class, but must not be
    // selected as one.
    const std::wstring t = L"a  \n  b";
    auto [s, e] = WordRangeAt(t, 1);   // first space
    EXPECT_EQ(s, size_t{1});
    EXPECT_EQ(e, size_t{3});           // stops before the '\n'
}

// ---------------------------------------------------------------------------
// NextWordBoundary / PrevWordBoundary — Ctrl+Arrow
// ---------------------------------------------------------------------------

TEST(WordBoundary, NextWordLandsOnFollowingWordStart) {
    // Windows edit-control semantics: skip the rest of this run, then skip whitespace,
    // so repeated Ctrl+Right walks word STARTS.
    const std::wstring t = L"alpha beta gamma";
    EXPECT_EQ(NextWordBoundary(t, 0), size_t{6});    // start of "beta"
    EXPECT_EQ(NextWordBoundary(t, 6), size_t{11});   // start of "gamma"
}

TEST(WordBoundary, NextWordFromMidWord) {
    const std::wstring t = L"alpha beta";
    EXPECT_EQ(NextWordBoundary(t, 2), size_t{6});
}

TEST(WordBoundary, NextWordAtEndStaysAtEnd) {
    const std::wstring t = L"abc";
    EXPECT_EQ(NextWordBoundary(t, 3), size_t{3});
    EXPECT_EQ(NextWordBoundary(t, 99), size_t{3});
}

TEST(WordBoundary, NextWordCrossesNewline) {
    // Unlike WordRangeAt, Ctrl+Right must reach the next line.
    const std::wstring t = L"ab\ncd";
    EXPECT_EQ(NextWordBoundary(t, 0), size_t{3});   // start of "cd"
}

TEST(WordBoundary, PrevWordLandsOnWordStart) {
    const std::wstring t = L"alpha beta gamma";
    EXPECT_EQ(PrevWordBoundary(t, 16), size_t{11});  // from end -> start of "gamma"
    EXPECT_EQ(PrevWordBoundary(t, 11), size_t{6});   // -> start of "beta"
    EXPECT_EQ(PrevWordBoundary(t, 6), size_t{0});    // -> start of "alpha"
}

TEST(WordBoundary, PrevWordFromMidWordGoesToItsStart) {
    const std::wstring t = L"alpha beta";
    EXPECT_EQ(PrevWordBoundary(t, 8), size_t{6});
}

TEST(WordBoundary, PrevWordAtStartStaysAtStart) {
    EXPECT_EQ(PrevWordBoundary(L"abc", 0), size_t{0});
}

TEST(WordBoundary, PrevWordSkipsTrailingWhitespaceFirst) {
    // Caret after the spaces: must reach "alpha"'s start, not stop in the gap.
    const std::wstring t = L"alpha   ";
    EXPECT_EQ(PrevWordBoundary(t, 8), size_t{0});
}

TEST(WordBoundary, NextAndPrevAgreeOnWordStarts) {
    // The asymmetry inside the two functions exists so that both come to rest on the
    // same positions. If they drifted, Ctrl+Right then Ctrl+Left would not return the
    // caret to where it began.
    const std::wstring t = L"one two three";
    const size_t afterOne = NextWordBoundary(t, 0);    // start of "two"
    EXPECT_EQ(PrevWordBoundary(t, afterOne), size_t{0});
}

// ---------------------------------------------------------------------------
// LogicalLineRangeAt — triple-click
// ---------------------------------------------------------------------------

TEST(WordBoundary, LogicalLineRangeExcludesTheNewline) {
    const std::wstring t = L"first\nsecond\nthird";
    auto [s, e] = LogicalLineRangeAt(t, 7);   // inside "second"
    EXPECT_EQ(s, size_t{6});
    EXPECT_EQ(e, size_t{12});                 // 'd' of second, not the '\n'
}

TEST(WordBoundary, LogicalLineRangeOnFirstLine) {
    const std::wstring t = L"first\nsecond";
    auto [s, e] = LogicalLineRangeAt(t, 2);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{5});
}

TEST(WordBoundary, LogicalLineRangeOnLastLineWithoutTrailingNewline) {
    const std::wstring t = L"first\nlast";
    auto [s, e] = LogicalLineRangeAt(t, 8);
    EXPECT_EQ(s, size_t{6});
    EXPECT_EQ(e, size_t{10});
}

TEST(WordBoundary, LogicalLineRangeOnEmptyLine) {
    // A blank line between two paragraphs: the range is empty but correctly placed, so a
    // triple-click there selects nothing rather than swallowing a neighbouring line.
    const std::wstring t = L"a\n\nb";
    auto [s, e] = LogicalLineRangeAt(t, 2);
    EXPECT_EQ(s, size_t{2});
    EXPECT_EQ(e, size_t{2});
}

TEST(WordBoundary, LogicalLineRangeOnEmptyText) {
    auto [s, e] = LogicalLineRangeAt(L"", 0);
    EXPECT_EQ(s, size_t{0});
    EXPECT_EQ(e, size_t{0});
}
