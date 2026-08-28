// UndoStack.h — undo/redo support for text editing.
//
// Tracks editing operations (insert, delete, replace) with their inverse actions
// and caret positions, so Ctrl+Z/Y work. Consecutive character insertions merge
// into a single undoable unit; a paste or a delete break the run. This matches
// what users expect: typing a word is one undo, but paste+type is two.
//
// The stack is bounded (default 100 operations) to cap memory. Oldest entries
// fall off when the limit is reached. An undo reverts one operation and pushes
// its inverse onto the redo stack; any new edit clears redo.
//
// Integration: the editor calls `RecordInsert` / `RecordDelete` / `RecordReplace`
// after each mutation, then `Undo()` / `Redo()` on Ctrl+Z/Y. The undo/redo methods
// return a struct describing what to do (replace text at [start, start+len) with
// `newText`, move caret to `caretAfter`). The editor applies it and must NOT call
// `Record*` for that change — the stack already knows about it.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fluent {

struct UndoAction {
    enum class Type { Insert, Delete, Replace };
    Type type;
    uint32_t start;             // start index of the affected range
    std::wstring text;          // inserted/deleted text (or new text for Replace)
    std::wstring oldText;       // only for Replace: the text being replaced
    uint32_t caretBefore;       // caret position before this action
    uint32_t caretAfter;        // caret position after this action
    bool mergeable = false;     // true for consecutive character insertions
};

struct UndoResult {
    uint32_t start;             // replace text at [start, start+len)
    uint32_t len;               // length of text to replace
    std::wstring newText;       // replacement text
    uint32_t caretAfter;        // where to place the caret
};

class UndoStack {
public:
    explicit UndoStack(size_t maxSize = 100) : maxSize_(maxSize) {}

    // Record a character insertion at `start`. Consecutive single-char inserts merge.
    void RecordInsert(uint32_t start, wchar_t ch, uint32_t caretBefore, uint32_t caretAfter);

    // Record a string insertion at `start` (paste, or multi-char input). Never merges.
    void RecordInsert(uint32_t start, const std::wstring& text,
                      uint32_t caretBefore, uint32_t caretAfter);

    // Record a deletion of `len` characters at `start`.
    void RecordDelete(uint32_t start, const std::wstring& deletedText,
                      uint32_t caretBefore, uint32_t caretAfter);

    // Record a replacement (selection delete + insert, as one atomic unit).
    void RecordReplace(uint32_t start, const std::wstring& oldText,
                       const std::wstring& newText,
                       uint32_t caretBefore, uint32_t caretAfter);

    // Undo the most recent action. Returns true and fills `result` if successful,
    // false if the undo stack is empty. The caller applies the result and must NOT
    // call Record* for that change.
    bool Undo(UndoResult& result);

    // Redo the most recently undone action. Returns true and fills `result` if
    // successful, false if the redo stack is empty.
    bool Redo(UndoResult& result);

    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    void Clear() { undoStack_.clear(); redoStack_.clear(); }

private:
    void PushUndo(UndoAction action);

    std::vector<UndoAction> undoStack_;
    std::vector<UndoAction> redoStack_;
    size_t maxSize_;
};

} // namespace fluent
