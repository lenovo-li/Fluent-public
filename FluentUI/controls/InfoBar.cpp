// InfoBar.cpp — see InfoBar.h.

#include "InfoBar.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/DWriteContext.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/ResourceCache.h"
#include "../styling/FocusVisual.h"
#include "../core/UIContext.h"

#include <algorithm>

namespace fluent {

namespace {
// Layout constants. Chosen to line up with WinUI's InfoBar metrics so a page mixing
// this with other controls does not look off by a few DIPs.
constexpr float kPadH = 14.0f;        // left/right inner padding
constexpr float kPadV = 12.0f;        // top/bottom inner padding
constexpr float kIconSize = 16.0f;    // severity mark box
constexpr float kIconGap = 12.0f;     // icon -> text
constexpr float kCloseSize = 24.0f;   // close hit target (square)
constexpr float kCloseGap = 8.0f;     // text -> close button
constexpr float kTitleGap = 4.0f;     // title baseline -> message
constexpr float kCornerDip = 4.0f;
constexpr float kGlyphStroke = 1.5f;  // close "x" and icon ring stroke
}  // namespace

InfoBar::InfoBar() {
    // Focusable only when it has something to activate. A non-closable InfoBar is a
    // static message: putting it in the tab order would make keyboard users tab through
    // read-only text with no action available, which is noise rather than access.
    SetFocusable(false);
    SetHAlign(HAlign::Stretch);
}

void InfoBar::SetTitle(std::wstring text) {
    // Measure-level: the title's presence adds a line, so desired height changes.
    if (title_ == text) return;
    title_ = std::move(text);
    InvalidateDirty(DirtyFlags::Measure);
}

void InfoBar::SetMessage(std::wstring text) {
    if (message_ == text) return;
    message_ = std::move(text);
    InvalidateDirty(DirtyFlags::Measure);
}

void InfoBar::SetSeverity(Severity s) {
    // Render-level: severity only swaps colours. It does not change any metric — the
    // icon box is a fixed size for every severity — so re-measuring would be waste.
    // (Contrast Button::SetKind, which looks similar but DOES change measurement
    // because it selects the font weight.)
    if (severity_ == s) return;
    severity_ = s;
    Invalidate();
}

void InfoBar::SetClosable(bool closable) {
    // Measure-level: the close button reserves horizontal space, which changes the width
    // available to the message and therefore how many lines it wraps to.
    if (closable_ == closable) return;
    closable_ = closable;
    SetFocusable(closable_);
    if (!closable_) closeHovered_ = false;
    InvalidateDirty(DirtyFlags::Measure);
}

void InfoBar::SeverityColors(D2D1_COLOR_F& fill, D2D1_COLOR_F& stroke) const {
    const ColorTokens& c = Theme().colors;
    switch (severity_) {
        case Severity::Success:
            fill = c.severitySuccessFill; stroke = c.severitySuccessStroke; break;
        case Severity::Warning:
            fill = c.severityWarningFill; stroke = c.severityWarningStroke; break;
        case Severity::Error:
            fill = c.severityErrorFill;   stroke = c.severityErrorStroke;   break;
        case Severity::Informational:
        default:
            fill = c.severityInfoFill;    stroke = c.severityInfoStroke;    break;
    }
}

RectDip InfoBar::CloseButtonRect() const {
    if (!closable_) return RectDip{0.0f, 0.0f, 0.0f, 0.0f};
    // Top-aligned rather than vertically centred: on a tall multi-line bar a centred
    // close button drifts far from the title it belongs to.
    return RectDip{bounds_.right() - kPadH - kCloseSize,
                   bounds_.y + kPadV - (kCloseSize - kIconSize) * 0.5f,
                   kCloseSize, kCloseSize};
}

void InfoBar::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);

    const float fontSize = Theme().typography.bodySize;
    // Width the text may occupy: full width minus padding, icon, gaps and (if present)
    // the close button. This is the number the wrap depends on, so it must match exactly
    // what Render passes to DrawText or the measured height will not match the drawn one.
    const float offered = availW > 0.0f ? availW : 320.0f;
    float textW = offered - kPadH * 2.0f - kIconSize - kIconGap;
    if (closable_) textW -= kCloseGap + kCloseSize;
    textW = std::max(1.0f, textW);

    float textH = 0.0f;
    DWriteContext* dw = Dwrite();
    if (dw && dw->Valid()) {
        // Title is one non-wrapping line (a title long enough to wrap is a message).
        if (!title_.empty()) {
            if (IDWriteTextFormat* fmt = dw->Format(
                    fontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                    DWRITE_WORD_WRAPPING_NO_WRAP)) {
                ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(dw->Factory()->CreateTextLayout(
                        title_.c_str(), static_cast<UINT32>(title_.size()), fmt,
                        textW, 100000.0f, layout.GetAddressOf()))) {
                    DWRITE_TEXT_METRICS m{};
                    if (SUCCEEDED(layout->GetMetrics(&m))) textH += m.height;
                }
            }
            if (!message_.empty()) textH += kTitleGap;
        }
        // Message wraps. Measured at an unbounded height so a tall wrapped block reports
        // its real height instead of being silently clipped to the box we guessed.
        if (!message_.empty()) {
            if (IDWriteTextFormat* fmt = dw->Format(
                    fontSize, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                    DWRITE_WORD_WRAPPING_WRAP)) {
                ComPtr<IDWriteTextLayout> layout;
                if (SUCCEEDED(dw->Factory()->CreateTextLayout(
                        message_.c_str(), static_cast<UINT32>(message_.size()), fmt,
                        textW, 100000.0f, layout.GetAddressOf()))) {
                    DWRITE_TEXT_METRICS m{};
                    if (SUCCEEDED(layout->GetMetrics(&m))) textH += m.height;
                }
            }
        }
    } else {
        // Headless / no DWrite: estimate so layout still produces a sane box and tests
        // that only care about structure can run. 1.35 is the usual line-height ratio.
        const float line = fontSize * 1.35f;
        if (!title_.empty()) textH += line;
        if (!message_.empty()) textH += line + (title_.empty() ? 0.0f : kTitleGap);
    }

    // The icon must fit even when the text is shorter than it.
    const float contentH = std::max(textH, kIconSize);
    const float h = contentH + kPadV * 2.0f;

    SetDesired({IsAuto(width_) ? offered : width_,
                IsAuto(height_) ? h : height_});
}

void InfoBar::Render(const DrawingContext& dc) {
    if (!dc.Dc()) return;

    D2D1_COLOR_F fill{}, stroke{};
    SeverityColors(fill, stroke);
    const ColorTokens& c = Theme().colors;

    // Focus ring FIRST: it is stroked outside bounds_, and a later clip (ours or an
    // ancestor's) would cut it away. Same ordering constraint FocusVisual.h documents.
    if (IsFocused())
        DrawFocusRing(dc, bounds_, c, FocusRingSpec{.cornerRadius = kCornerDip});

    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        kCornerDip, kCornerDip);
    dc.FillRoundedRect(rr, fill);
    dc.DrawRoundedRect(rr, stroke, Theme().spacing.borderWidth);

    // Severity mark: a stroked ring with a filled centre dot. Geometric rather than a
    // glyph so it needs no icon font (and so the headless pixel tests can find it).
    const float iconCx = bounds_.x + kPadH + kIconSize * 0.5f;
    const float iconCy = bounds_.y + kPadV + kIconSize * 0.5f;
    dc.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(iconCx, iconCy),
                                 kIconSize * 0.5f, kIconSize * 0.5f),
                   stroke, kGlyphStroke);
    dc.FillEllipse(D2D1::Ellipse(D2D1::Point2F(iconCx, iconCy), 1.6f, 1.6f), stroke);

    DWriteContext* dw = Dwrite();
    if (!dw || !dw->Valid()) return;

    const float fontSize = Theme().typography.bodySize;
    const float textX = bounds_.x + kPadH + kIconSize + kIconGap;
    float textRight = bounds_.right() - kPadH;
    if (closable_) textRight -= kCloseSize + kCloseGap;
    float y = bounds_.y + kPadV;

    if (!title_.empty()) {
        if (IDWriteTextFormat* fmt = dw->Format(
                fontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                DWRITE_WORD_WRAPPING_NO_WRAP)) {
            // Bottom of the rect is bounds_.bottom(), NOT y + one line height: giving
            // DrawText a box exactly one em tall shears descenders off (the Expander
            // header bug). Paragraph alignment NEAR keeps the text at the top anyway.
            dc.DrawText(title_.c_str(), static_cast<UINT32>(title_.size()), fmt,
                        D2D1::RectF(textX, y, textRight, bounds_.bottom()),
                        c.textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dw->Factory()->CreateTextLayout(
                    title_.c_str(), static_cast<UINT32>(title_.size()), fmt,
                    std::max(1.0f, textRight - textX), 100000.0f,
                    layout.GetAddressOf()))) {
                DWRITE_TEXT_METRICS m{};
                if (SUCCEEDED(layout->GetMetrics(&m))) y += m.height + kTitleGap;
            }
        }
    }

    if (!message_.empty()) {
        if (IDWriteTextFormat* fmt = dw->Format(
                fontSize, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                DWRITE_WORD_WRAPPING_WRAP)) {
            dc.DrawText(message_.c_str(), static_cast<UINT32>(message_.size()), fmt,
                        D2D1::RectF(textX, y, textRight, bounds_.bottom()),
                        c.textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    if (closable_) {
        const RectDip cb = CloseButtonRect();
        if (closeHovered_) {
            dc.FillRoundedRect(
                D2D1::RoundedRect(D2D1::RectF(cb.x, cb.y, cb.right(), cb.bottom()),
                                  kCornerDip, kCornerDip),
                c.controlFillHover);
        }
        // A geometric "x" for the same no-icon-font reason as the severity mark.
        const float inset = 7.0f;
        dc.DrawLine(D2D1::Point2F(cb.x + inset, cb.y + inset),
                    D2D1::Point2F(cb.right() - inset, cb.bottom() - inset),
                    c.textPrimary, kGlyphStroke);
        dc.DrawLine(D2D1::Point2F(cb.right() - inset, cb.y + inset),
                    D2D1::Point2F(cb.x + inset, cb.bottom() - inset),
                    c.textPrimary, kGlyphStroke);
    }
}

UIElement* InfoBar::HitTestDeep(float dipX, float dipY) {
    if (!IsVisible() || !bounds_.contains(dipX, dipY)) return nullptr;
    return this;
}

void InfoBar::OnPointerPressed(PointerEventArgs& e) {
    if (!closable_) return;
    if (CloseButtonRect().contains(e.position.x, e.position.y)) {
        // No explicit focus call: InputManager gives focus to the pressed element when it
        // is focusable, which SetClosable already arranged.
        RoutedEventArgs args{};
        args.source = this;
        closed_.Raise(*this, args);
        e.handled = true;
    }
}

void InfoBar::OnPointerMoved(PointerEventArgs& e) {
    if (!closable_) return;
    const bool over = CloseButtonRect().contains(e.position.x, e.position.y);
    if (over != closeHovered_) {
        closeHovered_ = over;
        Invalidate();
    }
}

void InfoBar::OnPointerLeft() {
    if (closeHovered_) {
        closeHovered_ = false;
        Invalidate();
    }
}

void InfoBar::OnKeyDownRouted(KeyEventArgs& e) {
    // Space/Enter activates the close affordance, matching ButtonBase. Only meaningful
    // when closable — and when it is not, the control is not focusable, so this cannot
    // be reached by keyboard anyway.
    if (!closable_) return;
    if (e.vk == VK_SPACE || e.vk == VK_RETURN) {
        RoutedEventArgs args{};
        args.source = this;
        closed_.Raise(*this, args);
        e.handled = true;
    }
}

float InfoBar::VisualOverflowDip() const {
    // Unconditional, NOT gated on IsFocused(): the frame on which focus LEAVES still has
    // to repaint the pixels the ring occupied, and by then IsFocused() is already false.
    // Hyperlink and Expander both shipped that bug.
    return FocusRingPadDip(FocusRingSpec{.cornerRadius = kCornerDip});
}

}  // namespace fluent
