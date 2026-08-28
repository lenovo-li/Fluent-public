// TabControl.cpp
#include "TabControl.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/ResourceCache.h"
#include "../input/FocusManager.h"
#include <algorithm>
#include <dwrite.h>

namespace fluent {

namespace {
constexpr float kHeaderPadX = 16.0f;
// Selection indicator: a short, thick, fully-rounded pill centered under the
// header text rather than an edge-to-edge hairline — the modern TabView look.
constexpr float kIndicatorHeight = 3.0f;
constexpr float kIndicatorInsetX = 12.0f;
// Header width when DWrite is unavailable (detached / headless). Deliberately a
// rough per-character estimate: the strip still lays out and hit-tests, and the
// real widths arrive on the first attached Measure.
constexpr float kFallbackCharWidth = 8.0f;
constexpr float kHeaderFontSize = 13.0f;
// P1-11: Close button dimensions and insets. The button is a small circular hit
// target on the right edge of the header; the '×' glyph is centered inside it.
constexpr float kCloseBtnSize = 16.0f;      // full hit target
constexpr float kCloseBtnInsetRight = 8.0f; // from header right edge
constexpr float kCloseBtnGap = 8.0f;        // between text and close btn
}  // namespace

void TabControl::RemoveTab(int index) {
    if (index < 0 || index >= static_cast<int>(headers_.size())) return;

    // Detach before the element leaves the tree, while the context is still valid.
    if (index < static_cast<int>(children_.size()) && children_[index] &&
        children_[index]->IsAttached())
        children_[index]->DetachFromContext();

    headers_.erase(headers_.begin() + index);
    // Erase the parallel width so the two vectors stay aligned. Deliberately does
    // NOT set headerWidthsDirty_: no remaining header's TEXT changed, so every
    // surviving width is still correct, and re-measuring would rebuild a layout per
    // tab for nothing. The erase is what keeps the size guard in MeasureHeaders
    // satisfied.
    if (index < static_cast<int>(headerWidths_.size()))
        headerWidths_.erase(headerWidths_.begin() + index);
    if (index < static_cast<int>(children_.size())) {
        children_[index]->SetParent(nullptr);
        children_.erase(children_.begin() + index);
    }

    // Move the selection. Removing the selected tab selects the previous one (or
    // the new first tab when the removed one was at index 0); removing a tab
    // BEFORE the selection shifts the stored index down so it still names the same
    // content.
    const int count = static_cast<int>(headers_.size());
    if (selectedIndex_ == index) {
        int next = (count == 0) ? -1 : std::min(std::max(0, index - 1), count - 1);
        selectedIndex_ = -1;  // force SetSelectedIndex to run its attach sync
        SetSelectedIndex(next);
    } else if (selectedIndex_ > index) {
        --selectedIndex_;
    }

    // Hover indices name a tab by position, so a removal invalidates them. Clearing
    // both is correct rather than merely shifting: the tab now under the pointer is
    // a different one, and the next PointerMoved recomputes them anyway. Leaving a
    // stale index would paint a hover on whichever tab slid into that slot — and if
    // the removed tab was the last one, on an index that no longer exists.
    hoverIndex_ = -1;
    closeButtonHoverIndex_ = -1;

    InvalidateDirty(DirtyFlags::Measure);
}

const std::wstring& TabControl::HeaderAt(int index) const {
    static const std::wstring kEmpty;
    if (index < 0 || index >= static_cast<int>(headers_.size())) return kEmpty;
    return headers_[index];
}

void TabControl::SetSelectedIndex(int index) {
    if (index < -1) index = -1;
    if (index >= static_cast<int>(headers_.size())) index = -1;
    if (selectedIndex_ == index) return;

    const int prev = selectedIndex_;
    selectedIndex_ = index;
    // Attach state first, so the tree is consistent before anyone observes the
    // change (a SelectionChanged handler may walk the new content).
    SyncContentAttachment();
    contentFade_.SetImmediate(0.0f);
    // Measure, not Render: the newly selected content has never been measured at
    // this size, and the strip's indicator target moved.
    InvalidateDirty(DirtyFlags::Measure);
    if (prev != index) {
        int payload = index;
        selectionChanged_.Raise(*this, payload);
    }
}

FrameworkElement* TabControl::SelectedContent() const {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(children_.size()))
        return nullptr;
    return children_[selectedIndex_].get();
}

bool TabControl::IsContentAttached(int index) const {
    if (index < 0 || index >= static_cast<int>(children_.size())) return false;
    return children_[index] && children_[index]->IsAttached();
}

float TabControl::HeaderWidth(int index) const {
    if (index < 0 || index >= static_cast<int>(headers_.size())) return 0.0f;
    const float text = (index < static_cast<int>(headerWidths_.size()))
                           ? headerWidths_[index]
                           : kFallbackCharWidth * headers_[index].size();
    // The close button widens the slot rather than overlapping the text: the text
    // is drawn CENTERED in the header rect (see Render), so an overlapping button
    // would sit on top of the last glyphs of a tab whose text fills its slot.
    const float close =
        closeButtonVisible_ ? (kCloseBtnSize + kCloseBtnGap) : 0.0f;
    return text + kHeaderPadX * 2.0f + close;
}

RectDip TabControl::CloseButtonRect(int index) const {
    // Close buttons are disabled on vertical strips — a button beside vertically-
    // oriented text reads poorly, and the strip width is constrained.
    if (!closeButtonVisible_ || IsVerticalStrip()) return RectDip{};
    const RectDip hr = HeaderRect(index);
    if (hr.isEmpty()) return RectDip{};
    const float x = hr.right() - kCloseBtnInsetRight - kCloseBtnSize;
    // A header clamped by the strip edge (see HeaderRect) can be too narrow to
    // hold the button. Report empty rather than a rect starting left of the
    // header, which would hit-test over the neighbouring tab's text.
    if (x < hr.x) return RectDip{};
    const float y = hr.y + (hr.h - kCloseBtnSize) * 0.5f;
    return RectDip{x, y, kCloseBtnSize, kCloseBtnSize};
}

int TabControl::CloseButtonIndexAt(float dipX, float dipY) const {
    if (!closeButtonVisible_) return -1;
    for (int i = 0; i < static_cast<int>(headers_.size()); ++i) {
        const RectDip r = CloseButtonRect(i);
        if (r.isEmpty()) continue;
        if (dipX >= r.x && dipX < r.right() && dipY >= r.y && dipY < r.bottom())
            return i;
    }
    return -1;
}

RectDip TabControl::HeaderRect(int index) const {
    if (index < 0 || index >= static_cast<int>(headers_.size())) return RectDip{};

    if (IsVerticalStrip()) {
        // Vertical: headers stack vertically, each taking stripHeight_ tall and
        // stripWidth_ wide (computed in MeasureOverride).
        float y = bounds_.y;
        for (int i = 0; i < index; ++i) y += stripHeight_;
        const float x = (stripPlacement_ == TabStripPlacement::Left)
                            ? bounds_.x
                            : (bounds_.right() - stripWidth_);
        // Clamp to the strip bounds so an overflowing header is visually truncated.
        const float bottom = bounds_.bottom();
        const float clampedH = std::min(stripHeight_, std::max(0.0f, bottom - y));
        return RectDip{x, y, stripWidth_, clampedH};
    } else {
        // Horizontal: headers run left to right, each taking its measured width.
        float x = bounds_.x;
        for (int i = 0; i < index; ++i) x += HeaderWidth(i);
        const float w = HeaderWidth(index);
        const float y = (stripPlacement_ == TabStripPlacement::Top)
                            ? bounds_.y
                            : (bounds_.bottom() - stripHeight_);
        // Clamp to the strip bounds so an overflowing header is visually truncated.
        const float right = bounds_.right();
        const float clampedW = std::min(w, std::max(0.0f, right - x));
        return RectDip{x, y, clampedW, stripHeight_};
    }
}

int TabControl::HeaderIndexAt(float dipX, float dipY) const {
    if (IsVerticalStrip()) {
        // Vertical strip: test whether point is inside the strip's horizontal band,
        // then walk down the headers.
        const float stripLeft =
            (stripPlacement_ == TabStripPlacement::Left) ? bounds_.x : (bounds_.right() - stripWidth_);
        if (dipX < stripLeft || dipX >= stripLeft + stripWidth_) return -1;

        float y = bounds_.y;
        for (int i = 0; i < static_cast<int>(headers_.size()); ++i) {
            if (dipY >= y && dipY < y + stripHeight_) return i;
            y += stripHeight_;
        }
        return -1;
    } else {
        // Horizontal strip: test whether point is inside the strip's vertical band,
        // then walk across the headers.
        const float stripTop =
            (stripPlacement_ == TabStripPlacement::Top) ? bounds_.y : (bounds_.bottom() - stripHeight_);
        if (dipY < stripTop || dipY >= stripTop + stripHeight_) return -1;

        float x = bounds_.x;
        for (int i = 0; i < static_cast<int>(headers_.size()); ++i) {
            const float w = HeaderWidth(i);
            if (dipX >= x && dipX < x + w) return i;
            x += w;
        }
        return -1;
    }
}

void TabControl::Render(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;
    const float corner = Theme().spacing.cornerRadiusSmall;

    // Strip background: a subtle layer so the selected header reads as raised.
    // Compute the strip rect based on placement.
    RectDip strip;
    if (IsVerticalStrip()) {
        const float x = (stripPlacement_ == TabStripPlacement::Left)
                            ? bounds_.x
                            : (bounds_.right() - stripWidth_);
        strip = RectDip{x, bounds_.y, stripWidth_, bounds_.h};
    } else {
        const float y = (stripPlacement_ == TabStripPlacement::Top)
                            ? bounds_.y
                            : (bounds_.bottom() - stripHeight_);
        strip = RectDip{bounds_.x, y, bounds_.w, stripHeight_};
    }

    dc.FillRect(D2D1::RectF(strip.x, strip.y, strip.right(), strip.bottom()),
                pal.layerFill);

    {
        // Clip header text to the strip so a tab whose width exceeds the
        // available space is truncated at the strip edge rather than spilling
        // past it. Scoped so the indicator and content below are NOT clipped.
        ClipGuard stripClip = dc.PushClip(
            D2D1::RectF(strip.x, strip.y, strip.right(), strip.bottom()));

        // Two formats: the selected header is SemiBold (Win11), the rest
        // Normal. The measurement path (MeasureHeaders) sizes every tab for
        // the WIDER of the two so selecting a tab never truncates its text.
        IDWriteTextFormat* fmt = Dwrite()
            ? Dwrite()->Format(kHeaderFontSize, DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_TEXT_ALIGNMENT_CENTER,
                               DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
            : nullptr;
        IDWriteTextFormat* fmtSel = Dwrite()
            ? Dwrite()->Format(kHeaderFontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                               DWRITE_TEXT_ALIGNMENT_CENTER,
                               DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
            : nullptr;

        for (int i = 0; i < static_cast<int>(headers_.size()); ++i) {
            const RectDip hr = HeaderRect(i);
            if (hr.isEmpty()) continue;
            const bool selected = (i == selectedIndex_);
            const bool hovered = (i == hoverIndex_);

            if (selected || hovered) {
                const D2D1_COLOR_F fill =
                    selected ? pal.controlFillDefault : pal.controlFillHover;
                // Vertically centered inside the strip: equal 3 DIP insets top and
                // bottom (the old rect was +2 top / +0 bottom, which read as
                // bottom-heavy and clipped against the indicator).
                const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
                        D2D1::RectF(hr.x + 2.0f, hr.y + 3.0f,
                                    hr.right() - 2.0f, hr.bottom() - 3.0f),
                        corner, corner);
                dc.FillRoundedRect(rr, fill);
                // A hairline border so the state change reads even when the fill is
                // within a few percent of the strip fill — light-theme hover is
                // exactly that case (white 0.70 -> near-white 0.85, ~2% apart).
                dc.DrawRoundedRect(rr, pal.controlStrokeDefault, 1.0f);
            }

            IDWriteTextFormat* itemFmt = selected ? (fmtSel ? fmtSel : fmt) : fmt;
            if (itemFmt && !headers_[i].empty()) {
                // P1-11: when close buttons are visible, the text rect must leave
                // room for the button. The text is still CENTERED (that's what
                // DWRITE_TEXT_ALIGNMENT_CENTER does), so a naïve "draw into the full
                // hr" would center it across the button. Instead narrow the rect by
                // the button's width so the centering anchor sits between the left
                // pad and the button.
                float textRight = hr.right();
                if (closeButtonVisible_) {
                    const RectDip cbr = CloseButtonRect(i);
                    if (!cbr.isEmpty())
                        textRight = cbr.x - kCloseBtnGap;
                }
                // TabControl derives from Panel, not Control, so there is no
                // Foreground override to consult here — the header colors stay
                // purely theme-driven. Giving TabControl the Control chrome layer
                // would mean re-parenting it, which is a larger change than this
                // property pass.
                dc.DrawText(headers_[i].c_str(),
                            static_cast<UINT32>(headers_[i].size()), itemFmt,
                            D2D1::RectF(hr.x, hr.y, textRight, hr.bottom()),
                            selected ? pal.textPrimary : pal.textSecondary,
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }

            // P1-11: Close button. A '×' glyph on a circular hover target, drawn
            // only when enabled. The hover fill is subtle (same as a Subtle Button
            // at rest → controlFillHover), so it appears only when the pointer is
            // actually over the button, not the whole header.
            if (closeButtonVisible_) {
                const RectDip cbr = CloseButtonRect(i);
                if (!cbr.isEmpty()) {
                    const bool btnHovered = (i == closeButtonHoverIndex_);
                    if (btnHovered) {
                        dc.FillEllipse(
                            D2D1::Ellipse(
                                D2D1::Point2F(cbr.x + cbr.w * 0.5f,
                                              cbr.y + cbr.h * 0.5f),
                                cbr.w * 0.5f, cbr.h * 0.5f),
                            pal.controlFillHover);
                    }
                    // The '×' is drawn with DrawLine rather than a font glyph so it
                    // scales exactly with the button size and needs no DWrite format.
                    const float cx = cbr.x + cbr.w * 0.5f;
                    const float cy = cbr.y + cbr.h * 0.5f;
                    const float arm = 4.0f;  // half-span of the X
                    const D2D1_COLOR_F glyphColor =
                        btnHovered ? pal.textPrimary : pal.textSecondary;
                    dc.DrawLine(D2D1::Point2F(cx - arm, cy - arm),
                                D2D1::Point2F(cx + arm, cy + arm),
                                glyphColor, 1.0f);
                    dc.DrawLine(D2D1::Point2F(cx + arm, cy - arm),
                                D2D1::Point2F(cx - arm, cy + arm),
                                glyphColor, 1.0f);
                }
            }
        }
    }  // stripClip pops here — indicator and content are not clipped

    // Selection indicator: an accent underline under the selected header. Draw it
    // at HeaderRect(selectedIndex_) directly — the authoritative position — rather
    // than at the slide animation's current value. The slide animation's tick
    // depends on the host animation registry, which is only rebuilt on input
    // events; a programmatic SetSelectedIndex can leave the animated value frozen
    // at the old header even though selectedIndex_ already moved, so the indicator
    // would sit under the wrong tab. Anchoring to the derived rect is always
    // correct. (The slide tween can be reintroduced once the registry collects
    // programmatic animations — see RenderNow.)
    if (selectedIndex_ >= 0) {
        const RectDip hr = HeaderRect(selectedIndex_);

        if (IsVerticalStrip()) {
            // Vertical strip: indicator is a vertical bar on the edge opposite the
            // content. For Left placement, it sits on the right edge of the strip; for
            // Right placement, on the left edge.
            const float h = std::min(std::max(0.0f, hr.h - kIndicatorInsetX * 2.0f), 36.0f);
            if (h > 0.0f) {
                const float y = hr.y + (hr.h - h) * 0.5f;
                const float x = (stripPlacement_ == TabStripPlacement::Left)
                                    ? (strip.right() - kIndicatorHeight - 1.0f)
                                    : (strip.x + 1.0f);
                dc.FillRoundedRect(
                    D2D1::RoundedRect(
                        D2D1::RectF(x, y, x + kIndicatorHeight, y + h),
                        kIndicatorHeight * 0.5f, kIndicatorHeight * 0.5f),
                    pal.accent);
            }
        } else {
            // Horizontal strip: indicator is a horizontal bar under/above the header.
            // For Top placement, it sits near the bottom of the strip; for Bottom
            // placement, near the top.
            const float w = std::min(std::max(0.0f, hr.w - kIndicatorInsetX * 2.0f), 36.0f);
            if (w > 0.0f) {
                const float x = hr.x + (hr.w - w) * 0.5f;
                const float y = (stripPlacement_ == TabStripPlacement::Top)
                                    ? (strip.bottom() - kIndicatorHeight - 1.0f)
                                    : (strip.y + 1.0f);
                dc.FillRoundedRect(
                    D2D1::RoundedRect(
                        D2D1::RectF(x, y, x + w, y + kIndicatorHeight),
                        kIndicatorHeight * 0.5f, kIndicatorHeight * 0.5f),
                    pal.accent);
            }
        }
    }

    // Content: only the selected child. The fade-in is a visual nicety layered on
    // top of the render, never a gate ON it: SetSelectedIndex is a programmatic
    // path (not only a pointer press), and the host only rebuilds its animation
    // set on input events — so the fade can sit at 0 with nothing to tick it. If
    // rendering were skipped at fade 0 the tab would show nothing until the next
    // unrelated input. Clamp the effective opacity up to 1 whenever the fade has
    // not started advancing; once it is mid-animation the real value applies.
    if (FrameworkElement* content = SelectedContent()) {
        const float fade = contentFade_;
        if (fade >= 1.0f || fade <= 0.0f) {
            content->RenderWithOpacity(dc);
        } else {
            content->RenderWithOpacity(dc.WithOpacity(fade));
        }
    }

    // Focus ring around the STRIP, not the whole control: the strip is what the
    // arrow keys drive, so ringing the entire control (content included) would
    // suggest the content is focused. Follows the strip to whichever edge it is on.
    if (IsFocused()) {
        dc.DrawRoundedRect(
            D2D1::RoundedRect(
                D2D1::RectF(strip.x + 0.5f, strip.y + 0.5f,
                            strip.right() - 0.5f, strip.bottom() - 0.5f),
                corner, corner),
            D2D1::ColorF(pal.focusStroke.r, pal.focusStroke.g, pal.focusStroke.b,
                         0.7f),
            1.0f);
    }
}

UIElement* TabControl::HitTestDeep(float dipX, float dipY) {
    // The strip is hit-tested by the TabControl itself (bounds contain it);
    // only the SELECTED content may be reached below the strip. Detached
    // contents keep their last bounds, so the base Panel walk would hit them.
    if (!HitTest(dipX, dipY)) return nullptr;
    if (FrameworkElement* content = SelectedContent()) {
        if (UIElement* hit = content->HitTestDeep(dipX, dipY)) return hit;
    }
    return this;
}

void TabControl::CollectDirtyBounds(std::vector<RectDip>& out) {
    if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    // Detached (unselected) contents are never repainted, so their dirty flags
    // must not inflate the redraw region either.
    if (FrameworkElement* content = SelectedContent())
        content->CollectDirtyBounds(out);
}

void TabControl::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;

    // P1-11: Close button wins over selection. A click on the '×' of tab 3 while
    // tab 1 is selected must close 3, not select it. Test the close button first
    // for that reason.
    const int closeHit = CloseButtonIndexAt(e.position.x, e.position.y);
    if (closeHit >= 0) {
        TabCloseRequestedArgs args{closeHit};
        tabCloseRequested_.Raise(*this, args);
        e.handled = true;
        return;
    }

    const int hit = HeaderIndexAt(e.position.x, e.position.y);
    if (hit < 0) return;
    SetSelectedIndex(hit);
    if (Context().focus) Context().focus->SetFocus(this);
    e.handled = true;
}

void TabControl::OnPointerMoved(PointerEventArgs& e) {
    const int hit = HeaderIndexAt(e.position.x, e.position.y);
    const int closeHit = CloseButtonIndexAt(e.position.x, e.position.y);

    // Separate hover states for the header and the close button. A hover over the
    // close button ALSO hovers the header (so the header's background fill appears
    // when you approach the '×'), but the button's own fill only appears when the
    // pointer is directly over the button's circular region.
    if (hit != hoverIndex_ || closeHit != closeButtonHoverIndex_) {
        hoverIndex_ = hit;
        closeButtonHoverIndex_ = closeHit;
        Invalidate();
    }
}

void TabControl::OnPointerLeft() {
    if (hoverIndex_ != -1 || closeButtonHoverIndex_ != -1) {
        hoverIndex_ = -1;
        closeButtonHoverIndex_ = -1;
        Invalidate();
    }
}

void TabControl::OnKeyDownRouted(KeyEventArgs& e) {
    const int count = static_cast<int>(headers_.size());
    if (count == 0) return;

    switch (e.vk) {
    case VK_LEFT:
        if (selectedIndex_ > 0) SetSelectedIndex(selectedIndex_ - 1);
        e.handled = true;
        break;
    case VK_RIGHT:
        if (selectedIndex_ < count - 1) SetSelectedIndex(selectedIndex_ + 1);
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
    default:
        break;
    }
}

bool TabControl::WantsAnimationTick() const {
    if (selectedIndex_ < 0) return false;
    const RectDip hr = HeaderRect(selectedIndex_);
    if (indicatorX_.Animating(hr.x, 0.25f)) return true;
    if (indicatorW_.Animating(hr.w, 0.25f)) return true;
    return contentFade_.Animating(1.0f, 0.01f);
}

void TabControl::OnAnimationTick(float dtSec) {
    if (selectedIndex_ < 0) return;
    const RectDip hr = HeaderRect(selectedIndex_);
    const float tau = Theme().motion.fadeTau;
    indicatorX_.Approach(hr.x, dtSec, tau, 0.25f);
    indicatorW_.Approach(hr.w, dtSec, tau, 0.25f);
    contentFade_.Approach(1.0f, dtSec, tau, 0.01f);
    Invalidate();
}

SizeDip TabControl::MeasureOverride(float availW, float availH) {
    MeasureHeaders();

    const bool vertical = IsVerticalStrip();

    if (vertical) {
        // Vertical strip: headers stack vertically, each as wide as the widest one.
        // The strip takes fixed width (stripWidth_), content gets the rest.
        float maxHeaderW = 0.0f;
        for (int i = 0; i < static_cast<int>(headers_.size()); ++i)
            maxHeaderW = std::max(maxHeaderW, headerWidths_[i]);
        // Add horizontal padding. Close button width is NOT added here — vertical
        // headers are text-only (a close button beside rotated text reads poorly).
        const float headerSlotW = maxHeaderW + kHeaderPadX * 2.0f;
        stripWidth_ = std::max(headerSlotW, 80.0f);  // clamp to a minimum

        const float stripH = static_cast<float>(headers_.size()) * stripHeight_;

        // Measure the selected content in the remaining width.
        float contentW = 0.0f, contentH = 0.0f;
        if (FrameworkElement* content = SelectedContent()) {
            const float contentAvailW = std::max(0.0f, availW - stripWidth_);
            content->MeasureCached(contentAvailW, availH);
            contentW = content->Desired().w;
            contentH = content->Desired().h;
        }

        return SizeDip{stripWidth_ + contentW, std::max(stripH, contentH)};
    } else {
        // Horizontal strip: headers run left to right, strip takes fixed height.
        float stripW = 0.0f;
        for (int i = 0; i < static_cast<int>(headers_.size()); ++i)
            stripW += HeaderWidth(i);

        // Only the selected content is measured — an unselected tab's subtree is
        // detached and must not be walked.
        float contentW = 0.0f, contentH = 0.0f;
        if (FrameworkElement* content = SelectedContent()) {
            const float contentAvailH = std::max(0.0f, availH - stripHeight_);
            content->MeasureCached(availW, contentAvailH);
            contentW = content->Desired().w;
            contentH = content->Desired().h;
        }

        return SizeDip{std::max(stripW, contentW), stripHeight_ + contentH};
    }
}

void TabControl::ArrangeOverride(const RectDip& content) {
    // The strip occupies one edge; the selected content gets the rest. Seed the
    // indicator on the first arrange so it does not slide in from x=0 on the very
    // first frame.
    if (selectedIndex_ >= 0 && indicatorW_ <= 0.0f) {
        const RectDip hr = HeaderRect(selectedIndex_);
        indicatorX_.SetImmediate(hr.x);
        indicatorW_.SetImmediate(hr.w);
    }

    if (FrameworkElement* child = SelectedContent()) {
        const float topGap = 8.0f;
        const float sideInset = 8.0f;
        RectDip slot;

        switch (stripPlacement_) {
        case TabStripPlacement::Top:
            // Content sits below the strip with a small top gap.
            slot = RectDip{content.x + sideInset, content.y + stripHeight_ + topGap,
                           std::max(0.0f, content.w - sideInset * 2.0f),
                           std::max(0.0f, content.h - stripHeight_ - topGap)};
            break;
        case TabStripPlacement::Bottom:
            // Content sits above the strip with a small bottom gap.
            slot = RectDip{content.x + sideInset, content.y + topGap,
                           std::max(0.0f, content.w - sideInset * 2.0f),
                           std::max(0.0f, content.h - stripHeight_ - topGap)};
            break;
        case TabStripPlacement::Left:
            // Content sits to the right of the strip with a small left gap.
            slot = RectDip{content.x + stripWidth_ + sideInset, content.y + topGap,
                           std::max(0.0f, content.w - stripWidth_ - sideInset),
                           std::max(0.0f, content.h - topGap * 2.0f)};
            break;
        case TabStripPlacement::Right:
            // Content sits to the left of the strip with a small right gap.
            slot = RectDip{content.x + sideInset, content.y + topGap,
                           std::max(0.0f, content.w - stripWidth_ - sideInset),
                           std::max(0.0f, content.h - topGap * 2.0f)};
            break;
        }

        ArrangeChild(child, slot);
    }
}

void TabControl::OnAttachedToTree() {
    // DWrite is available now, so header widths can be measured for real. This is
    // the call that clears headerWidthsDirty_ in the normal case: tabs are added
    // while detached, MeasureHeaders leaves the per-character estimate and stays
    // dirty, and the real metrics land here. The attach filtering itself happens in
    // AttachChildren — see the header for why it cannot happen here.
    MeasureHeaders();
}

void TabControl::AttachChildren(const UIContext& ctx) {
    // Only the selected tab's content joins the live tree.
    for (int i = 0; i < static_cast<int>(children_.size()); ++i) {
        if (!children_[i]) continue;
        if (i == selectedIndex_) children_[i]->AttachToContext(ctx);
    }
}

void TabControl::MeasureHeaders() {
    // Nothing here depends on the available size — headerWidths_ is a function of
    // the header STRINGS alone (kHeaderFontSize is a constant, and DIP-space text
    // metrics do not change with DPI). So the whole walk is skippable whenever the
    // strings have not changed since the last measurement.
    //
    // This gate is the point of the method. MeasureOverride calls it
    // unconditionally, and a resize drag re-measures with a new constraint every
    // frame — which defeats MeasureCached and used to rebuild TWO throwaway
    // IDWriteTextLayouts per tab per frame, read one float off each, and drop them.
    // The size guard is belt-and-suspenders: a headerWidths_ out of sync with
    // headers_ would index out of range in MeasureOverride, so re-measure rather
    // than trust the flag alone.
    if (!headerWidthsDirty_ && headerWidths_.size() == headers_.size()) return;

    headerWidths_.assign(headers_.size(), 0.0f);
    for (size_t i = 0; i < headers_.size(); ++i)
        headerWidths_[i] = kFallbackCharWidth * headers_[i].size();

    if (!Dwrite()) {
        // Detached / headless: leave the per-character estimate in place and stay
        // DIRTY, so the first attached Measure replaces it with real metrics.
        return;
    }
    // The selected header renders SemiBold (see Render), which is wider than
    // Normal. Measure BOTH and size every tab for the wider one — otherwise
    // selecting a tab would truncate its text (Render centers the wider text
    // into a rect measured for the narrower weight).
    constexpr float kLayoutBoxW = 10000.0f;
    constexpr float kLayoutBoxH = 100.0f;
    constexpr DWRITE_FONT_WEIGHT kWeights[] = {DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_WEIGHT_SEMI_BOLD};

    // Prefer the shared cache (roadmap §13.3) when the tree has one: two tab
    // controls showing the same header text, or a re-measure after an AddTab that
    // only appended, then cost a hash lookup instead of a DWrite build. Falls back
    // to building directly when detached from a cache-carrying host.
    // A DWriteContext that exists but failed Initialize() has no factory, and the
    // cache would hand back null for every key. Returning while still DIRTY is the
    // point: the estimates stay in place AND a later attach (or a device rebuild
    // that produces a working factory) still gets to measure for real. Clearing the
    // flag here would freeze the per-character estimate in permanently.
    IDWriteFactory* factory = Dwrite()->Valid() ? Dwrite()->Factory() : nullptr;
    if (!factory) return;
    ResourceCache* cache = Context().resourceCache;

    for (size_t i = 0; i < headers_.size(); ++i) {
        for (DWRITE_FONT_WEIGHT weight : kWeights) {
            ComPtr<IDWriteTextLayout> layout;
            if (cache) {
                TextLayoutKey key;
                key.text = headers_[i];
                key.fontSize = kHeaderFontSize;
                key.weight = weight;
                key.textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
                key.paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
                key.wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
                key.maxWidth = kLayoutBoxW;
                key.maxHeight = kLayoutBoxH;
                layout = cache->GetTextLayout(std::move(key));
            } else {
                IDWriteTextFormat* f =
                    Dwrite()->Format(kHeaderFontSize, weight,
                                     DWRITE_TEXT_ALIGNMENT_LEADING,
                                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (!f) continue;
                if (FAILED(factory->CreateTextLayout(
                        headers_[i].c_str(), static_cast<UINT32>(headers_[i].size()),
                        f, kLayoutBoxW, kLayoutBoxH, layout.GetAddressOf())))
                    continue;
            }
            if (!layout) continue;
            DWRITE_TEXT_METRICS m{};
            if (SUCCEEDED(layout->GetMetrics(&m)))
                headerWidths_[i] = (std::max)(headerWidths_[i], m.width);
        }
    }

    // Real metrics are in place; only a header-string change re-dirties this.
    headerWidthsDirty_ = false;
}

void TabControl::SyncContentAttachment() {
    if (!IsAttached()) return;
    for (int i = 0; i < static_cast<int>(children_.size()); ++i) {
        if (!children_[i]) continue;
        const bool want = (i == selectedIndex_);
        const bool have = children_[i]->IsAttached();
        if (want && !have)
            children_[i]->AttachToContext(Context());
        else if (!want && have)
            children_[i]->DetachFromContext();
    }
}

}  // namespace fluent
