// ListBox.cpp

#include "ListBox.h"
#include "../styling/ThemeTokens.h"
#include "../core/ScrollMath.h"
#include "../input/InputManager.h"
#include "../window/WindowServices.h"
#include "../graphics/DWriteContext.h"
#include <algorithm>

namespace fluent {

void ListBox::SetItemCount(size_t count)
{
    virtualItemCount_ = count;
    // Clear the items vector when switching to virtualized mode.
    items_.clear();
    // Clamp selection to the new count.
    if (selectedIndex_ >= static_cast<int>(count))
        selectedIndex_ = static_cast<int>(count) - 1;
    UpdateScrollExtent();
    Invalidate();
}

void ListBox::OnItemsChanged()
{
    // Direct mode: items_ was set via SetItems(). Clear virtualItemCount_ to
    // signal direct mode, clamp selection, update scroll extent.
    virtualItemCount_ = 0;
    if (selectedIndex_ >= ItemCount())
        selectedIndex_ = ItemCount() - 1;
    UpdateScrollExtent();
    Invalidate();
}

void ListBox::OnSelectionChanged(int /*oldIndex*/, int newIndex)
{
    EnsureSelectedVisible();
    if (newIndex >= 0 && newIndex < ItemCount())
        selectionChanged_.Raise(*this, newIndex);
}

void ListBox::OnSelectionSetChanged()
{
    // A Ctrl/Shift gesture moved the set. Scroll the active item into view (the
    // gesture is keyboard-reachable, so the target can be off-screen) and raise
    // the set event. Event::Raise takes Args& so the vector must be an lvalue.
    EnsureSelectedVisible();
    std::vector<int> sel = SelectedIndices();
    selectionSetChanged_.Raise(*this, sel);
}

void ListBox::OnBoundsChanged()
{
    LayoutCostProbe::Scope probe(LayoutCostKey::ListBoxBoundsChanged);
    scroll_.SetBounds(bounds_);
    UpdateScrollExtent();
}

void ListBox::OnAttachedToTree()
{
    // scroll_ is an embedded model, not a tree node, so nothing else would ever give
    // it a UIContext — and UIElement::Theme() falls back to a LIGHT default snapshot
    // when context_.theme is null. The rail was therefore drawn with light-theme ink
    // (textPrimary = near-black) regardless of the real theme, which is invisible on
    // a dark background. Forwarding the context is what puts it on the actual theme,
    // and it keeps working across a theme switch because the host overwrites its
    // snapshot in place. Same fix as ScrollPanel, TreeView, and TextArea.
    scroll_.AttachToContext(Context());
}

void ListBox::OnDetachedFromTree()
{
    // Clear the scroll model's context — it is no longer valid.
    scroll_.DetachFromContext();
}

void ListBox::UpdateScrollExtent()
{
    scroll_.SetContentHeight(itemHeight_ * ItemCount());
}

int ListBox::ItemCount() const
{
    return virtualItemCount_ > 0 ? static_cast<int>(virtualItemCount_)
                                 : static_cast<int>(items_.size());
}

// Both overloads resolve the item source identically; see ListBox.h for why the
// scratch form exists. Kept adjacent deliberately — an out-of-range index must
// produce empty from both, and a provider must win over items_ in both.
std::wstring ListBox::GetItemText(int i) const
{
    std::wstring scratch;
    // Copies out of whatever the scratch form resolved to. In direct mode that is a
    // copy of items_[i]; in virtualized mode it is a copy of a string this call just
    // moved into `scratch`. The redundant second copy is the price of the
    // value-returning signature, and is exactly why the render loop no longer uses it.
    return GetItemText(i, scratch);
}

const std::wstring& ListBox::GetItemText(int i, std::wstring& scratch) const
{
    // A function-local static, not a member: this is the only value that must outlive
    // the call while belonging to no item, and a member would make every ListBox carry
    // an always-empty string. Never mutated, so the shared instance is safe.
    static const std::wstring kEmpty;

    if (i < 0 || i >= ItemCount()) { scratch.clear(); return kEmpty; }
    if (ItemTextProvider) {
        // The provider returns by value, so there is nothing stable to reference —
        // it has to land in the caller's buffer.
        //
        // assign(), NOT `scratch = provider(...)`. The obvious spelling is a MOVE
        // assignment: it steals the temporary's heap block and frees whatever
        // capacity scratch had already grown, so every row reallocates and hoisting
        // the buffer out of the render loop buys nothing. That is the whole point of
        // this overload, so the subtlety is worth the explicit call. assign() copies
        // the characters into the buffer scratch already owns, which is one memcpy
        // into warm memory versus one allocation per visible row per frame.
        //
        // The cost is that the provider's temporary is still allocated and then
        // discarded — unavoidable while the provider signature returns by value, and
        // the allocator's free-list makes that the cheap half of the pair.
        const std::wstring produced = ItemTextProvider(static_cast<size_t>(i));
        scratch.assign(produced);
        return scratch;
    }
    if (i < static_cast<int>(items_.size()))
        return items_[i];   // the control owns it — no copy, scratch untouched
    return kEmpty;
}

int ListBox::HitItem(float dipY) const
{
    float y = dipY - bounds_.y + scroll_.Offset();
    int index = static_cast<int>(y / itemHeight_);
    return (index >= 0 && index < ItemCount()) ? index : -1;
}

int ListBox::VisibleCapacity() const
{
    return std::max(0, static_cast<int>(bounds_.h / itemHeight_) + 1);
}

int ListBox::PageStep() const
{
    return std::max(1, static_cast<int>(bounds_.h / itemHeight_) - 1);
}

void ListBox::EnsureSelectedVisible()
{
    if (selectedIndex_ < 0 || selectedIndex_ >= ItemCount()) return;
    float itemTop = selectedIndex_ * itemHeight_;
    float target = EnsureVisibleOffset(itemTop, itemHeight_,
                                       scroll_.Offset(), bounds_.h);
    if (target != scroll_.Offset())
        scroll_.SetOffset(target);
}

void ListBox::OnKeyDownRouted(KeyEventArgs& e)
{
    int count = ItemCount();
    if (count == 0) return;

    // --- Multi-select keyboard gestures (Multiple mode only) ----------------
    // Handled before the plain switch so Shift+Down extends the range instead of
    // moving the single selection. Ctrl+Space toggles the active item in place —
    // the one gesture that changes the set without moving the active index.
    if (GetSelectionMode() == SelectionMode::Multiple) {
        const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;

        if (ctrl && e.vk == VK_SPACE) {
            ToggleSelection(selectedIndex_);
            e.handled = true;
            return;
        }
        if (shift) {
            // Target index for the navigation keys that extend a range; anything
            // else falls through to the normal switch. A -1 active index starts
            // the range at 0, matching what a plain arrow key does from nothing.
            const int from = (selectedIndex_ >= 0) ? selectedIndex_ : 0;
            int target = -1;
            switch (e.vk) {
            case VK_UP:    target = std::max(0, from - 1); break;
            case VK_DOWN:  target = std::min(count - 1, from + 1); break;
            case VK_HOME:  target = 0; break;
            case VK_END:   target = count - 1; break;
            case VK_PRIOR: target = std::max(0, from - PageStep()); break;
            case VK_NEXT:  target = std::min(count - 1, from + PageStep()); break;
            default: break;
            }
            if (target >= 0) {
                RangeSelectTo(target);
                e.handled = true;
                return;
            }
        }
    }

    switch (e.vk) {
    case VK_UP:
        if (selectedIndex_ < 0)
            SetSelectedIndex(0);
        else
            SetSelectedIndex(std::max(0, selectedIndex_ - 1));
        e.handled = true;
        break;
    case VK_DOWN:
        if (selectedIndex_ < 0)
            SetSelectedIndex(0);
        else
            SetSelectedIndex(std::min(count - 1, selectedIndex_ + 1));
        e.handled = true;
        break;
    case VK_HOME:
        SetSelectedIndex(0);
        e.handled = true;
        break;
    case VK_END:
        SetSelectedIndex(count - 1);
        e.handled = true;
        break;
    case VK_PRIOR:  // Page Up
        if (selectedIndex_ < 0)
            SetSelectedIndex(0);
        else
            SetSelectedIndex(std::max(0, selectedIndex_ - PageStep()));
        e.handled = true;
        break;
    case VK_NEXT:   // Page Down
        if (selectedIndex_ < 0)
            SetSelectedIndex(0);
        else
            SetSelectedIndex(std::min(count - 1, selectedIndex_ + PageStep()));
        e.handled = true;
        break;
    case VK_SPACE:
    case VK_RETURN:
        // Space/Enter on the selected item: no-op (already selected), but mark handled.
        if (selectedIndex_ >= 0)
            e.handled = true;
        break;
    default:
        break;
    }
}

void ListBox::OnPointerWheelChanged(PointerEventArgs& e)
{
    if (scroll_.MaxOffset() <= 0.0f) return;  // nothing to scroll
    // One wheel notch scrolls 3 items (matching OS list default).
    float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
    scroll_.AnimateBy(lines * itemHeight_);
    Invalidate();
    e.handled = true;
}

void ListBox::OnPointerMoved(PointerEventArgs& e)
{
    float dipX = e.position.x, dipY = e.position.y;

    // Scrollbar drag: track the thumb.
    if (scroll_.IsDragging()) {
        scroll_.DragTo(dipY);
        Invalidate();
        e.handled = true;
        return;
    }

    // Scrollbar hover: wake the bar, expand on hover.
    scroll_.Wake();
    scroll_.SetBarHover(scroll_.HitBarRegion(dipX, dipY));

    // Item hover: update hoverIndex_ for visual feedback.
    int newHover = HitItem(dipY);
    if (newHover != hoverIndex_) {
        hoverIndex_ = newHover;
        Invalidate();
    }
}

void ListBox::OnPointerPressed(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;
    float dipX = e.position.x, dipY = e.position.y;

    // Scrollbar drag: press anywhere in the hover strip (16 DIP), not just the
    // visible thumb (3-7 DIP). A rail that lights up on hover but ignores clicks
    // outside the narrow thumb reads as broken, and 3 DIP is not a real target.
    if (scroll_.HitBarRegion(dipX, dipY)) {
        // Off-thumb presses first center the thumb on the pointer, then drag from
        // there. Without the jump the thumb would stay put and leap on the first
        // move, which feels like lag. With it the list jumps to roughly where the
        // pointer landed — consistent with how page up/down behave (they jump too).
        if (!scroll_.HitThumb(dipX, dipY)) {
            const RectDip thumb = scroll_.ThumbRect();
            const float trackRange = std::max(1.0f, bounds_.h - thumb.h);
            const float wantTop = (dipY - thumb.h * 0.5f) - bounds_.y;
            scroll_.SetOffset(wantTop / trackRange * scroll_.MaxOffset());
        }
        scroll_.BeginDrag(dipY);
        if (Context().input)
            Context().input->CapturePointer(this);
        Invalidate();
        e.handled = true;
        return;
    }

    // Item press: capture so release routes back even if pointer drifts.
    if (Context().input)
        Context().input->CapturePointer(this);
    e.handled = true;
}

void ListBox::OnPointerReleased(PointerEventArgs& e)
{
    if (e.button != PointerButton::Left) return;

    bool wasDragging = scroll_.IsDragging();
    if (wasDragging) {
        scroll_.EndDrag();
        Invalidate();
    }

    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);

    e.handled = true;
    if (wasDragging) return;

    // Item click: select the item under the pointer (if inside bounds).
    float dipX = e.position.x, dipY = e.position.y;
    if (!bounds_.contains(dipX, dipY)) return;
    if (scroll_.HitThumb(dipX, dipY)) return;

    int clickedItem = HitItem(dipY);
    if (clickedItem < 0) return;

    // Modifier-aware selection (Multiple mode only; Single mode ignores modifiers
    // so a Ctrl+click there behaves exactly as it always did).
    //
    // Ctrl wins over Shift when both are held — same precedence TreeView uses,
    // which is what Explorer does. The alternative (Ctrl+Shift extending without
    // clearing) needs a second anchor concept to be well-defined.
    if (GetSelectionMode() == SelectionMode::Multiple) {
        const bool ctrl = (e.modifiers & ModifierKeys::Ctrl) != ModifierKeys::None;
        const bool shift = (e.modifiers & ModifierKeys::Shift) != ModifierKeys::None;
        if (ctrl)  { ToggleSelection(clickedItem); return; }
        if (shift) { RangeSelectTo(clickedItem);   return; }
        // Plain click falls through: Selector::SetSelectedIndex collapses the set
        // onto this item and re-anchors, so handling it here as well would raise
        // the set event twice for one gesture.
    }

    SetSelectedIndex(clickedItem);
}

void ListBox::OnPointerLeft()
{
    // Pointer left the control: drop bar hover and clear item hover.
    scroll_.SetBarHover(false);
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        Invalidate();
    }
}

void ListBox::Render(const DrawingContext& dc)
{
    if (!Dwrite()) return;

    const ColorTokens& pal = Theme().colors;

    IDWriteTextFormat* fmt = Dwrite()->Format(
        13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return;

    // Clip to bounds so items and scrollbar don't draw outside.
    ClipGuard clip = dc.PushClip(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

    // Draw only visible items (virtualized).
    int first = static_cast<int>(scroll_.Offset() / itemHeight_);
    int visible = VisibleCapacity();
    int count = ItemCount();

    float y = bounds_.y - (scroll_.Offset() - first * itemHeight_);

    const float itemCorner = Theme().spacing.cornerRadiusSmall;

    // Hoisted OUT of the row loop on purpose: the virtualized GetItemText assigns
    // each provider result INTO this buffer, so once it has grown to the longest
    // visible row it stops allocating and every later row is a memcpy into warm
    // memory. Declared inside the loop it would be a fresh string per row and the
    // scratch overload would buy nothing. Unused entirely in direct-items mode,
    // where GetItemText returns a reference to items_[i] and never touches it.
    std::wstring textScratch;

    for (int n = 0; n < visible && first + n < count; ++n) {
        int index = first + n;
        D2D1_RECT_F itemRect = D2D1::RectF(bounds_.x + 4.0f, y + 1.0f,
                                           bounds_.right() - 12.0f, y + itemHeight_ - 1.0f);

        // Selection highlight: accent fill with a left accent bar. In Multiple
        // mode any item in the set gets the highlight, not just the active one.
        if (IsSelected(index)) {
            dc.FillRoundedRect(
                D2D1::RoundedRect(itemRect, itemCorner, itemCorner),
                D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.15f));
            dc.FillRoundedRect(
                D2D1::RoundedRect(
                    D2D1::RectF(itemRect.left, itemRect.top + 5,
                                itemRect.left + 3, itemRect.bottom - 5),
                    2.0f, 2.0f),
                pal.accent);
        }
        // Hover highlight: subtle fill, only when not selected.
        else if (index == hoverIndex_) {
            dc.FillRoundedRect(
                D2D1::RoundedRect(itemRect, itemCorner, itemCorner),
                D2D1::ColorF(pal.textPrimary.r, pal.textPrimary.g,
                             pal.textPrimary.b, 0.05f));
        }

        // Item text.
        const std::wstring& text = GetItemText(index, textScratch);
        D2D1_RECT_F textRect = D2D1::RectF(bounds_.x + 12.0f, y,
                                           bounds_.right() - 16.0f, y + itemHeight_);
        D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
        dc.DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt,
                    textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        y += itemHeight_;
    }

    // Scrollbar.
    scroll_.Render(dc);

    // Focus ring.
    if (IsFocused()) {
        const auto accent = EffectiveAccentColor(pal.accent);
        dc.DrawRoundedRect(
            D2D1::RoundedRect(
                D2D1::RectF(bounds_.x + 0.5f, bounds_.y + 0.5f,
                            bounds_.right() - 0.5f, bounds_.bottom() - 0.5f),
                4.0f, 4.0f),
            D2D1::ColorF(accent.r, accent.g, accent.b, 0.7f), 1.0f);
    }
}

} // namespace fluent
