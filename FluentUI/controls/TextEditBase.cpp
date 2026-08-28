// TextEditBase.cpp — shared editing model for TextBox / TextArea.

#include "TextEditBase.h"
#include "../text/WordBoundary.h"
#include <imm.h>
#include <algorithm>

#pragma comment(lib, "imm32.lib")

namespace fluent {

void TextEditBase::SetText(const std::wstring& text) {
    text_ = text;
    caret_ = selAnchor_ = static_cast<UINT32>(text_.size());
    // Replacing the whole document is a "new document" event, not an edit: undoing
    // across it would restore text the caller never displayed. Drop the history.
    undo_.Clear();
    OnTextLayoutDirty();
    EnsureCaretVisible();
    Invalidate();
}

const std::wstring& TextEditBase::DisplayText(std::wstring& scratch) const {
    // No composition: the layout string is the buffer itself. Return a reference and
    // copy nothing — this is the path every frame takes while not using an IME.
    if (composition_.empty()) return text_;
    scratch = text_;
    scratch.insert(std::min<size_t>(caret_, scratch.size()), composition_);
    return scratch;
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void TextEditBase::OnAttachedToTree() {
    OnTextLayoutDirty();
    InstallEditContextMenu();
    Invalidate();
}

std::vector<MenuItem> TextEditBase::BuildEditMenuItems() {
    // Every `enabled` below is computed HERE, at open time. MenuItem::enabled is copied
    // into the flyout by SetItems, so a list built once at attach would grey items
    // according to conditions from then — "Copy" permanently disabled because there was
    // no selection when the control was created.
    const bool hasSel = HasSelection();
    // Whether the clipboard actually holds text. Checked rather than assumed so "Paste"
    // is honestly greyed on a fresh login or after copying an image.
    const bool canPaste = !readOnly_ && IsClipboardFormatAvailable(CF_UNICODETEXT);

    std::vector<MenuItem> items;

    MenuItem cut;
    cut.text = L"剪切";
    cut.accelerator = L"Ctrl+X";
    cut.enabled = hasSel && !readOnly_;
    cut.onInvoke = [this] {
        if (CopySelection(ClipboardOwner())) {
            DeleteSelection();
            OnTextLayoutDirty();
            Changed();
        }
    };
    items.push_back(std::move(cut));

    MenuItem copy;
    copy.text = L"复制";
    copy.accelerator = L"Ctrl+C";
    copy.enabled = hasSel;
    copy.onInvoke = [this] { CopySelection(ClipboardOwner()); };
    items.push_back(std::move(copy));

    MenuItem paste;
    paste.text = L"粘贴";
    paste.accelerator = L"Ctrl+V";
    paste.enabled = canPaste;
    paste.onInvoke = [this] { Paste(ClipboardOwner()); };
    items.push_back(std::move(paste));

    items.push_back(MenuItem::Sep());

    MenuItem selectAll;
    selectAll.text = L"全选";
    selectAll.accelerator = L"Ctrl+A";
    selectAll.enabled = !text_.empty();
    selectAll.onInvoke = [this] {
        selAnchor_ = 0;
        caret_ = static_cast<UINT32>(text_.size());
        // Not Invalidate(): a composited subclass paints the highlight into its own
        // surface. Same reason as the Ctrl+A key path.
        OnSelectionChanged();
    };
    items.push_back(std::move(selectAll));

    return items;
}

void TextEditBase::InstallEditContextMenu() {
    auto menu = std::make_unique<MenuFlyout>();
    menu->SetItems(BuildEditMenuItems());
    SetContextMenu(std::move(menu));
}

void TextEditBase::OnContextMenuOpening(float dipX, float dipY) {
    // A right-click INSIDE the selection must leave the selection alone: the user is
    // about to pick Copy or Cut, and moving the caret would destroy the very thing they
    // are acting on — the menu would then offer a greyed-out Copy over text that looked
    // selected a moment ago. Outside the selection, a right-click places the caret, which
    // is what makes "right-click there, then Paste" work.
    const UINT32 index = IndexAtPoint(dipX, dipY);
    UINT32 selStart = 0, selLen = 0;
    SelectionRange(selStart, selLen);
    const bool insideSelection = selLen > 0 && index >= selStart && index <= selStart + selLen;
    if (!insideSelection) {
        caret_ = selAnchor_ = index;
        OnSelectionChanged();
    }

    // Rebuild with the state as it is NOW (see BuildEditMenuItems).
    if (auto* flyout = static_cast<MenuFlyout*>(ContextMenu()))
        flyout->SetItems(BuildEditMenuItems());
}

// ---------------------------------------------------------------------------
// Multi-click selection
// ---------------------------------------------------------------------------

void TextEditBase::BeginCharacterDrag(UINT32 index) {
    dragGranularity_ = SelectGranularity::Character;
    dragStartLo_ = dragStartHi_ = index;
}

bool TextEditBase::ApplyMultiClickSelection(UINT32 index, int clickCount) {
    if (clickCount < 2) return false;

    if (clickCount == 2) {
        auto [s, e] = WordRangeAt(text_, index);
        dragGranularity_ = SelectGranularity::Word;
        dragStartLo_ = static_cast<UINT32>(s);
        dragStartHi_ = static_cast<UINT32>(e);
    } else {
        // Triple: the LOGICAL line, not the visual one. A visual line would change what
        // gets selected when the window is resized, which is indefensible — the user
        // selected a sentence, not a set of pixels. LogicalLineRangeAt works straight off
        // text_, so this is identical in Wrap and NoWrap and needs no line index. For a
        // single-line TextBox the buffer holds no '\n', so it selects everything, which
        // is the right answer there too.
        auto [s, e] = LogicalLineRangeAt(text_, index);
        dragGranularity_ = SelectGranularity::Line;
        dragStartLo_ = static_cast<UINT32>(s);
        dragStartHi_ = static_cast<UINT32>(e);
    }

    selAnchor_ = dragStartLo_;
    caret_ = dragStartHi_;
    // Direct assignment to the selection fields, so the composited subclass has to be
    // told (see OnSelectionChanged). MoveCaret is deliberately not used: it would clamp
    // and re-derive the anchor, discarding the range just computed.
    OnSelectionChanged();
    return true;
}

void TextEditBase::ExtendDragSelection(UINT32 index) {
    if (dragGranularity_ == SelectGranularity::Character) {
        caret_ = std::min<UINT32>(index, static_cast<UINT32>(text_.size()));
        return;
    }

    // Snap the pointer end outward to a whole unit, then pin whichever edge of the
    // ORIGINAL unit is on the far side. This is what keeps the initially clicked word
    // fully selected regardless of which way the drag goes.
    size_t lo = 0, hi = 0;
    if (dragGranularity_ == SelectGranularity::Word) {
        auto r = WordRangeAt(text_, index);
        lo = r.first; hi = r.second;
        // A click that lands exactly on a newline yields an empty range; fall back to
        // the raw index so the drag still tracks the pointer instead of freezing.
        if (lo == hi) { lo = hi = std::min<size_t>(index, text_.size()); }
    } else {
        auto r = LogicalLineRangeAt(text_, index);
        lo = r.first; hi = r.second;
    }

    if (hi > dragStartHi_) {          // dragging forward
        selAnchor_ = dragStartLo_;
        caret_ = static_cast<UINT32>(hi);
    } else if (lo < dragStartLo_) {   // dragging backward
        selAnchor_ = dragStartHi_;
        caret_ = static_cast<UINT32>(lo);
    } else {                          // still inside the original unit
        selAnchor_ = dragStartLo_;
        caret_ = dragStartHi_;
    }
}

// ---------------------------------------------------------------------------
// Selection + editing
// ---------------------------------------------------------------------------

void TextEditBase::SelectionRange(UINT32& start, UINT32& len) const {
    start = std::min(caret_, selAnchor_);
    len = std::max(caret_, selAnchor_) - start;
}

void TextEditBase::Select(UINT32 start, UINT32 length) {
    const UINT32 maxPos = static_cast<UINT32>(text_.size());
    start = std::min(start, maxPos);
    UINT32 end = std::min(start + length, maxPos);
    selAnchor_ = start;
    caret_ = end;
    OnSelectionChanged();
}

UINT32 TextEditBase::SelectionStart() const {
    return std::min(caret_, selAnchor_);
}

UINT32 TextEditBase::SelectionLength() const {
    return std::max(caret_, selAnchor_) - std::min(caret_, selAnchor_);
}

void TextEditBase::DeleteSelection() {
    if (!HasSelection()) return;
    UINT32 start = 0, len = 0;
    SelectionRange(start, len);
    text_.erase(start, len);
    caret_ = selAnchor_ = start;
}

void TextEditBase::InsertText(const std::wstring& raw) {
    if (readOnly_) return;
    std::wstring s = SanitizeInput(raw);
    if (s.empty()) return;

    // --- P1-2: CharacterCasing + InputFilter (applied before MaxLength) -------
    // 1. CharacterCasing: force the case of every inserted character. Applied to
    //    typing, paste, IME commit alike — any route. SetText and undo replay are
    //    excluded deliberately: SetText is programmatic (caller already controls it),
    //    and undo must restore exactly the bytes that were there; re-casing on replay
    //    would make Ctrl+Z land on a state the user never saw.
    if (casing_ != CharacterCasing::Normal) {
        for (wchar_t& c : s) {
            if (casing_ == CharacterCasing::Upper) {
                c = static_cast<wchar_t>(::towupper(c));
            } else {
                c = static_cast<wchar_t>(::towlower(c));
            }
        }
    }

    // 2. InputFilter: drop individual characters the predicate rejects. Pasting
    //    "a1b2c3" into a digits-only field yields "123", not an all-or-nothing reject,
    //    which is what makes paste usable for copying formatted numbers.
    if (inputFilter_) {
        s.erase(std::remove_if(s.begin(), s.end(),
                               [this](wchar_t c) { return !inputFilter_(c); }),
                s.end());
        if (s.empty()) return;
    }

    // MaxLength: TRUNCATE the incoming run to the room left rather than rejecting it
    // whole. Rejecting would make pasting 500 characters into a 10-character field do
    // nothing at all, which reads as "the paste is broken"; filling what fits is what
    // every OS text field does and tells the user where the limit is.
    //
    // The room has to be computed against what the buffer will hold AFTER the pending
    // selection is replaced. Typing over a 5-character selection in a field that is
    // already full frees 5 units first, so room is (limit - (size - selLen)). Using the
    // simple (limit - size) would make every overwrite at the limit silently no-op,
    // which is the bug this arithmetic exists to avoid.
    //
    // This is the only length check in the class, and it is here because every mutation
    // that ADDS text — typing, paste, IME commit — funnels through this function. Undo
    // replay deliberately bypasses it (ApplyUndoResult writes text_ directly): a redo
    // must restore exactly what was there, and re-clamping could silently change it.
    if (maxLength_ > 0) {
        size_t occupied = text_.size();
        if (HasSelection()) {
            UINT32 selStart = 0, selLen = 0;
            SelectionRange(selStart, selLen);
            occupied -= selLen;
        }
        const size_t room = (occupied >= maxLength_) ? 0 : (maxLength_ - occupied);
        if (room == 0) return;
        if (s.size() > room) {
            s.resize(room);
            // Never cut between the halves of a surrogate pair. A lone high surrogate is
            // not valid UTF-16 and DWrite paints it as the replacement glyph, so the
            // field would show a limit of "10 characters" and then render a box as the
            // tenth. Dropping the orphan costs one character and keeps the text valid.
            if (!s.empty() && s.back() >= 0xD800 && s.back() <= 0xDBFF)
                s.pop_back();
            if (s.empty()) return;
        }
    }

    const UINT32 caretBefore = caret_;

    // Typing over a selection is ONE undoable operation, not a delete followed by an
    // insert — otherwise Ctrl+Z leaves the text deleted, which is not a state the user
    // ever saw. Record it as a Replace so a single undo restores both.
    if (HasSelection()) {
        UINT32 selStart = 0, selLen = 0;
        SelectionRange(selStart, selLen);
        std::wstring replaced = text_.substr(selStart, selLen);
        DeleteSelection();
        text_.insert(caret_, s);
        caret_ += static_cast<UINT32>(s.size());
        selAnchor_ = caret_;
        undo_.RecordReplace(selStart, replaced, s, caretBefore, caret_);
    } else {
        const UINT32 at = caret_;
        text_.insert(caret_, s);
        caret_ += static_cast<UINT32>(s.size());
        selAnchor_ = caret_;
        // A single typed character can merge with the preceding run; anything longer
        // (paste, IME commit) stays its own entry. Both overloads of RecordInsert exist
        // for exactly this distinction.
        if (s.size() == 1)
            undo_.RecordInsert(at, s[0], caretBefore, caret_);
        else
            undo_.RecordInsert(at, s, caretBefore, caret_);
    }

    OnTextLayoutDirty();
    Changed();
}

void TextEditBase::EraseRange(UINT32 start, UINT32 len) {
    if (readOnly_ || len == 0 || start >= text_.size()) return;
    len = std::min<UINT32>(len, static_cast<UINT32>(text_.size()) - start);
    std::wstring removed = text_.substr(start, len);
    const UINT32 caretBefore = caret_;
    text_.erase(start, len);
    caret_ = selAnchor_ = start;
    undo_.RecordDelete(start, removed, caretBefore, caret_);
}

void TextEditBase::DeleteSelectionRecorded() {
    if (!HasSelection()) return;
    UINT32 start = 0, len = 0;
    SelectionRange(start, len);
    EraseRange(start, len);
}

void TextEditBase::ApplyUndoResult(const UndoResult& r) {
    // Clamp against the current buffer. The stack's indices are consistent with the
    // edits it recorded, but SetText (which clears history) and a readOnly_ toggle can
    // both land between record and replay, and a bad substr here is a crash rather
    // than a wrong character.
    const UINT32 size = static_cast<UINT32>(text_.size());
    const UINT32 start = std::min(r.start, size);
    const UINT32 len = std::min(r.len, size - start);

    if (len > 0) text_.erase(start, len);
    if (!r.newText.empty()) text_.insert(start, r.newText);

    caret_ = selAnchor_ = std::min<UINT32>(r.caretAfter, static_cast<UINT32>(text_.size()));
    OnTextLayoutDirty();
    Changed();
}

void TextEditBase::Undo() {
    if (readOnly_) return;
    UndoResult r;
    if (!undo_.Undo(r)) return;
    ApplyUndoResult(r);
}

void TextEditBase::Redo() {
    if (readOnly_) return;
    UndoResult r;
    if (!undo_.Redo(r)) return;
    ApplyUndoResult(r);
}

void TextEditBase::MoveCaret(UINT32 to, bool extend) {
    caret_ = std::min<UINT32>(to, static_cast<UINT32>(text_.size()));
    if (!extend) selAnchor_ = caret_;
    EnsureCaretVisible();
    ResetBlink();
    Invalidate();
}

void TextEditBase::Changed() {
    EnsureCaretVisible();
    ResetBlink();
    std::wstring snapshot = text_;
    textChanged_.Raise(*this, snapshot);
    Invalidate();
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

// The HWND to open the clipboard against.
//
// NOT GetActiveWindow(), which is what this used to be and is wrong in three ways: it
// returns null when the app is not the foreground application (the clipboard then has no
// owner, so a later WM_DESTROYCLIPBOARD goes nowhere); inside a dialog it returns the
// dialog rather than the window hosting this control, depending on which happens to be
// active; and in a multi-window app it returns whichever window is active, which need not
// be this control's window at all. The context knows the right answer — that is what
// UIContext::hwnd is for.
//
// The GetActiveWindow fallback stays for the un-attached case (no context yet): passing
// null to OpenClipboard is legal and mostly works, so a best-effort guess beats refusing
// to copy.
HWND TextEditBase::ClipboardOwner() const {
    if (HWND h = Context().hwnd) return h;
    return GetActiveWindow();
}

bool TextEditBase::CopySelection(HWND owner) const {
    if (!HasSelection()) return false;
    UINT32 start = 0, len = 0;
    SelectionRange(start, len);
    std::wstring sel = text_.substr(start, len);
    SIZE_T bytes = (sel.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void* dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return false; }
    memcpy(dst, sel.c_str(), bytes);
    GlobalUnlock(mem);
    if (!OpenClipboard(owner)) { GlobalFree(mem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

void TextEditBase::Paste(HWND owner) {
    if (readOnly_ || !OpenClipboard(owner)) return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        if (const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(h))) {
            std::wstring s(src);
            GlobalUnlock(h);
            CloseClipboard();
            InsertText(s);  // SanitizeInput decides how newlines are treated
            return;
        }
    }
    CloseClipboard();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void TextEditBase::OnTextInput(wchar_t ch) {
    if (!AcceptsChar(ch)) return;  // control chars handled in OnKeyDownRouted
    InsertText(std::wstring(1, ch));
}

void TextEditBase::OnKeyDownRouted(KeyEventArgs& e) {
    UINT vk = e.vk;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const HWND owner = ClipboardOwner();

    if (ctrl) {
        switch (vk) {
            case 'A':
                selAnchor_ = 0;
                caret_ = static_cast<UINT32>(text_.size());
                // NOT a bare Invalidate(): a composited subclass paints the highlight
                // into its own surface and never sees this frame. See OnSelectionChanged.
                OnSelectionChanged();
                e.handled = true;
                return;
            case 'C': CopySelection(owner); e.handled = true; return;
            case 'X': if (!readOnly_ && CopySelection(owner)) { DeleteSelectionRecorded(); OnTextLayoutDirty(); Changed(); } e.handled = true; return;
            case 'V': Paste(owner); e.handled = true; return;
            case 'Z':
                if (shift) Redo();  // Ctrl+Shift+Z is also a redo
                else Undo();
                e.handled = true;
                return;
            case 'Y': Redo(); e.handled = true; return;
            // Ctrl+Left/Right move by word; adding Shift extends the selection.
            //
            // Handled in the base rather than delegated to OnNavigationKey because
            // horizontal word movement is identical for both editors — nothing about it
            // depends on whether the control wraps. Note that Ctrl+Home/End are NOT here:
            // those stay in TextArea::OnNavigationKey, which checks Ctrl itself, and
            // reaching them requires this switch to fall through for those keys.
            case VK_LEFT:
                MoveCaret(static_cast<UINT32>(PrevWordBoundary(text_, caret_)), shift);
                e.handled = true;
                return;
            case VK_RIGHT:
                MoveCaret(static_cast<UINT32>(NextWordBoundary(text_, caret_)), shift);
                e.handled = true;
                return;
            default: break;
        }
    }

    switch (vk) {
        case VK_BACK:
            if (!readOnly_) {
                if (HasSelection()) { DeleteSelectionRecorded(); OnTextLayoutDirty(); Changed(); }
                else if (caret_ > 0) { EraseRange(caret_ - 1, 1); OnTextLayoutDirty(); Changed(); }
            }
            e.handled = true;
            return;
        case VK_DELETE:
            if (!readOnly_) {
                if (HasSelection()) { DeleteSelectionRecorded(); OnTextLayoutDirty(); Changed(); }
                else if (caret_ < text_.size()) {
                    // EraseRange collapses the caret to `start`, which for a forward
                    // delete is where it already is — so the caret does not move, as
                    // Delete should behave.
                    EraseRange(caret_, 1);
                    OnTextLayoutDirty();
                    Changed();
                }
            }
            e.handled = true;
            return;
        default:
            // Arrows / Home / End / PageUp / PageDown belong to the subclass.
            if (OnNavigationKey(vk, shift)) e.handled = true;
            return;
    }
}

HCURSOR TextEditBase::Cursor() const {
    static HCURSOR ibeam = LoadCursor(nullptr, IDC_IBEAM);
    return ibeam;
}

void TextEditBase::OnFocusChanged() {
    if (!IsFocused()) {
        selecting_ = false;
        composition_.clear();
    }
    ResetBlink();
    Invalidate();
}

// ---------------------------------------------------------------------------
// IME
// ---------------------------------------------------------------------------

void TextEditBase::OnImeStartComposition(HWND) {
    composition_.clear();
    Invalidate();
}

void TextEditBase::OnImeComposition(HWND hwnd, LPARAM flags) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;

    if (flags & GCS_RESULTSTR) {
        LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
        if (bytes > 0) {
            std::wstring result(bytes / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(himc, GCS_RESULTSTR, result.data(), bytes);
            composition_.clear();
            InsertText(result);
        }
    }
    if (flags & GCS_COMPSTR) {
        LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
        composition_.assign(std::max<LONG>(0, bytes) / sizeof(wchar_t), L'\0');
        if (bytes > 0)
            ImmGetCompositionStringW(himc, GCS_COMPSTR, composition_.data(), bytes);
    }
    ImmReleaseContext(hwnd, himc);
    // GCS_RESULTSTR above went through InsertText, which already raised the full
    // OnTextLayoutDirty. What is left here is a composition-string update: text_ is
    // unchanged, so use the narrower hook.
    OnCompositionDirty();
    EnsureCaretVisible();
    Invalidate();
}

void TextEditBase::OnImeEndComposition(HWND) {
    composition_.clear();
    // Only the composition went away; text_ is whatever InsertText already committed.
    OnCompositionDirty();
    Invalidate();
}

} // namespace fluent
