// ComboBox.h — Fluent dropdown list selector.
//
// The header (drawn in the parent window) is a rounded button showing the
// currently selected item text plus a chevron glyph. Clicking or pressing
// Space/Enter/Alt+Down opens a popup with a scrollable list of items. The popup
// is a separate top-level HWND (PopupHost) positioned below (or above if near
// the screen bottom) the header, dismissed by clicking outside, selecting an
// item, or pressing Esc.
//
// ComboBox is a Selector<std::wstring> (roadmap §WP-06): the item vector and
// selected-index storage (+ clamp/dedup) live in the Selector/ItemsControl base.
// The double storage of WP-05 (ComboBox::items_ mirrored into ComboListView::items_)
// is eliminated — the list view now holds a pointer to the shared items_ vector.
#pragma once

#include "primitives/Selector.h"
#include "../core/Subscription.h"
#include "../base/Event.h"
#include "../graphics/DWriteContext.h"
#include <string>
#include <vector>

namespace fluent {

class WindowServices;
class PopupHost;

class ComboBox : public Selector<std::wstring> {
public:
    ComboBox();
    ~ComboBox();

    // Set the list of items (replaces the current list). Inherited from
    // ItemsControl<wstring>; ComboBox overrides OnItemsChanged to sync the popup.
    // Keep the same signature so call sites are unchanged.
    using Selector<std::wstring>::SetItems;

    // Get/set the selected index (0-based; -1 means no selection).
    // SelectedIndex() inherited from Selector. SetSelectedIndex overridden below.
    using Selector<std::wstring>::SelectedIndex;
    void SetSelectedIndex(int index) override;

    // Fired when the selection changes (user picks an item); payload is the new
    // index. Replaces SetOnChange(std::function<void(int)>).
    Event<ComboBox, int>& SelectionChanged() { return selectionChanged_; }

    // --- Editable mode ----------------------------------------------------
    // In editable mode the header accepts typed text instead of only showing the
    // selected item, so the user can enter a value that is not in the list (the
    // classic "combo" behavior: a list of suggestions over a free-text field).
    //
    // The text is drawn and edited BY ComboBox rather than by an embedded TextBox.
    // A nested TextBox would bring a second focusable element inside this one —
    // Tab would stop on the field and again on the combo — plus its own attach
    // lifecycle, caret blink registration and context menu, all for a single line
    // with no selection. What editing actually needs here is a caret index and
    // Backspace/Delete/arrows, which is small enough to own directly.
    //
    // Deliberately NOT filtered: typing does not auto-select a matching item and
    // does not narrow the list. Which of those an app wants is a policy decision
    // (prefix vs substring, case sensitivity, whether to auto-commit) — TextChanged
    // hands the app the text and lets it call SetItems itself.
    void SetEditable(bool editable);
    bool IsEditable() const { return editable_; }

    // The current header text. In editable mode this is the typed text; otherwise
    // it is the selected item's text (empty when nothing is selected), so a caller
    // can read the value without branching on the mode.
    std::wstring Text() const;

    // Replace the typed text. No-op when not editable. Does not raise TextChanged
    // (a programmatic set is not user input); does not touch the selection.
    void SetText(std::wstring text);

    // The caret's code-unit offset into the typed text. Editable mode only.
    // Exposed because every editing key is defined by what it does to the caret,
    // and a test cannot see a drawn caret.
    UINT32 CaretIndex() const { return caret_; }

    // Fired on each user-driven text change (typing, Backspace, Delete). NOT
    // fired by SetText, and NOT fired when picking a list item — that raises
    // SelectionChanged instead, and a handler listening to both would otherwise
    // see one user action twice.
    Event<ComboBox, std::wstring>& TextChanged() { return textChanged_; }

    // Text input arrives here from the host (editable mode consumes it).
    void OnTextInput(wchar_t ch) override;
    // The caret blinks while this control is focused in editable mode.
    bool WantsBlink() const override { return editable_; }
    void OnBlink() override;
    HCURSOR Cursor() const override;

    // Set popup card background opacity [0..1], default 1.0 (opaque).
    //
    // Safe to call BEFORE the control is attached: the value is stored and applied when
    // OnAttachedToTree creates the popup. It previously forwarded straight to popup_ and
    // did nothing when that was still null, so the common `build page -> configure ->
    // attach` order silently lost the setting -- and silently, because there was no
    // popup to complain. Mirrors how MenuFlyout keeps cardOpacity_ and re-applies it to
    // each popup level it creates.
    void SetPopupOpacity(float opacity);
    float PopupOpacity() const { return popupOpacity_; }

    // Control overrides.
    void Measure(float availW, float availH) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    void Render(const DrawingContext& dc) override;

    // The focus ring is stroked OUTSIDE bounds_ (WP-07 §S4) — see FocusRingPadDip.
    float VisualOverflowDip() const override;

protected:
    void OnStateChanged() override;
    void OnFocusChanged() override { Invalidate(); }
    void OnClickRouted(PointerEventArgs& e) override;

    // ItemsControl hook: re-clamp selection after items change + sync list view.
    void OnItemsChanged() override;
    // Selector hook: fires the SelectionChanged event.
    void OnSelectionChanged(int /*old*/, int newIdx) override;

    // Build the popup host + list content from the tree context (window/DWrite).
    // Detach tears the popup down so nothing outlives the tree.
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;

private:
    void OpenPopup();
    void ClosePopup();
    void OnItemSelected(int index);

    // --- Editable-mode helpers -------------------------------------------
    // Handle an editing key (Backspace/Delete/arrows/Home/End). Returns true when
    // the key was consumed. Split out so OnKeyDownRouted stays readable and so the
    // dropdown keys and the editing keys cannot silently overlap.
    bool HandleEditKey(UINT vk);
    // Insert `s` at the caret and raise TextChanged.
    void InsertEditText(const std::wstring& s);
    // Clamp caret_ into [0, text_.size()].
    void ClampCaret();
    // The text the header should draw: typed text when editable, else the
    // selected item. Shared by Render and Text() so the two cannot disagree.
    const std::wstring& HeaderText() const;

    // Header bounds in screen pixels (popup anchor + light-dismiss self-check).
    RECT HeaderScreenRect() const;
    bool HeaderContainsScreenPoint(int screenX, int screenY) const;

    // Cached from Context().window for the attach period (valid between
    // OnAttachedToTree and OnDetachedFromTree; used for popup positioning and the
    // dismiss registry). Not an external injection — the tree provides it.
    WindowServices* window_ = nullptr;
    std::unique_ptr<PopupHost> popup_;
    // Remembered so a pre-attach SetPopupOpacity survives until the popup exists.
    float popupOpacity_ = 1.0f;
    std::unique_ptr<Control> listView_;  // ComboListView, defined in .cpp

    bool popupOpen_ = false;

    // --- Editable mode state ---------------------------------------------
    bool editable_ = false;
    std::wstring text_;     // typed text (editable mode only)
    UINT32 caret_ = 0;      // caret offset into text_ [0..text_.size()]
    bool caretVisible_ = false;  // blink phase (drawn when true)

    Event<ComboBox, std::wstring> textChanged_;

    // Empty string returned by HeaderText() when nothing is selected and not
    // editable. Exists so HeaderText() can return a reference in all paths.
    static const std::wstring emptyString_;

    // Owns the window's active-popup-dismiss registration while the dropdown is
    // open. Reset on close (and by ~ComboBox), so the window never holds a
    // dangling [this] dismiss callback after this ComboBox is destroyed.
    Subscription dismissSub_;

    Event<ComboBox, int> selectionChanged_;
};

} // namespace fluent
