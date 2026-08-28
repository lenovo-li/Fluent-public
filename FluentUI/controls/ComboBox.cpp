// ComboBox.cpp

#include "ComboBox.h"
#include "../window/WindowServices.h"
#include "../services/PopupHost.h"
#include "../services/PopupGeometry.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include "../core/ScrollMath.h"
#include "../graphics/ResourceCache.h"
#include <algorithm>

namespace fluent {

namespace {
const char* kTag = "ComboBox";
constexpr float kCornerRadius = 4.0f;  // DIP
constexpr float kChevronSize = 8.0f;   // DIP, chevron glyph width

// The header's focus ring: one spec shared by the paint (Render) and the dirty rect
// (CollectDirtyBounds), so the reported region can never drift from what is drawn.
FocusRingSpec HeaderFocusRing() {
    FocusRingSpec spec;              // inset 2 / stroke 1.5 are the defaults
    spec.cornerRadius = kCornerRadius;
    return spec;
}

// Private list view element for the popup content. Renders items with hover
// and selected states, handles pointer and keyboard navigation, and fires a
// callback when an item is clicked or Enter is pressed.
class ComboListView : public Control {
public:
    ComboListView() { SetClickable(true); }
    // Bind to the ComboBox's shared items vector (not owned) — eliminates the
    // WP-05 double storage. The pointer stays valid for the ComboBox's life
    // (the list view is owned by, and destroyed with, the ComboBox).
    void SetItemsRef(const std::vector<std::wstring>* items) { items_ = items; Invalidate(); }
    void SetSelectedIndex(int idx) { selectedIndex_ = idx; Invalidate(); }
    void SetOnSelect(std::function<void(int)> cb) { onSelect_ = std::move(cb); }

    // Number of bound items (0 when unbound).
    int Count() const { return items_ ? static_cast<int>(items_->size()) : 0; }

    // Measure to fit all items (capped at a max height for scrolling).
    void Measure(float availW, float availH) override {
        UNREFERENCED_PARAMETER(availH);
        float itemH = 32.0f;
        float maxH = itemH * 8.0f;  // cap at 8 items visible
        float w = IsAuto(width_) ? std::min(availW, 300.0f) : width_;
        float h = std::min(itemH * Count(), maxH);
        SetDesired({w, h});
    }

    void OnKeyDownRouted(KeyEventArgs& e) override {
        if (Count() == 0) return;
        int idx = hoveredIndex_ >= 0 ? hoveredIndex_ : selectedIndex_;
        if (idx < 0) idx = 0;

        switch (e.vk) {
            case VK_UP:
                hoveredIndex_ = std::max(0, idx - 1);
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_DOWN:
                hoveredIndex_ = std::min(Count() - 1, idx + 1);
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_HOME:
                hoveredIndex_ = 0;
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_END:
                hoveredIndex_ = Count() - 1;
                EnsureVisible(hoveredIndex_);
                Invalidate();
                e.handled = true; break;
            case VK_RETURN:
            case VK_SPACE:
                if (hoveredIndex_ >= 0 && hoveredIndex_ < Count()) {
                    if (onSelect_) onSelect_(hoveredIndex_);
                }
                e.handled = true; break;
            default:
                break;
        }
    }

    void OnPointerWheelChanged(PointerEventArgs& e) override {
        if (Count() == 0) return;
        // Scroll by ~3 items per wheel notch.
        float itemH = 32.0f;
        float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
        scrollOffset_ += lines * itemH;
        scrollOffset_ = std::clamp(scrollOffset_, 0.0f, MaxScroll());
        Invalidate();
        e.handled = true;
    }

    void Render(const DrawingContext& dc) override {
        if (!Dwrite() || !items_ || items_->empty()) return;
        const auto& items = *items_;
        const ColorTokens& pal = Theme().colors;

        IDWriteTextFormat* fmt = Dwrite()->Format(
            13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return;

        ClipGuard clip = dc.PushClip(
            D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

        float itemH = 32.0f;
        int first = static_cast<int>(scrollOffset_ / itemH);
        int visible = static_cast<int>(bounds_.h / itemH) + 2;
        float y = bounds_.y - (scrollOffset_ - first * itemH);

        for (int i = first; i < static_cast<int>(items.size()) && i < first + visible; ++i) {
            D2D1_RECT_F itemRect = D2D1::RectF(bounds_.x + 4.0f, y, bounds_.right() - 4.0f, y + itemH);

            if (i == hoveredIndex_) {
                dc.FillRoundedRect(D2D1::RoundedRect(itemRect, Theme().spacing.cornerRadiusSmall, Theme().spacing.cornerRadiusSmall),
                                   D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.15f));
            } else if (i == selectedIndex_) {
                dc.FillRoundedRect(D2D1::RoundedRect(itemRect, Theme().spacing.cornerRadiusSmall, Theme().spacing.cornerRadiusSmall),
                                   D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.08f));
            }

            D2D1_RECT_F textRect = D2D1::RectF(itemRect.left + 8.0f, itemRect.top,
                                               itemRect.right - 8.0f, itemRect.bottom);
            D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
            dc.DrawText(items[i].c_str(), static_cast<UINT32>(items[i].size()), fmt,
                        textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

            if (i == selectedIndex_) {
                dc.FillRoundedRect(
                    D2D1::RoundedRect(D2D1::RectF(itemRect.left + 2.0f, itemRect.top + 8.0f,
                                                  itemRect.left + 4.0f, itemRect.bottom - 8.0f),
                                      1.0f, 1.0f),
                    pal.accent);
            }
            y += itemH;
        }
    }

    void OnPointerMoved(PointerEventArgs& e) override {
        int idx = HitItem(e.position.y);
        if (idx != hoveredIndex_) { hoveredIndex_ = idx; Invalidate(); }
    }

    void OnClickRouted(PointerEventArgs& e) override {
        int idx = HitItem(e.position.y);
        if (idx >= 0 && idx < Count() && onSelect_) onSelect_(idx);
    }

private:
    int HitItem(float dipY) const {
        float itemH = 32.0f;
        float y = dipY - bounds_.y + scrollOffset_;
        int idx = static_cast<int>(y / itemH);
        return (idx >= 0 && idx < Count()) ? idx : -1;
    }

    void EnsureVisible(int idx) {
        if (idx < 0 || idx >= Count()) return;
        float itemH = 32.0f;
        scrollOffset_ = EnsureVisibleOffset(idx * itemH, itemH, scrollOffset_, bounds_.h);
        scrollOffset_ = std::clamp(scrollOffset_, 0.0f, MaxScroll());
    }

    float MaxScroll() const {
        float itemH = 32.0f;
        return std::max(0.0f, itemH * Count() - bounds_.h);
    }

    const std::vector<std::wstring>* items_ = nullptr;  // non-owning; points to Selector::items_
    int selectedIndex_ = -1;
    int hoveredIndex_ = -1;
    float scrollOffset_ = 0.0f;
    std::function<void(int)> onSelect_;
};

} // namespace

// ---------------------------------------------------------------------------
// ComboBox implementation
// ---------------------------------------------------------------------------

ComboBox::ComboBox() {
    SetFocusable(true);
    SetClickable(true);  // header toggles the dropdown via the base click gesture
}

ComboBox::~ComboBox() {
    // Close the dropdown and unregister the window's dismiss hook before the
    // members are torn down, so the window can never invoke a [this] callback
    // into a half-destroyed ComboBox. Idempotent; safe if already closed.
    ClosePopup();
}

void ComboBox::OnAttachedToTree() {
    // The framework injects the host on tree attach (roadmap §6.2), replacing the
    // old manual Attach(WindowServices*). Cache the window for the attach period
    // (popup positioning + dismiss registration read it); it is cleared on detach.
    window_ = Context().window;
    if (!window_) return;

    // Create the popup host (hidden, reused on each open).
    popup_ = std::make_unique<PopupHost>();
    if (FAILED(popup_->Create(window_->Instance(), window_->Hwnd(),
                              &window_->D2D(), &window_->DWrite()))) {
        TraceMsg(kTag, "OnAttachedToTree: PopupHost::Create failed");
        popup_.reset();
        return;
    }
    // Share the tree's resource caches with the dropdown content (§13.3).
    popup_->SetResourceCache(Context().resourceCache);
    // Apply any opacity set before attach (see SetPopupOpacity).
    popup_->SetCardOpacity(popupOpacity_);
    // Share the tree's stable theme snapshot so the dropdown + its rows theme
    // identically to the parent (roadmap §11, WP-05).
    popup_->SetTheme(Context().theme);

    // Create the list view content. It reads DWrite through the context PopupHost
    // injects on SetContent (roadmap §6.2). The list binds to the shared base
    // items_ vector (no copy — eliminates the WP-05 double storage).
    auto* list = new ComboListView();
    list->SetItemsRef(&items_);
    list->SetSelectedIndex(selectedIndex_);
    list->SetOnSelect([this](int idx) { OnItemSelected(idx); });
    listView_.reset(list);
    popup_->SetContent(listView_.get());
    popup_->SetOnClose([this]() {
        popupOpen_ = false;
        dismissSub_.reset();  // unregister the window's dismiss hook
        Invalidate();
    });

    TraceMsg(kTag, "OnAttachedToTree: popup and list view created");
}

void ComboBox::OnDetachedFromTree() {
    // Tear down the popup and its window registrations while the host is still
    // valid, then drop the cached window pointer (the context is about to clear).
    ClosePopup();
    popup_.reset();
    listView_.reset();
    window_ = nullptr;
}

void ComboBox::OnItemsChanged() {
    // ItemsControl hook: items_ was replaced. Re-clamp the selection to the new
    // range and refresh the (already items_-bound) list view's selected index.
    selectedIndex_ = ClampIndex(selectedIndex_);
    if (auto* list = dynamic_cast<ComboListView*>(listView_.get()))
        list->SetSelectedIndex(selectedIndex_);
    Invalidate();
}

void ComboBox::SetSelectedIndex(int index) {
    // Reuse the base clamp/dedup + OnSelectionChanged hook, then sync the list view.
    Selector<std::wstring>::SetSelectedIndex(index);
    if (auto* list = dynamic_cast<ComboListView*>(listView_.get()))
        list->SetSelectedIndex(selectedIndex_);
}

void ComboBox::OnSelectionChanged(int /*oldIdx*/, int newIdx) {
    // In editable mode the header draws text_, not SelectedItem(), so a selection change
    // has to write the chosen item into the field or the control looks inert: picking
    // from the dropdown moved selectedIndex_ while the box kept showing the typed text.
    //
    // Only on a REAL item. Clearing the selection (-1) must not wipe a partially typed
    // entry -- the user is mid-edit and has selected nothing, which is not the same as
    // having selected an empty string.
    //
    // The caret goes to the end so typing continues naturally after a pick, matching what
    // SetEditable(true) does when it seeds the field from an existing selection.
    if (editable_) {
        if (const std::wstring* sel = SelectedItem()) {
            text_ = *sel;
            caret_ = static_cast<UINT32>(text_.size());
        }
    }
    // Selector hook: fires on any real selection change (programmatic or user).
    selectionChanged_.Raise(*this, newIdx);
}

void ComboBox::SetPopupOpacity(float opacity) {
    // Store first, then push if the popup already exists. Storing unconditionally is
    // what makes a pre-attach call work; without it the setting was dropped on the
    // floor whenever a page configured the control before adding it to the tree.
    popupOpacity_ = std::clamp(opacity, 0.0f, 1.0f);
    if (popup_) popup_->SetCardOpacity(popupOpacity_);
}

void ComboBox::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    // Measure like a button: explicit size or a reasonable default.
    SetDesired({IsAuto(width_) ? std::min(availW, 200.0f) : width_,
                     IsAuto(height_) ? 32.0f : height_});
}

// ---------------------------------------------------------------------------
// Editable mode
// ---------------------------------------------------------------------------

const std::wstring ComboBox::emptyString_;

void ComboBox::SetEditable(bool editable) {
    if (editable_ == editable) return;
    editable_ = editable;
    if (editable_) {
        // Seed the field from the current selection so turning editing on does not
        // blank a combo that already had a value.
        if (const std::wstring* sel = SelectedItem()) text_ = *sel;
        caret_ = static_cast<UINT32>(text_.size());
    } else {
        // Leaving editable mode drops the typed text: the value is once again
        // whatever the selection says, and keeping a stale field would make Text()
        // disagree with what is drawn.
        text_.clear();
        caret_ = 0;
    }
    caretVisible_ = false;
    InvalidateDirty(DirtyFlags::Render);
}

std::wstring ComboBox::Text() const { return HeaderText(); }

const std::wstring& ComboBox::HeaderText() const {
    if (editable_) return text_;
    if (const std::wstring* sel = SelectedItem()) return *sel;
    return emptyString_;
}

void ComboBox::SetText(std::wstring text) {
    if (!editable_) return;
    if (text_ == text) return;
    text_ = std::move(text);
    caret_ = static_cast<UINT32>(text_.size());
    // Programmatic, so no TextChanged — that event means "the user typed".
    InvalidateDirty(DirtyFlags::Render);
}

void ComboBox::ClampCaret() {
    caret_ = std::min<UINT32>(caret_, static_cast<UINT32>(text_.size()));
}

void ComboBox::InsertEditText(const std::wstring& s) {
    if (s.empty()) return;
    ClampCaret();
    text_.insert(caret_, s);
    caret_ += static_cast<UINT32>(s.size());
    caretVisible_ = true;
    InvalidateDirty(DirtyFlags::Render);
    std::wstring snapshot = text_;
    textChanged_.Raise(*this, snapshot);
}

void ComboBox::OnTextInput(wchar_t ch) {
    if (!editable_) return;
    // Reject control characters; Backspace/Delete/Enter arrive as key-downs.
    if (ch < 0x20 || ch == 0x7F) return;
    InsertEditText(std::wstring(1, ch));
}

bool ComboBox::HandleEditKey(UINT vk) {
    if (!editable_) return false;
    ClampCaret();
    switch (vk) {
    case VK_BACK:
        if (caret_ == 0) return true;  // consumed, nothing to erase
        text_.erase(caret_ - 1, 1);
        --caret_;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        {
            std::wstring snapshot = text_;
            textChanged_.Raise(*this, snapshot);
        }
        return true;
    case VK_DELETE:
        if (caret_ >= text_.size()) return true;
        text_.erase(caret_, 1);
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        {
            std::wstring snapshot = text_;
            textChanged_.Raise(*this, snapshot);
        }
        return true;
    case VK_LEFT:
        if (caret_ > 0) --caret_;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    case VK_RIGHT:
        // Right at the end of the text is NOT consumed in a way that reaches the
        // dropdown: swallow it so a caret move never doubles as "open the list".
        if (caret_ < text_.size()) ++caret_;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    case VK_HOME:
        caret_ = 0;
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    case VK_END:
        caret_ = static_cast<UINT32>(text_.size());
        caretVisible_ = true;
        InvalidateDirty(DirtyFlags::Render);
        return true;
    default:
        return false;
    }
}

void ComboBox::OnBlink() {
    if (!editable_ || !IsFocused()) {
        if (caretVisible_) { caretVisible_ = false; InvalidateDirty(DirtyFlags::Render); }
        return;
    }
    caretVisible_ = !caretVisible_;
    InvalidateDirty(DirtyFlags::Render);
}

HCURSOR ComboBox::Cursor() const {
    if (!editable_) return nullptr;  // window default (arrow)
    static HCURSOR ibeam = LoadCursor(nullptr, IDC_IBEAM);
    return ibeam;
}

RECT ComboBox::HeaderScreenRect() const {
    RECT rcWindow;
    GetWindowRect(window_->Hwnd(), &rcWindow);
    return AnchorScreenRect(rcWindow.left, rcWindow.top,
                            bounds_.x, bounds_.y, bounds_.w, bounds_.h,
                            window_->DpiScale());
}

bool ComboBox::HeaderContainsScreenPoint(int screenX, int screenY) const {
    if (!window_) return false;
    RECT r = HeaderScreenRect();
    POINT pt = {screenX, screenY};
    return PtInRect(&r, pt) != FALSE;
}

void ComboBox::OpenPopup() {
    if (!popup_ || !window_ || popupOpen_) return;

    // Compute the anchor rect in screen coordinates (header bounds).
    RECT anchor = HeaderScreenRect();

    // Popup width matches the header; height fits the list (capped by Measure).
    float popupW = bounds_.w;
    float itemH = 32.0f;
    float maxH = itemH * 8.0f;
    float popupH = std::min(itemH * items_.size(), maxH) + 16.0f;  // +padding

    if (SUCCEEDED(popup_->Open(anchor, popupW, popupH))) {
        popupOpen_ = true;
        if (window_) {
            dismissSub_ = window_->RegisterActivePopupDismiss([this](PopupDismissReason reason, HWND otherHwnd, int screenX, int screenY) {
                // For deactivate, check if the other HWND is our popup. If so, ignore.
                if (reason == PopupDismissReason::Deactivate) {
                    if (popup_ && otherHwnd == popup_->Hwnd())
                        return false;  // keep popup open
                }
                // For click dismiss, keep the popup open when the click lands
                // inside the popup window (the list handles it) OR on our own
                // header (so the header's OnClick can toggle the popup closed
                // instead of this callback closing it and OnClick reopening it
                // on the same click).
                if (reason == PopupDismissReason::Click) {
                    if (popup_ && popup_->ContainsScreenPoint(screenX, screenY))
                        return false;  // keep popup open
                    if (window_ && HeaderContainsScreenPoint(screenX, screenY))
                        return false;  // let the header toggle handle it
                }
                // For other reasons (deactivate/move/resize) or clicks outside, dismiss.
                ClosePopup();
                return true;  // dismissed
            });
        }
        TraceMsg(kTag, "OpenPopup: popup shown");
    } else {
        TraceMsg(kTag, "OpenPopup: PopupHost::Open failed");
    }
}

void ComboBox::ClosePopup() {
    if (!popup_ || !popupOpen_) return;
    popup_->Close();
    popupOpen_ = false;
    dismissSub_.reset();  // unregister the window's dismiss hook
    Invalidate();
}

void ComboBox::OnItemSelected(int index) {
    // SetSelectedIndex raises SelectionChanged via the Selector hook when the
    // index actually changes; do not raise again here.
    SetSelectedIndex(index);
    ClosePopup();
}

void ComboBox::OnKeyDownRouted(KeyEventArgs& e) {
    UINT vk = e.vk;

    // Editable-mode editing keys take precedence over dropdown navigation so
    // Left/Right/Home/End never open the popup when a caret is moving.
    if (editable_ && HandleEditKey(vk)) {
        e.handled = true;
        return;
    }

    // Forward navigation keys to the popup if it's open.
    if (popupOpen_ && popup_) {
        if (vk == VK_ESCAPE) {
            ClosePopup();
            e.handled = true;
            return;
        }
        if (popup_->ForwardKey(vk)) {
            e.handled = true;
            return;
        }
    }

    // Open the popup with Space/Enter/Down/Alt+Down.
    if (!popupOpen_) {
        bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (vk == VK_SPACE || vk == VK_RETURN || vk == VK_DOWN ||
            (vk == VK_DOWN && altDown)) {
            OpenPopup();
            e.handled = true;
            return;
        }
    }
}

void ComboBox::OnClickRouted(PointerEventArgs&) {
    if (popupOpen_)
        ClosePopup();
    else
        OpenPopup();
}

void ComboBox::OnStateChanged() {
    Invalidate();
}

void ComboBox::Render(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;

    const float corner = EffectiveCornerRadius(kCornerRadius);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);

    // Fill: state-driven for hover/press feedback.
    D2D1_COLOR_F fill;
    switch (State()) {
        case VisualState::Hover:   fill = pal.controlFillHover; break;
        case VisualState::Pressed: fill = pal.controlFillPressed; break;
        default:                   fill = pal.controlFillDefault; break;
    }

    dc.FillRoundedRect(rr, EffectiveBackground(fill));

    // Border.
    dc.DrawRoundedRect(rr, EffectiveBorderBrush(pal.controlStrokeDefault),
                       EffectiveBorderThickness(1.0f));

    // Header text: either typed text (editable) or selected item (readonly).
    // The same format and layout is used in both modes, only the source differs.
    if (Dwrite()) {
        const std::wstring& displayText = HeaderText();
        if (!displayText.empty()) {
            if (IDWriteTextFormat* fmt = Dwrite()->Format(
                    13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                    DWRITE_WORD_WRAPPING_NO_WRAP)) {
                D2D1_RECT_F textRect = D2D1::RectF(bounds_.x + 10.0f, bounds_.y,
                                                   bounds_.right() - 24.0f, bounds_.bottom());
                D2D1_COLOR_F textColor = EffectiveForeground(pal.textPrimary);
                dc.DrawText(displayText.c_str(), static_cast<UINT32>(displayText.size()),
                            fmt, textRect, textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

                // Caret (editable mode only, visible phase only).
                if (editable_ && IsFocused() && caretVisible_) {
                    ClampCaret();
                    ComPtr<IDWriteTextLayout> layout;
                    if (SUCCEEDED(Dwrite()->Factory()->CreateTextLayout(
                            displayText.c_str(), static_cast<UINT32>(displayText.size()),
                            fmt, textRect.right - textRect.left,
                            textRect.bottom - textRect.top, &layout))) {
                        DWRITE_HIT_TEST_METRICS htm;
                        float caretX, caretY;
                        if (SUCCEEDED(layout->HitTestTextPosition(caret_, false,
                                                                   &caretX, &caretY, &htm))) {
                            float x = textRect.left + caretX;
                            float y1 = textRect.top + caretY;
                            float y2 = y1 + htm.height;
                            dc.DrawLine(D2D1::Point2F(x, y1), D2D1::Point2F(x, y2),
                                        textColor, 1.0f);
                        }
                    }
                }
            }
        }
    }

    // Chevron (down arrow) on the right. The glyph is a static "v" shape, so it
    // is built once as a unit-space path geometry in the shared cache and drawn
    // here positioned by a transform — instead of re-emitting line primitives
    // every frame (roadmap §13.3, WP-04). Falls back to primitives if no cache.
    float cx = bounds_.right() - 16.0f;
    float cy = bounds_.y + bounds_.h * 0.5f;
    ResourceCache* cache = Context().resourceCache;
    ComPtr<ID2D1PathGeometry> chevron;
    if (cache) {
        const float hw = kChevronSize * 0.5f;
        chevron = cache->GetGeometry(GlyphId::ChevronDown, kChevronSize,
            [hw](ID2D1GeometrySink* sink) {
                // Unit space centered on the glyph origin; positioned by transform.
                sink->BeginFigure(D2D1::Point2F(-hw, -2.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(0.0f, 2.0f));
                sink->AddLine(D2D1::Point2F(hw, -2.0f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            });
    }
    if (chevron) {
        TransformGuard tg = dc.PushTransform(D2D1::Matrix3x2F::Translation(cx, cy));
        dc.DrawGeometry(chevron.Get(), pal.textPrimary, 1.5f);
    } else {
        dc.DrawLine(D2D1::Point2F(cx - kChevronSize * 0.5f, cy - 2.0f),
                    D2D1::Point2F(cx, cy + 2.0f), pal.textPrimary, 1.5f);
        dc.DrawLine(D2D1::Point2F(cx, cy + 2.0f),
                    D2D1::Point2F(cx + kChevronSize * 0.5f, cy - 2.0f), pal.textPrimary, 1.5f);
    }

    // Focus ring. Routed through the shared helper (same geometry as the hand-rolled
    // version it replaces: inset 2, corner kCornerRadius+2, stroke 1.5) so
    // CollectDirtyBounds can derive its pad from the same spec.
    if (IsFocused()) {
        dc.DrawRoundedRect(FocusRingRect(bounds_, HeaderFocusRing()), pal.accent,
                           HeaderFocusRing().strokeWidth);
    }
}

float ComboBox::VisualOverflowDip() const {
    return FocusRingPadDip(HeaderFocusRing());
}

} // namespace fluent
