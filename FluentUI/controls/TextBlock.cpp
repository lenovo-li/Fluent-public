// TextBlock.cpp

#include "TextBlock.h"
#include "../styling/ThemeTokens.h"
#include "../input/InputManager.h"
#include "../diagnostics/LayoutCostProbe.h"
#include <algorithm>

namespace fluent {

float TextBlock::ResolveTypographySize() const
{
    const TypographyTokens& t = Theme().typography;
    switch (role_) {
        case TypographyRole::Caption:  return t.captionSize;
        case TypographyRole::Body:     return t.bodySize;
        case TypographyRole::Subtitle: return t.subtitleSize;
        case TypographyRole::Title:    return t.titleSize;
        case TypographyRole::Custom:   break;
    }
    return fontSize_;
}

void TextBlock::Measure(float availW, float availH)
{
    LayoutCostProbe::Scope probe(LayoutCostKey::TextBlockMeasure);
    UNREFERENCED_PARAMETER(availH);
    // Size to content: measure the text at the offered width and report its
    // natural height (honoring an explicit width/height override if set).
    float wrapW = IsAuto(width_) ? (availW > 0 ? availW : 1.0f) : width_;

    const float fontSize = ResolveTypographySize();
    float measuredH = fontSize * 1.4f;  // fallback for empty / no-DWrite
    float measuredW = 0.0f;
    DWriteContext* dwrite = Dwrite();
    if (dwrite && !text_.empty()) {
        if (IDWriteTextFormat* fmt = dwrite->Format(
                fontSize, weight_, align_, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                wrap_ ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP)) {
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dwrite->Factory()->CreateTextLayout(
                    text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                    std::max(1.0f, wrapW), 100000.0f, layout.GetAddressOf()))) {
                DWRITE_TEXT_METRICS m{};
                if (SUCCEEDED(layout->GetMetrics(&m))) {
                    measuredH = m.height;
                    measuredW = m.widthIncludingTrailingWhitespace;
                }
            }
        }
    }

    // Auto width reports the text's natural width (clamped to what was offered);
    // Auto height reports the wrapped text height.
    SetDesired({IsAuto(width_) ? std::min(measuredW, wrapW) : width_,
                IsAuto(height_) ? measuredH : height_});
}

void TextBlock::OnBoundsChanged()
{
    LayoutCostProbe::Scope probe(LayoutCostKey::TextBlockBoundsChanged);
    UpdateMetrics();
    SetScrollOffset(scrollOffset_);
}

void TextBlock::UpdateMetrics()
{
    contentHeight_ = 0.0f;
    ComPtr<IDWriteTextLayout> layout = CreateLayout();
    if (!layout) return;

    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics)))
        contentHeight_ = metrics.height;
}

ComPtr<IDWriteTextLayout> TextBlock::CreateLayout() const
{
    DWriteContext* dwrite = Dwrite();
    if (!dwrite || text_.empty() || bounds_.w <= 0.0f) {
        // Nothing to lay out: clear the cache so a later valid state rebuilds.
        cachedLayout_.Reset();
        cachedKey_ = LayoutKey{};
        return {};
    }

    // Reuse the cached layout when every input that defines it is unchanged
    // (roadmap §6.3). This is the whole point: Render/UpdateMetrics/hit-testing
    // call CreateLayout several times per frame, and rebuilding the DWrite layout
    // each time is the hot spot.
    const float fontSize = ResolveTypographySize();
    LayoutKey key;
    key.text = text_;
    key.fontSize = fontSize;
    key.weight = weight_;
    key.align = align_;
    key.wrap = wrap_;
    key.lineSpacing = lineSpacing_;
    key.width = bounds_.w;
    key.valid = true;
    if (cachedLayout_ && key.Matches(cachedKey_)) return cachedLayout_;

    // Immutable shared format for the (size, weight, align, wrap) key. Paragraph
    // alignment is NEAR here (text starts at the top of the box).
    IDWriteTextFormat* fmt = dwrite->Format(
        fontSize, weight_, align_, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
        wrap_ ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!fmt) return {};

    ComPtr<IDWriteTextLayout> layout;
    dwrite->Factory()->CreateTextLayout(
        text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
        std::max(1.0f, bounds_.w), 100000.0f, layout.GetAddressOf());
    if (!layout) return {};

    // Line spacing is per-layout (safe to set on this fresh object), not on the
    // shared format — otherwise it would leak into every other caller of the
    // same (size, weight) key.
    if (lineSpacing_ > 0.0f) {
        float lh = fontSize * lineSpacing_;
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lh, lh * 0.8f);
    }

    cachedLayout_ = layout;
    cachedKey_ = std::move(key);
    return layout;
}

UINT32 TextBlock::HitTextPosition(float dipX, float dipY, bool* trailing) const
{
    ComPtr<IDWriteTextLayout> layout = CreateLayout();
    if (!layout) return 0;

    BOOL isTrailing = FALSE;
    BOOL isInside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    float x = std::clamp(dipX - bounds_.x, 0.0f, std::max(1.0f, bounds_.w));
    float y = std::max(0.0f, dipY - bounds_.y + scrollOffset_);
    if (FAILED(layout->HitTestPoint(x, y, &isTrailing, &isInside, &metrics)))
        return 0;
    if (trailing) *trailing = isTrailing != FALSE;
    return metrics.textPosition + (isTrailing ? metrics.length : 0);
}

void TextBlock::SelectionRange(UINT32& start, UINT32& length) const
{
    start = std::min(selectionAnchor_, selectionCaret_);
    UINT32 end = std::max(selectionAnchor_, selectionCaret_);
    length = end - start;
}

bool TextBlock::HasSelection() const
{
    UINT32 start = 0, length = 0;
    SelectionRange(start, length);
    return length > 0;
}

void TextBlock::ClearSelection()
{
    selectionAnchor_ = selectionCaret_ = 0;
    selecting_ = false;
    Invalidate();
}

bool TextBlock::CopySelectionToClipboard(HWND owner) const
{
    UINT32 start = 0, length = 0;
    SelectionRange(start, length);
    if (length == 0 || start >= text_.size()) return false;

    length = std::min<UINT32>(length, static_cast<UINT32>(text_.size()) - start);
    std::wstring selected = text_.substr(start, length);
    SIZE_T bytes = (selected.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void* dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); return false; }
    memcpy(dst, selected.c_str(), bytes);
    GlobalUnlock(mem);

    if (!OpenClipboard(owner)) { GlobalFree(mem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

void TextBlock::SetScrollOffset(float offsetDip)
{
    float maxOffset = std::max(0.0f, contentHeight_ - bounds_.h);
    float next = std::clamp(offsetDip, 0.0f, maxOffset);
    if (next == scrollOffset_) return;
    scrollOffset_ = next;
    Invalidate();
}

void TextBlock::ScrollBy(float deltaDip)
{
    UpdateMetrics();
    SetScrollOffset(scrollOffset_ + deltaDip);
}

void TextBlock::OnPointerWheelChanged(PointerEventArgs& e)
{
    UpdateMetrics();
    // Nothing to scroll: leave unhandled so the event bubbles to a parent scroller.
    if (contentHeight_ <= bounds_.h) return;
    // One notch (WHEEL_DELTA = 120) scrolls ~3 lines of text.
    float lines = -static_cast<float>(e.wheelDelta) / WHEEL_DELTA * 3.0f;
    SetScrollOffset(scrollOffset_ + lines * ResolveTypographySize() * 1.4f);
    e.handled = true;
}

void TextBlock::OnKeyDownRouted(KeyEventArgs& e)
{
    if (!selectable_) return;
    UpdateMetrics();
    float line = ResolveTypographySize() * 1.4f;
    float page = std::max(line, bounds_.h - line);
    switch (e.vk) {
    case VK_UP:    SetScrollOffset(scrollOffset_ - line); e.handled = true; break;
    case VK_DOWN:  SetScrollOffset(scrollOffset_ + line); e.handled = true; break;
    case VK_PRIOR: SetScrollOffset(scrollOffset_ - page); e.handled = true; break;
    case VK_NEXT:  SetScrollOffset(scrollOffset_ + page); e.handled = true; break;
    case VK_HOME:  SetScrollOffset(0.0f); e.handled = true; break;
    case VK_END:   SetScrollOffset(contentHeight_); e.handled = true; break;
    default:       break;
    }
}

HCURSOR TextBlock::Cursor() const
{
    return selectable_ ? LoadCursor(nullptr, IDC_IBEAM) : nullptr;
}

RectDip TextBlock::ThumbRect() const
{
    if (contentHeight_ <= bounds_.h || bounds_.h <= 0.0f) return {};
    float trackW = 4.0f;
    float ratio = bounds_.h / contentHeight_;
    float thumbH = std::max(24.0f, bounds_.h * ratio);
    float maxOffset = std::max(1.0f, contentHeight_ - bounds_.h);
    float thumbY = bounds_.y + (bounds_.h - thumbH) * (scrollOffset_ / maxOffset);
    return {bounds_.right() - trackW - 3.0f, thumbY, trackW, thumbH};
}

bool TextBlock::HitThumb(float dipX, float dipY) const
{
    return ThumbRect().contains(dipX, dipY);
}

void TextBlock::OnPointerMoved(PointerEventArgs& e)
{
    float dipX = e.position.x, dipY = e.position.y;
    if (selecting_) {
        if (dipY < bounds_.y)
            ScrollBy(std::max(-48.0f, dipY - bounds_.y));
        else if (dipY > bounds_.bottom())
            ScrollBy(std::min(48.0f, dipY - bounds_.bottom()));
        selectionCaret_ = HitTextPosition(dipX, dipY);
        Invalidate();
        e.handled = true;
        return;
    }
    if (!draggingThumb_) return;
    RectDip thumb = ThumbRect();
    float trackRange = std::max(1.0f, bounds_.h - thumb.h);
    float contentRange = std::max(0.0f, contentHeight_ - bounds_.h);
    SetScrollOffset(dragStartOffset_ + (dipY - dragStartY_) * contentRange / trackRange);
    e.handled = true;
}

void TextBlock::OnPointerPressed(PointerEventArgs& e)
{
    if (!selectable_ || e.button != PointerButton::Left) return;
    float dipX = e.position.x, dipY = e.position.y;
    UpdateMetrics();
    if (HitThumb(dipX, dipY)) {
        draggingThumb_ = true;
        dragStartY_ = dipY;
        dragStartOffset_ = scrollOffset_;
        if (Context().input) Context().input->CapturePointer(this);
        e.handled = true;
        return;
    }
    bool trailing = false;
    selectionAnchor_ = selectionCaret_ = HitTextPosition(dipX, dipY, &trailing);
    selecting_ = true;
    if (Context().input) Context().input->CapturePointer(this);
    Invalidate();
    e.handled = true;
}

void TextBlock::OnPointerReleased(PointerEventArgs& e)
{
    bool active = draggingThumb_ || selecting_;
    draggingThumb_ = false;
    selecting_ = false;
    if (active && Context().input && Context().input->Captured() == this)
        Context().input->ReleaseCapture(this);
    if (active) e.handled = true;
}

void TextBlock::Render(const DrawingContext& dc)
{
    if (!Dwrite() || text_.empty()) return;
    UpdateMetrics();
    ComPtr<IDWriteTextLayout> layout = CreateLayout();
    if (!layout) return;

    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;
    D2D1_COLOR_F textColor = EffectiveForeground();
    if (!HasForeground()) {
        // No user override, use semantic state-based color
        textColor = dimmed_ ? pal.textSecondary : pal.textPrimary;
    }

    ClipGuard clip = dc.PushClip(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

    UINT32 selStart = 0, selLen = 0;
    SelectionRange(selStart, selLen);
    if (selLen > 0) {
        UINT32 actual = 0;
        layout->HitTestTextRange(selStart, selLen, bounds_.x, bounds_.y - scrollOffset_, nullptr, 0, &actual);
        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actual);
        if (!metrics.empty() && SUCCEEDED(layout->HitTestTextRange(selStart, selLen,
            bounds_.x, bounds_.y - scrollOffset_, metrics.data(), actual, &actual))) {
            D2D1_COLOR_F selColor = D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.28f);
            for (UINT32 i = 0; i < actual; ++i) {
                const auto& m = metrics[i];
                dc.FillRect(D2D1::RectF(m.left, m.top, m.left + m.width, m.top + m.height), selColor);
            }
        }
    }

    dc.DrawTextLayout(D2D1::Point2F(bounds_.x, bounds_.y - scrollOffset_), layout.Get(),
                      textColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Scrollbar: only in selectable mode (tooltips and static labels never show it).
    if (selectable_ && contentHeight_ > bounds_.h && bounds_.h > 0.0f) {
        RectDip thumb = ThumbRect();
        dc.FillRoundedRect(
            D2D1::RoundedRect(D2D1::RectF(thumb.x, thumb.y, thumb.right(), thumb.bottom()), 2.0f, 2.0f),
            D2D1::ColorF(pal.textPrimary.r, pal.textPrimary.g, pal.textPrimary.b,
                         draggingThumb_ ? 0.55f : (th.dark ? 0.35f : 0.25f)));
    }
}

} // namespace fluent
