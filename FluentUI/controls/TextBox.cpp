// TextBox.cpp — single-line specifics on top of TextEditBase.

#include "TextBox.h"
#include "../styling/ThemeTokens.h"
#include "../input/InputManager.h"
#include "../graphics/ResourceCache.h"
#include <algorithm>

namespace fluent {

namespace {
constexpr float kPadX = 10.0f;     // horizontal text padding (DIP)
constexpr float kCaretW = 1.0f;    // caret width (DIP)
} // namespace

void TextBox::Measure(float availW, float availH) {
    LayoutCostProbe::Scope probe(LayoutCostKey::TextBoxMeasure);
    UNREFERENCED_PARAMETER(availH);
    // WinUI TextBox MinHeight is 32 DIP; the content height is one line of body
    // text plus top/bottom padding. Width defaults to the natural text width but
    // most real usages pin it with SetWidth — when they don't, take the offer.
    const float fontSize = EffectiveFontSize();
    constexpr float kMinH = 32.0f;
    constexpr float kPadY = 6.0f * 2.0f;

    float textW = 0.0f;
    DWriteContext* dwrite = Dwrite();
    if (dwrite && !text_.empty()) {
        if (IDWriteTextFormat* fmt = dwrite->Format(fontSize)) {
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dwrite->Factory()->CreateTextLayout(
                    text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                    1e6f, 1e6f, layout.GetAddressOf()))) {
                DWRITE_TEXT_METRICS m{};
                if (SUCCEEDED(layout->GetMetrics(&m)))
                    textW = m.widthIncludingTrailingWhitespace;
            }
        }
    }

    const float naturalW = textW > 0.0f ? textW + kPadX * 2.0f : 160.0f;
    SetDesired({IsAuto(width_) ? naturalW : width_,
                     IsAuto(height_) ? std::max(kMinH, fontSize + kPadY) : height_});
}

// ---------------------------------------------------------------------------
// Text + layout helpers
// ---------------------------------------------------------------------------

std::wstring TextBox::GeometryText() const {
    return password_ ? std::wstring(text_.size(), L'\x2022') : text_;
}

const std::wstring& TextBox::DisplayText(std::wstring& scratch) const {
    // Plain (non-password) and not composing: the layout string is the buffer, so hand
    // back a reference without copying. Password masking or an active composition both
    // need a built string, which goes in the caller's scratch buffer.
    if (!password_ && composition_.empty()) return text_;
    scratch = GeometryText();
    if (!composition_.empty())
        scratch.insert(std::min<size_t>(caret_, scratch.size()), composition_);
    return scratch;
}

std::wstring TextBox::SanitizeInput(std::wstring s) const {
    // Single-line: cut at the first newline (matches the old paste behavior and
    // also guards against a pasted multi-line string arriving via IME).
    size_t nl = s.find_first_of(L"\r\n");
    if (nl != std::wstring::npos) s.resize(nl);
    return s;
}

ComPtr<IDWriteTextLayout> TextBox::CreateLayout(const std::wstring& s) const {
    if (!Dwrite()) return {};

    // Route through the shared, epoch-versioned layout cache (roadmap §13.3):
    // Render + CaretX + HitIndex + ClampScroll all lay out the same string each
    // frame, so caching keyed on (text, size, height) turns 4-6 identical builds
    // per frame into one. The key uses a fixed unbounded width (single line never
    // wraps) and the box height, matching CreateTextLayout's args below.
    // One resolved weight for both paths below: the cache key and the cache-less
    // fallback must agree, or a cache hit would serve a layout built at a weight the
    // fallback would have drawn differently.
    const DWRITE_FONT_WEIGHT weight = EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL);

    if (ResourceCache* cache = Context().resourceCache) {
        TextLayoutKey key;
        key.text = s;
        key.fontSize = EffectiveFontSize();
        key.weight = weight;
        key.textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
        key.paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        key.wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
        key.maxWidth = 100000.0f;
        key.maxHeight = std::max(1.0f, bounds_.h);
        return cache->GetTextLayout(std::move(key));
    }

    // Fallback for a detached / cache-less context (headless tests): build directly.
    IDWriteTextFormat* fmt = Dwrite()->Format(
        EffectiveFontSize(), weight,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return {};
    ComPtr<IDWriteTextLayout> layout;
    Dwrite()->Factory()->CreateTextLayout(
        s.c_str(), static_cast<UINT32>(s.size()), fmt, 100000.0f,
        std::max(1.0f, bounds_.h), layout.GetAddressOf());
    return layout;
}

float TextBox::CaretX(UINT32 index) const {
    // DisplayText, not GeometryText: the caret index callers pass is
    // caret_ + composition_.size(), which points PAST an inline IME composition. Measured
    // against a string that excludes the composition, that index is out of range and gets
    // clamped to the end — so the caret X stopped advancing while composing, and the IME
    // candidate window (placed from CaretRectDip, which calls this) sat frozen at the
    // position where composition began.
    //
    // Render() was already using DisplayText for the drawn caret, so the on-screen caret
    // moved correctly and only the candidate window lagged. That split is why this went
    // unnoticed: the visible caret and the thing positioned off it disagreed.
    std::wstring scratch;
    const std::wstring& disp = DisplayText(scratch);
    ComPtr<IDWriteTextLayout> layout = CreateLayout(disp);
    if (!layout) return 0.0f;
    DWRITE_HIT_TEST_METRICS hm{};
    float x = 0.0f, y = 0.0f;
    index = std::min<UINT32>(index, static_cast<UINT32>(disp.size()));
    layout->HitTestTextPosition(index, FALSE, &x, &y, &hm);
    return x;
}

UINT32 TextBox::HitIndex(float dipX) const {
    std::wstring disp = GeometryText();
    ComPtr<IDWriteTextLayout> layout = CreateLayout(disp);
    if (!layout) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm{};
    float localX = dipX - (bounds_.x + ContentLeft()) + scrollX_;
    layout->HitTestPoint(localX, bounds_.h * 0.5f, &trailing, &inside, &hm);
    return hm.textPosition + (trailing ? hm.length : 0);
}

float TextBox::ContentLeft() const { return kPadX; }
float TextBox::ContentWidth() const { return std::max(1.0f, bounds_.w - kPadX * 2.0f); }

void TextBox::ClampScroll() {
    float textW = CaretX(static_cast<UINT32>(text_.size()));
    float maxScroll = std::max(0.0f, textW - ContentWidth());
    scrollX_ = std::clamp(scrollX_, 0.0f, maxScroll);
}

void TextBox::EnsureCaretVisible() {
    float cx = CaretX(caret_);
    float viewLeft = scrollX_;
    float viewRight = scrollX_ + ContentWidth();
    if (cx < viewLeft) scrollX_ = cx;
    else if (cx > viewRight) scrollX_ = cx - ContentWidth();
    ClampScroll();
}

// ---------------------------------------------------------------------------
// Navigation (single-line: left/right + Home/End)
// ---------------------------------------------------------------------------

bool TextBox::OnNavigationKey(UINT vk, bool shift) {
    switch (vk) {
        case VK_LEFT:
            if (HasSelection() && !shift) MoveCaret(std::min(caret_, selAnchor_), false);
            else MoveCaret(caret_ > 0 ? caret_ - 1 : 0, shift);
            return true;
        case VK_RIGHT:
            if (HasSelection() && !shift) MoveCaret(std::max(caret_, selAnchor_), false);
            else MoveCaret(caret_ + 1, shift);
            return true;
        case VK_HOME: MoveCaret(0, shift); return true;
        case VK_END:  MoveCaret(static_cast<UINT32>(text_.size()), shift); return true;
        default:      return false;  // let the window handle Tab etc.
    }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void TextBox::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    const UINT32 index = HitIndex(e.position.x);
    // Double-click selects the word, triple selects the whole (single-line) buffer.
    // ApplyMultiClickSelection also records the drag granularity, so the move handler
    // below extends by whole words after a double-click.
    if (!ApplyMultiClickSelection(index, e.clickCount)) {
        caret_ = selAnchor_ = index;
        BeginCharacterDrag(index);
    }
    selecting_ = true;
    if (Context().input) Context().input->CapturePointer(this);
    ResetBlink();
    Invalidate();
    e.handled = true;
}

void TextBox::OnPointerMoved(PointerEventArgs& e) {
    if (!selecting_) return;
    ExtendDragSelection(HitIndex(e.position.x));
    EnsureCaretVisible();
    Invalidate();
    e.handled = true;
}

void TextBox::OnPointerReleased(PointerEventArgs& e) {
    if (!selecting_) return;
    selecting_ = false;
    if (Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    e.handled = true;
}

bool TextBox::CaretRectDip(RectDip& out) const {
    // Caret position including any inline composition string before it.
    UINT32 idx = caret_ + static_cast<UINT32>(composition_.size());
    float cx = bounds_.x + ContentLeft() + CaretX(idx) - scrollX_;
    out = {cx, bounds_.y + 4.0f, kCaretW, bounds_.h - 8.0f};
    return true;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void TextBox::Render(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;
    const D2D1_COLOR_F accent = EffectiveAccentColor(pal.accent);
    const D2D1_COLOR_F ink = EffectiveForeground(pal.textPrimary);

    D2D1_RECT_F box = D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom());
    const float corner = EffectiveCornerRadius();
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box, corner, corner);

    // Fill + border (accent border when focused; the focused stroke is deliberately
    // heavier, so the user's BorderThickness only replaces the resting weight).
    dc.FillRoundedRect(rr, EffectiveBackground(pal.controlFillDefault));
    dc.DrawRoundedRect(rr, IsFocused() ? accent : EffectiveBorderBrush(pal.controlStrokeDefault),
                       IsFocused() ? 1.5f : EffectiveBorderThickness(1.0f));

    float originX = bounds_.x + ContentLeft() - scrollX_;

    // Clip scoped to a block so it pops before the function returns (the guard
    // replaces the old two manual PopAxisAlignedClip paths).
    {
        ClipGuard clip = dc.PushClip(
            D2D1::RectF(bounds_.x + ContentLeft(), bounds_.y,
                        bounds_.right() - ContentLeft(), bounds_.bottom()));

        std::wstring scratch;
        const std::wstring& disp = DisplayText(scratch);

        // Placeholder when empty and not composing.
        if (disp.empty() && !placeholder_.empty()) {
            if (ComPtr<IDWriteTextLayout> ph = CreateLayout(placeholder_)) {
                dc.DrawTextLayout(D2D1::Point2F(originX, bounds_.y), ph.Get(),
                                  pal.textSecondary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        } else {
            ComPtr<IDWriteTextLayout> layout = CreateLayout(disp);
            if (layout) {
                // Selection highlight (only when focused and a range exists).
                if (IsFocused() && HasSelection() && composition_.empty()) {
                    UINT32 start = 0, len = 0;
                    SelectionRange(start, len);
                    UINT32 actual = 0;
                    layout->HitTestTextRange(start, len, originX, bounds_.y, nullptr, 0, &actual);
                    if (actual > 0) {
                        std::vector<DWRITE_HIT_TEST_METRICS> m(actual);
                        if (SUCCEEDED(layout->HitTestTextRange(start, len, originX, bounds_.y,
                                                               m.data(), actual, &actual))) {
                            D2D1_COLOR_F sel = D2D1::ColorF(accent.r, accent.g, accent.b, 0.30f);
                            for (UINT32 i = 0; i < actual; ++i)
                                dc.FillRect(D2D1::RectF(m[i].left, m[i].top,
                                                        m[i].left + m[i].width,
                                                        m[i].top + m[i].height), sel);
                        }
                    }
                }

                // Underline the IME composition substring so it reads as "in progress".
                if (!composition_.empty()) {
                    UINT32 compStart = caret_;
                    UINT32 actual = 0;
                    layout->HitTestTextRange(compStart, static_cast<UINT32>(composition_.size()),
                                             originX, bounds_.y, nullptr, 0, &actual);
                    if (actual > 0) {
                        std::vector<DWRITE_HIT_TEST_METRICS> m(actual);
                        if (SUCCEEDED(layout->HitTestTextRange(compStart, static_cast<UINT32>(composition_.size()),
                                                               originX, bounds_.y, m.data(), actual, &actual))) {
                            for (UINT32 i = 0; i < actual; ++i)
                                dc.FillRect(D2D1::RectF(m[i].left, m[i].top + m[i].height - 2.0f,
                                                        m[i].left + m[i].width,
                                                        m[i].top + m[i].height), accent);
                        }
                    }
                }

                dc.DrawTextLayout(D2D1::Point2F(originX, bounds_.y), layout.Get(),
                                  ink, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }

        }

        // Caret (focused, visible phase). Placed after any inline composition, and
        // deliberately OUTSIDE the placeholder/text branch above.
        //
        // It used to live inside the else-branch, so an empty box that had a placeholder
        // took the first branch and drew NO caret -- clicking such a field looked like it
        // had not taken focus at all. An empty box with no placeholder happened to work
        // (it fell through to the else and laid out an empty string), which is why this
        // survived: the demo's fields all have placeholders and the tests used boxes
        // without them.
        if (IsFocused() && caretVisible_) {
            UINT32 idx = caret_ + static_cast<UINT32>(composition_.size());
            float cx = originX + CaretX(idx);
            dc.FillRect(
                D2D1::RectF(cx, bounds_.y + 4.0f, cx + kCaretW, bounds_.bottom() - 4.0f),
                ink);
        }
    }
}

} // namespace fluent
