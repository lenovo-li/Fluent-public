// TextEditBase.h — shared editing model for self-drawn text controls.
//
// Holds the pieces that a single-line TextBox and a multi-line TextArea have in
// common: the text buffer, caret + selection, IME composition string, clipboard
// cut/copy/paste, caret blink, and the editing-key handlers (Backspace, Delete,
// Ctrl+A/C/X/V). Layout, caret geometry, navigation (arrows/Home/End), scroll,
// and Render are left to the subclass, which knows whether it wraps.
//
// Indices are UTF-16 code-unit offsets into the text; the caret sits between
// code units [0..length]; a selection is the range [min(anchor,caret), max).
#pragma once

#include "../core/Control.h"
#include "../base/Event.h"
#include "../graphics/DWriteContext.h"
#include "../text/UndoStack.h"
#include "MenuFlyout.h"
#include <dwrite_3.h>
#include <functional>
#include <string>
#include <vector>

namespace fluent {

class TextEditBase : public Control {
public:
    // Widest border stroke either subclass paints (focused). Half of it lands outside
    // bounds_; CollectDirtyBounds accounts for that.
    static constexpr float kBorderStrokeMax = 1.5f;

    TextEditBase() { SetFocusable(true); }

    void SetText(const std::wstring& text);
    const std::wstring& Text() const { return text_; }
    void SetPlaceholder(std::wstring text) { placeholder_ = std::move(text); Invalidate(); }
    void SetReadOnly(bool ro) { readOnly_ = ro; }
    bool ReadOnly() const { return readOnly_; }

    // Limit the text length. 0 (default) = unlimited. Enforced at insertion time
    // (typing, paste, IME commit); SetText is NOT clamped — it sets initial content.
    void SetMaxLength(size_t len) { maxLength_ = len; }
    size_t MaxLength() const { return maxLength_; }

    // --- Input policy (P1-2) ----------------------------------------------
    // Force the case of inserted text, WinForms CharacterCasing-style. Applied to
    // typing, paste and IME commit alike, so a field declared Upper cannot be filled
    // with lower-case text by any route.
    //
    // Applied to INSERTED text only — SetText and undo replay are untouched. SetText is
    // programmatic content the caller already controls; undo must restore exactly the
    // bytes that were there, and re-casing on replay would make Ctrl+Z land on a state
    // the user never saw.
    enum class CharacterCasing { Normal, Upper, Lower };
    void SetCharacterCasing(CharacterCasing c) { casing_ = c; }
    CharacterCasing GetCharacterCasing() const { return casing_; }

    // Reject individual characters. Return true to accept. Called per code unit of every
    // insertion, after casing and before the length check.
    //
    // Rejected characters are DROPPED, not the whole run: pasting "a1b2c3" into a
    // digits-only field yields "123" rather than nothing, which is what makes paste
    // usable for the common case of copying a formatted number.
    //
    // Deliberately a per-character predicate rather than a whole-string validator: a
    // string-level "is this valid" check cannot express partial acceptance, and a field
    // is edited one character at a time, so most intermediate states of a string-level
    // rule are invalid ("4." while typing "4.5") and rejecting them makes the field
    // impossible to type into.
    void SetInputFilter(std::function<bool(wchar_t)> f) { inputFilter_ = std::move(f); }
    void ClearInputFilter() { inputFilter_ = nullptr; }
    bool HasInputFilter() const { return static_cast<bool>(inputFilter_); }

    // Fired when the text changes (typing, delete, cut, paste). Payload is the new
    // full text. Replaces SetOnChange(std::function<void(const std::wstring&)>).
    Event<TextEditBase, std::wstring>& TextChanged() { return textChanged_; }

    // Fired when the selection changes (caret move, drag selection, Select() call).
    // Payload is (start, length) of the current selection.
    struct SelectionChangedArgs { UINT32 start; UINT32 length; };
    Event<TextEditBase, SelectionChangedArgs>& SelectionChanged() { return selectionChanged_; }

    // Drive the IME composition string without an IME. OnImeComposition needs a real
    // HWND and a live input context, so the composition code path is otherwise
    // unreachable from a headless test — and that path has a performance cliff on it
    // (a composition makes DisplayText copy the whole buffer), which is exactly the
    // kind of thing that needs a regression test. Sets the same state the IME sets and
    // fires the same invalidation hook.
    void TestSetComposition(std::wstring s) {
        composition_ = std::move(s);
        OnCompositionDirty();
        Invalidate();
    }

    // Place the caret (and collapse the selection there) without going through input.
    // SetText leaves the caret at the END of the buffer, so any test about behaviour
    // that depends on WHERE the caret is — a long-line layout clip is bounded only when
    // the caret is not demanding the far end of the line — cannot set that up otherwise.
    // Clamped, so a test cannot accidentally establish an out-of-range caret and then
    // assert against whatever the clamp happened to produce.
    void TestSetCaret(UINT32 index) {
        caret_ = selAnchor_ = std::min<UINT32>(index, static_cast<UINT32>(text_.size()));
    }

    // Selection endpoints, for tests. Unordered on purpose (anchor and caret, not
    // min/max) so a test can also check WHICH end the caret is on — that distinguishes
    // "dragged forward" from "dragged backward", which matters for word-granularity
    // drags where the fixed edge switches sides.
    UINT32 SelectionStartForTest() const { return selAnchor_; }
    UINT32 SelectionEndForTest() const { return caret_; }

    // Programmatic selection API (public, not test-only). WinForms TextBox-style: start
    // + length, not anchor/caret. Clamped to the actual text length.
    void Select(UINT32 start, UINT32 length);
    UINT32 SelectionStart() const;
    UINT32 SelectionLength() const;

    // Run the same select-all the Ctrl+A key path and the context menu run, including
    // the OnSelectionChanged notification. Exists because reaching Ctrl+A itself needs a
    // focus manager and a key route, while the thing under test is what the selection
    // change does to a composited subclass.
    void SelectAllForTest() {
        selAnchor_ = 0;
        caret_ = static_cast<UINT32>(text_.size());
        OnSelectionChanged();
    }

    // --- Undo / redo ------------------------------------------------------
    // Ctrl+Z / Ctrl+Y (and Ctrl+Shift+Z) are wired to these in OnKeyDownRouted.
    //
    // Every mutation the control makes to text_ goes through one of the three
    // Record* helpers below, so the stack is complete by construction rather than
    // by remembering to call it at each site. The one path deliberately NOT
    // recorded is SetText: replacing the document is a "new document" event, and
    // an undo across it would restore text the caller never showed the user.
    // SetText clears the history instead.
    void Undo();
    void Redo();
    bool CanUndo() const { return undo_.CanUndo(); }
    bool CanRedo() const { return undo_.CanRedo(); }
    // Drop the history (switching documents in the same control).
    void ClearUndoHistory() { undo_.Clear(); }

    // The border is stroked ON the bounds edge, so half its width falls OUTSIDE
    // bounds_ (WP-07 §S4) — otherwise the outer half survives a partial redraw as a
    // stale line. kBorderStrokeMax is the widest the subclasses stroke it (1.5 while
    // focused).
    float VisualOverflowDip() const override {
        return kBorderStrokeMax * 0.5f + 1.0f;
    }

    // Element overrides shared by both editors (routed).
    void OnTextInput(wchar_t ch) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    HCURSOR Cursor() const override;
    bool WantsBlink() const override { return true; }
    void OnBlink() override { caretVisible_ = !caretVisible_; Invalidate(); }
    void OnImeStartComposition(HWND hwnd) override;
    void OnImeComposition(HWND hwnd, LPARAM flags) override;
    void OnImeEndComposition(HWND hwnd) override;

protected:
    void OnFocusChanged() override;
    // DWrite arrives with the tree context (roadmap §6.2): rebuild the cached
    // layout and repaint once attached (replaces the old SetDWrite side effects).
    void OnAttachedToTree() override;

    // Refresh the context-menu item state and place the caret. See the base declaration
    // for why the items must be rebuilt rather than merely reused.
    void OnContextMenuOpening(float dipX, float dipY) override;

    // The character offset a pointer at these window DIPs addresses. Implemented by the
    // subclass, which owns the layout. Needed by OnContextMenuOpening so a right-click
    // outside the selection can move the caret there — the base cannot hit-test itself.
    virtual UINT32 IndexAtPoint(float dipX, float dipY) const = 0;

    // Install the cut/copy/paste/select-all menu. Called on attach; a subclass that
    // wants no menu (or a different one) overrides OnAttachedToTree and skips it.
    void InstallEditContextMenu();
    // The item list with enabled flags computed from the CURRENT state.
    std::vector<MenuItem> BuildEditMenuItems();

    // --- Subclass responsibilities ---------------------------------------
    // Invalidate any cached text layout (size / text / font changed).
    virtual void OnTextLayoutDirty() {}
    // The selection moved without the text changing. Default: repaint.
    //
    // WHY THIS EXISTS AS A HOOK rather than an Invalidate() at each call site. A
    // composited subclass paints the selection highlight into a composition surface and
    // returns early from Render(), so Invalidate() alone reaches none of those pixels —
    // the highlight only appears the next time something else happens to re-rasterize
    // the surface, which in practice meant "after the user scrolls". Ctrl+A looked like
    // it did nothing at all.
    //
    // Note that MoveCaret does NOT need this: it already routes through
    // EnsureCaretVisible, which a composited subclass overrides. What needs it is every
    // path that sets caret_/selAnchor_ DIRECTLY — Ctrl+A, double-click word select,
    // triple-click line select. That asymmetry is the whole reason this is easy to get
    // wrong, so any new direct assignment to the selection fields belongs here too.
    virtual void OnSelectionChanged() {
        Invalidate();
        UINT32 start = 0, length = 0;
        SelectionRange(start, length);
        SelectionChangedArgs args{start, length};
        selectionChanged_.Raise(*this, args);
    }

    // Invalidate what an IME COMPOSITION change affects, which is strictly less than
    // what a text change affects: text_ is untouched, so anything derived purely from
    // text_ (a line index, per-line layouts away from the caret) is still valid.
    //
    // Separate from OnTextLayoutDirty because collapsing the two costs O(document) per
    // composition keystroke in a subclass that indexes the buffer — which is seconds
    // per key once the document is tens of MB. Default: treat it as a full text change,
    // which is correct for a subclass that caches nothing derived from text_ alone.
    virtual void OnCompositionDirty() { OnTextLayoutDirty(); }
    // Scroll so the caret is on screen (subclass knows the axis).
    virtual void EnsureCaretVisible() {}
    // Navigation keys the subclass owns (arrows, Home/End, PageUp/Down). Return
    // true if consumed. Called after the base handles editing/clipboard keys.
    virtual bool OnNavigationKey(UINT vk, bool shift) {
        UNREFERENCED_PARAMETER(vk); UNREFERENCED_PARAMETER(shift); return false;
    }
    // Filter text arriving from a keystroke or paste (e.g. single-line strips
    // newlines). Default: accept as-is.
    virtual std::wstring SanitizeInput(std::wstring s) const { return s; }
    // Whether a typed character should be inserted (single-line rejects \r\n via
    // SanitizeInput; this gates control chars). Default rejects < space.
    virtual bool AcceptsChar(wchar_t ch) const { return ch >= 0x20 && ch != 0x7F; }

    // --- Multi-click selection (double = word, triple = line) --------------
    // The unit an in-progress drag extends by. A drag that began as a double-click
    // extends whole words, which is what every editor does and what makes a
    // double-click-then-drag feel like it "grips" the text instead of degrading to
    // character selection the moment the pointer moves.
    enum class SelectGranularity { Character, Word, Line };

    // Set the selection for a press at character `index` with the given click count.
    // Returns true when it consumed the press (count >= 2); false for a plain single
    // click, which the subclass then handles as a caret placement.
    //
    // Lives in the base because the logic is identical for both editors: a character
    // offset in, a selection out. Only "which offset did the pointer hit" differs, and
    // that is already the subclass's job (it owns the layout).
    bool ApplyMultiClickSelection(UINT32 index, int clickCount);

    // Extend the in-progress drag to `index`, honouring the granularity the gesture
    // started with. Character granularity is the plain caret move; Word and Line snap
    // both ends outward so partially covered words/lines are fully selected.
    void ExtendDragSelection(UINT32 index);

    // Record that a single-click drag is starting (granularity Character, anchor at
    // `index`). Subclasses call this on a plain press so a later drag knows what unit
    // to extend by.
    void BeginCharacterDrag(UINT32 index);

    // --- Shared editing helpers (index-based, layout-agnostic) ------------
    void SelectionRange(UINT32& start, UINT32& len) const;
    bool HasSelection() const { return caret_ != selAnchor_; }
    void DeleteSelection();               // caret returns to selection start
    void InsertText(const std::wstring& s);
    void MoveCaret(UINT32 to, bool extendSelection);

    // Erase [start, start+len) and record it for undo. The single place a
    // non-selection deletion (Backspace / Delete) mutates the buffer, so the undo
    // entry cannot be forgotten at one of the call sites.
    void EraseRange(UINT32 start, UINT32 len);
    // DeleteSelection + record. Used by Backspace/Delete/Cut with a live selection.
    void DeleteSelectionRecorded();
    // Apply an UndoResult to the buffer without recording it (that would push the
    // reverse of the undo back onto the stack and make Ctrl+Z a toggle).
    void ApplyUndoResult(const UndoResult& r);
    void Changed();                       // EnsureCaretVisible + blink + cb + invalidate
    void ResetBlink() { caretVisible_ = true; }
    bool CopySelection(HWND owner) const;
    void Paste(HWND owner);
    // The window to open the clipboard against — Context().hwnd, not the active window.
    // See the .cpp for the three ways GetActiveWindow() gets this wrong.
    HWND ClipboardOwner() const;

    // Layout string = text (+ inline IME composition). Password subclass overrides.
    //
    // RETURNS A REFERENCE, and takes a caller-owned scratch buffer to build into when a
    // transform is actually needed. In the common case — no IME composition, no
    // password masking — the layout string IS text_, and this hands back a reference to
    // it and touches `scratch` not at all.
    //
    // WHY THE SIGNATURE IS SHAPED LIKE THIS. It used to return std::wstring by value,
    // which copied the entire buffer on every call. That is invisible at the sizes a
    // text box normally holds and ruinous past a megabyte or so, and the call sites make
    // it worse than it looks: several of them want only the LENGTH (to clamp a caret
    // index) or only whether it is EMPTY (to choose the placeholder), and were paying a
    // full copy for a question the buffer size alone answers. CaretMetrics is on the
    // per-keystroke and per-tick paths, so a large document copied its whole buffer
    // several times per frame to compute a caret rectangle.
    //
    // Returning a reference means the value is only valid until the next mutation of
    // text_ / composition_ (or the next call that reuses the same scratch buffer),
    // which is exactly the lifetime every existing call site already needs — each one
    // uses it within a single expression or block.
    virtual const std::wstring& DisplayText(std::wstring& scratch) const;

    // Length of DisplayText() without building it. Use when only the size is needed —
    // clamping an index, sizing a range — so no buffer is copied or composed.
    size_t DisplayLength() const { return text_.size() + composition_.size(); }
    // Whether DisplayText() would be empty, without building it.
    bool DisplayEmpty() const { return text_.empty() && composition_.empty(); }

    std::wstring text_;
    std::wstring placeholder_;
    std::wstring composition_;            // active IME composition (inline)
    bool readOnly_ = false;
    size_t maxLength_ = 0;                // 0 = unlimited; enforced in InsertText
    CharacterCasing casing_ = CharacterCasing::Normal;
    std::function<bool(wchar_t)> inputFilter_;  // null = accept everything

    UINT32 caret_ = 0;                    // insertion point (code-unit index)
    UINT32 selAnchor_ = 0;                // selection origin; == caret_ when none
    bool selecting_ = false;              // mouse drag-select in progress
    // Drag granularity, and the range the gesture STARTED from. The start range is kept
    // separately from selAnchor_ because a word/line drag has to be able to grow in
    // either direction from a whole unit: dragging left from a double-clicked word must
    // keep that word's END as the fixed edge, and dragging right must keep its START.
    // A single anchor index cannot express that, so a drag reversal would collapse the
    // originally clicked word to a caret.
    SelectGranularity dragGranularity_ = SelectGranularity::Character;
    UINT32 dragStartLo_ = 0;
    UINT32 dragStartHi_ = 0;
    bool caretVisible_ = true;            // blink phase
    Event<TextEditBase, std::wstring> textChanged_;
    Event<TextEditBase, SelectionChangedArgs> selectionChanged_;

    UndoStack undo_;                      // undo/redo history for text edits
};

} // namespace fluent
