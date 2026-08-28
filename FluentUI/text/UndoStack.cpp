// UndoStack.cpp
#include "UndoStack.h"
#include <algorithm>

namespace fluent {

void UndoStack::RecordInsert(uint32_t start, wchar_t ch,
                             uint32_t caretBefore, uint32_t caretAfter) {
    // Try to merge with the last action if it's a consecutive single-char insert.
    if (!undoStack_.empty()) {
        auto& last = undoStack_.back();
        if (last.mergeable && last.type == UndoAction::Type::Insert &&
            last.start + static_cast<uint32_t>(last.text.size()) == start) {
            // Consecutive insert at the expected position: merge.
            last.text.push_back(ch);
            last.caretAfter = caretAfter;
            redoStack_.clear();
            return;
        }
    }

    // Cannot merge: push a new action.
    UndoAction action;
    action.type = UndoAction::Type::Insert;
    action.start = start;
    action.text = std::wstring(1, ch);
    action.caretBefore = caretBefore;
    action.caretAfter = caretAfter;
    action.mergeable = true;
    PushUndo(std::move(action));
}

void UndoStack::RecordInsert(uint32_t start, const std::wstring& text,
                             uint32_t caretBefore, uint32_t caretAfter) {
    UndoAction action;
    action.type = UndoAction::Type::Insert;
    action.start = start;
    action.text = text;
    action.caretBefore = caretBefore;
    action.caretAfter = caretAfter;
    action.mergeable = false;  // multi-char inserts never merge
    PushUndo(std::move(action));
}

void UndoStack::RecordDelete(uint32_t start, const std::wstring& deletedText,
                             uint32_t caretBefore, uint32_t caretAfter) {
    UndoAction action;
    action.type = UndoAction::Type::Delete;
    action.start = start;
    action.text = deletedText;
    action.caretBefore = caretBefore;
    action.caretAfter = caretAfter;
    action.mergeable = false;
    PushUndo(std::move(action));
}

void UndoStack::RecordReplace(uint32_t start, const std::wstring& oldText,
                              const std::wstring& newText,
                              uint32_t caretBefore, uint32_t caretAfter) {
    UndoAction action;
    action.type = UndoAction::Type::Replace;
    action.start = start;
    action.text = newText;
    action.oldText = oldText;
    action.caretBefore = caretBefore;
    action.caretAfter = caretAfter;
    action.mergeable = false;
    PushUndo(std::move(action));
}

bool UndoStack::Undo(UndoResult& result) {
    if (undoStack_.empty()) return false;

    UndoAction action = std::move(undoStack_.back());
    undoStack_.pop_back();

    // The redo entry is the SAME action, unmodified — Redo() replays it forward,
    // so it must keep its original start/text/carets. (An earlier version pushed a
    // caret-swapped copy here, which made a redo land the caret where the undo had
    // put it.)
    redoStack_.push_back(action);

    result.caretAfter = action.caretBefore;

    switch (action.type) {
    case UndoAction::Type::Insert:
        // Undo an insert: delete the inserted text.
        result.start = action.start;
        result.len = static_cast<uint32_t>(action.text.size());
        result.newText.clear();
        break;

    case UndoAction::Type::Delete:
        // Undo a delete: re-insert the deleted text.
        result.start = action.start;
        result.len = 0;
        result.newText = action.text;
        break;

    case UndoAction::Type::Replace:
        // Undo a replace: put oldText back where newText is.
        result.start = action.start;
        result.len = static_cast<uint32_t>(action.text.size());
        result.newText = action.oldText;
        break;
    }

    return true;
}

bool UndoStack::Redo(UndoResult& result) {
    if (redoStack_.empty()) return false;

    UndoAction action = std::move(redoStack_.back());
    redoStack_.pop_back();

    // Back onto the undo stack unchanged, for the same reason as above.
    // NOT through PushUndo: that clears the redo stack (correct for a NEW edit,
    // wrong here — it would make redo work exactly once per undo run).
    undoStack_.push_back(action);

    result.caretAfter = action.caretAfter;

    switch (action.type) {
    case UndoAction::Type::Insert:
        // Redo an insert: insert the text again.
        result.start = action.start;
        result.len = 0;
        result.newText = action.text;
        break;

    case UndoAction::Type::Delete:
        // Redo a delete: delete it again.
        result.start = action.start;
        result.len = static_cast<uint32_t>(action.text.size());
        result.newText.clear();
        break;

    case UndoAction::Type::Replace:
        // Redo a replace: newText over oldText.
        result.start = action.start;
        result.len = static_cast<uint32_t>(action.oldText.size());
        result.newText = action.text;
        break;
    }

    return true;
}

void UndoStack::PushUndo(UndoAction action) {
    undoStack_.push_back(std::move(action));
    if (undoStack_.size() > maxSize_) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();  // any new edit clears redo
}

} // namespace fluent
