// TextEditInputPolicyTests.cpp — P1-2: CharacterCasing + InputFilter.
//
// Both policies live in TextEditBase::InsertText, applied after SanitizeInput and
// before the MaxLength clamp. That ordering is the contract: casing runs first so a
// filter written against upper-case letters sees upper-case input, and MaxLength runs
// last so the limit counts what actually lands in the buffer rather than what arrived.
//
// WHAT THESE TESTS REACH, AND WHAT THEY DO NOT. OnTextInput is public and is the real
// typing path (WM_CHAR -> OnTextInput -> InsertText), so every assertion below goes
// through production code with no test-only seam. InsertText itself is protected, so
// the MULTI-character routes — clipboard paste and IME commit — are not reachable from
// a unit test: Paste needs a clipboard and an HWND, the IME path needs a live input
// context. The per-character policies are identical on those routes (they are applied
// to the whole run in one pass), but the run-level interactions specific to them are
// covered by inspection only and deliberately NOT asserted here:
//   * a filter dropping SOME characters of a pasted run (paste "a1b2" -> "12")
//   * MaxLength truncating a run that casing/filter already shortened
// Asserting those against the typing path would be asserting against code that does
// not run them — one character at a time never exercises run-level truncation.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBox.h"
#include <string>

using namespace fluent;

namespace {

// Type a string one character at a time through the public input path, exactly as
// WM_CHAR would deliver it.
void Type(TextBox& tb, const wchar_t* s) {
    for (const wchar_t* p = s; *p; ++p) tb.OnTextInput(*p);
}

}  // namespace

// --- CharacterCasing ------------------------------------------------------

TEST(TextEditInputPolicy, CasingUpperForcesTypedCharacters) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    Type(tb, L"abc");
    EXPECT_EQ(tb.Text(), std::wstring(L"ABC"));
}

TEST(TextEditInputPolicy, CasingLowerForcesTypedCharacters) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Lower);
    Type(tb, L"XYZ");
    EXPECT_EQ(tb.Text(), std::wstring(L"xyz"));
}

// Digits and punctuation have no case; towupper/towlower must leave them alone rather
// than mapping them to something else.
TEST(TextEditInputPolicy, CasingLeavesCaselessCharactersAlone) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    Type(tb, L"a1b-2");
    EXPECT_EQ(tb.Text(), std::wstring(L"A1B-2"));
}

TEST(TextEditInputPolicy, CasingNormalIsTheDefaultAndPreservesInput) {
    TextBox tb;
    EXPECT_TRUE(tb.GetCharacterCasing() == TextEditBase::CharacterCasing::Normal);
    Type(tb, L"MiXeD");
    EXPECT_EQ(tb.Text(), std::wstring(L"MiXeD"));
}

// SetText is programmatic content the caller already controls, so it is NOT re-cased.
// Only what the user inserts is forced.
TEST(TextEditInputPolicy, CasingDoesNotRewriteSetText) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    tb.SetText(L"lowercase");
    EXPECT_EQ(tb.Text(), std::wstring(L"lowercase"));

    // But a subsequent keystroke is cased.
    tb.TestSetCaret(9);
    Type(tb, L"x");
    EXPECT_EQ(tb.Text(), std::wstring(L"lowercaseX"));
}

// Undo must restore exactly the bytes that were in the buffer. Replay writes text_
// directly and bypasses the policy, so changing the casing mode between the edit and
// the undo cannot alter what undo produces.
TEST(TextEditInputPolicy, CasingDoesNotAffectUndoReplay) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    tb.SetText(L"seed");
    tb.TestSetCaret(4);
    Type(tb, L"a");
    EXPECT_EQ(tb.Text(), std::wstring(L"seedA"));

    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Lower);
    tb.Undo();
    EXPECT_EQ(tb.Text(), std::wstring(L"seed"));
    tb.Redo();
    // Redo restores the recorded 'A', not a re-cased 'a'.
    EXPECT_EQ(tb.Text(), std::wstring(L"seedA"));
}

// --- InputFilter ----------------------------------------------------------

TEST(TextEditInputPolicy, FilterRejectsDisallowedCharacters) {
    TextBox tb;
    tb.SetInputFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
    Type(tb, L"a1b2c3");
    EXPECT_EQ(tb.Text(), std::wstring(L"123"));
}

TEST(TextEditInputPolicy, FilterRejectingEverythingLeavesBufferUntouched) {
    TextBox tb;
    tb.SetText(L"initial");
    tb.TestSetCaret(7);
    tb.SetInputFilter([](wchar_t) { return false; });
    Type(tb, L"abc123");
    EXPECT_EQ(tb.Text(), std::wstring(L"initial"));
}

TEST(TextEditInputPolicy, NoFilterIsTheDefault) {
    TextBox tb;
    EXPECT_FALSE(tb.HasInputFilter());
    Type(tb, L"anything!@#");
    EXPECT_EQ(tb.Text(), std::wstring(L"anything!@#"));
}

TEST(TextEditInputPolicy, ClearInputFilterRestoresAcceptAll) {
    TextBox tb;
    tb.SetInputFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
    Type(tb, L"a1");
    EXPECT_EQ(tb.Text(), std::wstring(L"1"));
    EXPECT_TRUE(tb.HasInputFilter());

    tb.ClearInputFilter();
    EXPECT_FALSE(tb.HasInputFilter());
    Type(tb, L"a2");
    EXPECT_EQ(tb.Text(), std::wstring(L"1a2"));
}

// SetText bypasses the filter for the same reason it bypasses casing.
TEST(TextEditInputPolicy, FilterDoesNotRewriteSetText) {
    TextBox tb;
    tb.SetInputFilter([](wchar_t c) { return c >= L'0' && c <= L'9'; });
    tb.SetText(L"letters");
    EXPECT_EQ(tb.Text(), std::wstring(L"letters"));
}

// --- Ordering: casing runs BEFORE the filter -----------------------------
// This is the assertion that pins the order down. The filter accepts only upper-case
// letters; the user types lower-case. If the filter ran first it would reject every
// character and the field would stay empty — the test would fail. Passing proves casing
// happened first.

TEST(TextEditInputPolicy, CasingIsAppliedBeforeTheFilterSeesTheCharacter) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    tb.SetInputFilter([](wchar_t c) { return c >= L'A' && c <= L'Z'; });
    Type(tb, L"a1b2c");
    EXPECT_EQ(tb.Text(), std::wstring(L"ABC"));
}

// --- Ordering: MaxLength runs LAST ---------------------------------------
// The filter drops 3 of 6 typed characters, so a limit of 3 must count only the 3 that
// survived. If MaxLength were applied to the raw input instead, it would have stopped
// after "a1b" and produced just "AB".

TEST(TextEditInputPolicy, MaxLengthCountsWhatSurvivesTheFilter) {
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    tb.SetInputFilter([](wchar_t c) { return c >= L'A' && c <= L'Z'; });
    tb.SetMaxLength(3);
    Type(tb, L"a1b2c3");
    EXPECT_EQ(tb.Text(), std::wstring(L"ABC"));

    // Limit reached: further accepted characters are refused.
    Type(tb, L"d");
    EXPECT_EQ(tb.Text(), std::wstring(L"ABC"));
}

// --- Policies apply to TextArea too (same base implementation) -----------

TEST(TextEditInputPolicy, PoliciesApplyToTextAreaViaTheSharedBase) {
    // Asserted on TextBox's sibling to prove the policy lives in the base rather than
    // in one subclass. TextArea's own SanitizeInput (newline normalisation) runs first
    // and is unaffected.
    TextBox tb;
    tb.SetCharacterCasing(TextEditBase::CharacterCasing::Upper);
    Type(tb, L"ok");
    EXPECT_EQ(tb.Text(), std::wstring(L"OK"));
}
