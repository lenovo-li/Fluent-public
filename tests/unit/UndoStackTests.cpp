// UndoStackTests.cpp — unit tests for the text undo/redo stack.

#include "../framework/Test.h"
#include "../../FluentUI/text/UndoStack.h"

using namespace fluent;

// --- Empty state -----------------------------------------------------------

TEST(UndoStack, EmptyStackCannotUndoOrRedo) {
    UndoStack stack;
    EXPECT_FALSE(stack.CanUndo());
    EXPECT_FALSE(stack.CanRedo());
    UndoResult r;
    EXPECT_FALSE(stack.Undo(r));
    EXPECT_FALSE(stack.Redo(r));
}

// --- Single insert ---------------------------------------------------------

TEST(UndoStack, SingleCharInsertIsUndoable) {
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    EXPECT_TRUE(stack.CanUndo());
    EXPECT_FALSE(stack.CanRedo());

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.len, 1u);            // remove the one character
    EXPECT_TRUE(r.newText.empty());
    EXPECT_EQ(r.caretAfter, 0u);     // caret back to where typing began
}

// --- Merging ---------------------------------------------------------------

TEST(UndoStack, ConsecutiveCharsMergeIntoOneUnit) {
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    stack.RecordInsert(1, L'b', 1, 2);
    stack.RecordInsert(2, L'c', 2, 3);

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.len, 3u);            // "abc" removed in one step
    EXPECT_EQ(r.caretAfter, 0u);
    EXPECT_FALSE(stack.CanUndo());    // and it really was ONE entry
}

TEST(UndoStack, NonAdjacentInsertStartsANewUnit) {
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    stack.RecordInsert(5, L'b', 5, 6);   // caret jumped: must not merge

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 5u);
    EXPECT_EQ(r.len, 1u);
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.len, 1u);
}

TEST(UndoStack, StringInsertNeverMerges) {
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    stack.RecordInsert(1, std::wstring(L"bc"), 1, 3);  // a paste
    stack.RecordInsert(3, L'd', 3, 4);

    // Three distinct units: the paste breaks the typing run on both sides.
    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 3u);
    EXPECT_EQ(r.len, 1u);
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 1u);
    EXPECT_EQ(r.len, 2u);
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.len, 1u);
}

TEST(UndoStack, DeleteBreaksTheTypingRun) {
    // Type "a", delete something, type "b": three units, not one merged insert.
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    stack.RecordDelete(1, L"x", 2, 1);
    stack.RecordInsert(1, L'b', 1, 2);

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));   // undo the 'b'
    EXPECT_EQ(r.len, 1u);
    EXPECT_TRUE(stack.Undo(r));   // undo the delete -> re-inserts "x"
    EXPECT_EQ(r.newText, std::wstring(L"x"));
    EXPECT_TRUE(stack.Undo(r));   // undo the 'a'
    EXPECT_EQ(r.start, 0u);
    EXPECT_FALSE(stack.CanUndo());
}

// --- Delete / replace ------------------------------------------------------

TEST(UndoStack, UndoDeleteReinsertsTextAndRestoresCaret) {
    UndoStack stack;
    stack.RecordDelete(5, L"text", 9, 5);

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 5u);
    EXPECT_EQ(r.len, 0u);                       // insert, don't remove
    EXPECT_EQ(r.newText, std::wstring(L"text"));
    EXPECT_EQ(r.caretAfter, 9u);                // caret as it was before deleting
}

TEST(UndoStack, UndoReplaceRestoresTheOldText) {
    UndoStack stack;
    stack.RecordReplace(10, L"old", L"new", 13, 13);

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));
    EXPECT_EQ(r.start, 10u);
    EXPECT_EQ(r.len, 3u);                      // length of "new"
    EXPECT_EQ(r.newText, std::wstring(L"old"));
}

TEST(UndoStack, ReplaceWithDifferentLengthsUsesTheRightSpans) {
    // The two spans differ, so a symmetric implementation passes the equal-length
    // case above and still corrupts this one.
    UndoStack stack;
    stack.RecordReplace(0, L"before", L"after", 6, 5);

    UndoResult undo;
    EXPECT_TRUE(stack.Undo(undo));
    EXPECT_EQ(undo.len, 5u);                       // remove "after" (5)
    EXPECT_EQ(undo.newText, std::wstring(L"before"));

    UndoResult redo;
    EXPECT_TRUE(stack.Redo(redo));
    EXPECT_EQ(redo.len, 6u);                       // remove "before" (6)
    EXPECT_EQ(redo.newText, std::wstring(L"after"));
}

// --- Redo ------------------------------------------------------------------

TEST(UndoStack, RedoReappliesTheUndoneInsert) {
    UndoStack stack;
    stack.RecordInsert(0, std::wstring(L"abc"), 0, 3);

    UndoResult r;
    stack.Undo(r);
    EXPECT_TRUE(stack.CanRedo());
    EXPECT_FALSE(stack.CanUndo());

    EXPECT_TRUE(stack.Redo(r));
    EXPECT_EQ(r.start, 0u);
    EXPECT_EQ(r.len, 0u);
    EXPECT_EQ(r.newText, std::wstring(L"abc"));
    EXPECT_EQ(r.caretAfter, 3u);       // caret back at the end of the insert
    EXPECT_TRUE(stack.CanUndo());      // and the entry returned to the undo side
}

TEST(UndoStack, RedoDeleteRemovesAgain) {
    UndoStack stack;
    stack.RecordDelete(5, L"gone", 9, 5);

    UndoResult r;
    stack.Undo(r);
    EXPECT_TRUE(stack.Redo(r));
    EXPECT_EQ(r.start, 5u);
    EXPECT_EQ(r.len, 4u);
    EXPECT_TRUE(r.newText.empty());
}

TEST(UndoStack, UndoRedoCyclesRepeatedly) {
    // One undo/redo pair must not consume the entry — this is what breaks if the
    // redo path pushes through the redo-clearing PushUndo.
    UndoStack stack;
    stack.RecordInsert(0, std::wstring(L"xy"), 0, 2);

    UndoResult r;
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(stack.Undo(r));
        EXPECT_EQ(r.len, 2u);
        EXPECT_TRUE(stack.Redo(r));
        EXPECT_EQ(r.newText, std::wstring(L"xy"));
    }
}

TEST(UndoStack, NewEditAfterUndoClearsRedo) {
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    UndoResult r;
    stack.Undo(r);
    EXPECT_TRUE(stack.CanRedo());

    stack.RecordInsert(0, L'b', 0, 1);   // a fresh edit invalidates the redo branch
    EXPECT_FALSE(stack.CanRedo());
}

TEST(UndoStack, MergingIntoAnExistingEntryAlsoClearsRedo) {
    // The merge path returns early, so it needs its own redo-clear — otherwise a
    // keystroke that merges leaves a stale redo entry pointing at dead text.
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    stack.RecordInsert(1, L'b', 1, 2);
    UndoResult r;
    stack.Undo(r);
    EXPECT_TRUE(stack.CanRedo());

    stack.RecordInsert(0, L'c', 0, 1);
    EXPECT_FALSE(stack.CanRedo());
}

// --- Bounds / clearing -----------------------------------------------------

TEST(UndoStack, MaxSizeDropsTheOldestEntry) {
    UndoStack stack(3);
    // Non-adjacent positions so nothing merges and each is its own entry.
    stack.RecordInsert(0,  L'a', 0,  1);
    stack.RecordInsert(10, L'b', 10, 11);
    stack.RecordInsert(20, L'c', 20, 21);
    stack.RecordInsert(30, L'd', 30, 31);   // evicts the 'a'

    UndoResult r;
    EXPECT_TRUE(stack.Undo(r));   // d
    EXPECT_EQ(r.start, 30u);
    EXPECT_TRUE(stack.Undo(r));   // c
    EXPECT_EQ(r.start, 20u);
    EXPECT_TRUE(stack.Undo(r));   // b
    EXPECT_EQ(r.start, 10u);
    EXPECT_FALSE(stack.CanUndo()); // 'a' is gone
}

TEST(UndoStack, ClearEmptiesBothSides) {
    UndoStack stack;
    stack.RecordInsert(0, L'a', 0, 1);
    UndoResult r;
    stack.Undo(r);
    EXPECT_TRUE(stack.CanRedo());

    stack.Clear();
    EXPECT_FALSE(stack.CanUndo());
    EXPECT_FALSE(stack.CanRedo());
}
