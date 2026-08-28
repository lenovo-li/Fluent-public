// MenuBar.cpp

#include "MenuBar.h"
#include "../window/WindowServices.h"
#include "../services/PopupGeometry.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DrawingContext.h"
#include <algorithm>

namespace fluent {

namespace {
const char* kTag = "MenuBar";

constexpr float kFontSize = 13.0f;    // DIP
constexpr float kItemPadX = 12.0f;    // DIP, horizontal padding each side of a title
constexpr float kCornerRadius = 4.0f; // DIP, hover/open highlight corner
} // namespace

MenuBar::MenuBar() = default;
MenuBar::~MenuBar() = default;

void MenuBar::OnAttachedToTree() {
    window_ = Context().window;
    flyout_ = std::make_unique<MenuFlyout>();
    // The flyout is not a visual-tree child (the bar owns it directly), so it
    // does not get a context automatically — hand it the bar's tree context so
    // its list views read DWrite / the window through Context() (roadmap §6.2).
    flyout_->SetContext(Context());

    // Keep the open menu alive when a click lands on one of our titles: the bar's
    // own OnClickAt decides whether to toggle it closed or switch to another.
    flyout_->SetDismissGuard([this](int sx, int sy) {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            RECT r = ItemScreenRect(i);
            POINT p = {sx, sy};
            if (PtInRect(&r, p)) return true;
        }
        return false;
    });

    // When the flyout closes by any path (click-outside, Esc, invoke), clear our
    // open-state — unless we're mid-switch between titles.
    flyout_->SetOnClosed([this]() {
        if (switching_) return;
        openIndex_ = -1;
        Invalidate();
    });

    // Titles added before attach measured with no DWrite (width 0); recompute now
    // that Context().dwrite is available so the bar lays out correctly.
    for (TopItem& it : items_)
        it.width = MeasureText(it.title) + kItemPadX * 2.0f;
    InvalidateMeasure();
}

void MenuBar::OnDetachedFromTree() {
    // Tear the flyout (and its popups / window registrations) down while still
    // attached, so no [this] callback outlives the tree. Idempotent.
    if (flyout_) { flyout_->Close(); flyout_.reset(); }
    window_ = nullptr;
}

void MenuBar::AddMenu(std::wstring title, std::vector<MenuItem> items) {
    TopItem it;
    it.title = std::move(title);
    it.items = std::move(items);
    it.width = MeasureText(it.title) + kItemPadX * 2.0f;
    items_.push_back(std::move(it));
    Invalidate();
}

float MenuBar::MeasureText(const std::wstring& s) const {
    if (s.empty() || !Dwrite()) return 0.0f;
    // Default format is NO_WRAP; measuring width does not depend on alignment.
    IDWriteTextFormat* fmt = Dwrite()->Format(kFontSize);
    if (!fmt) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(Dwrite()->Factory()->CreateTextLayout(
            s.c_str(), static_cast<UINT32>(s.size()), fmt,
            100000.0f, 100.0f, layout.GetAddressOf())))
        return 0.0f;
    DWRITE_TEXT_METRICS m{};
    if (FAILED(layout->GetMetrics(&m))) return 0.0f;
    return m.widthIncludingTrailingWhitespace;
}

void MenuBar::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    float total = 0.0f;
    for (const TopItem& it : items_) total += it.width;
    SetDesired({IsAuto(width_) ? std::min(availW, total) : width_,
                     IsAuto(height_) ? 32.0f : height_});
}

float MenuBar::ItemXOffset(int i) const {
    float x = bounds_.x;
    for (int k = 0; k < i && k < static_cast<int>(items_.size()); ++k)
        x += items_[k].width;
    return x;
}

int MenuBar::ItemAt(float dipX) const {
    float x = bounds_.x;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        // Hit-test the VISIBLE slice, matching what Render paints: a title clipped
        // away by a too-narrow bar must not be clickable, and one that is partly
        // visible is clickable only over the part actually on screen.
        const RectDip vis = VisibleTitleRect(x, items_[i].width, bounds_);
        if (vis.isEmpty()) break;   // laid out left to right: the rest are out too
        if (dipX >= vis.x && dipX < vis.right()) return i;
        x += items_[i].width;
    }
    return -1;
}

RECT MenuBar::ItemScreenRect(int i) const {
    RECT r = {0, 0, 0, 0};
    if (!window_ || i < 0 || i >= static_cast<int>(items_.size())) return r;
    RECT rcWindow;
    GetWindowRect(window_->Hwnd(), &rcWindow);
    // Anchor the dropdown under the VISIBLE slice, so a partly-clipped title opens its
    // menu against what the user can actually see rather than off the bar's edge.
    const RectDip vis = VisibleTitleRect(ItemXOffset(i), items_[i].width, bounds_);
    if (vis.isEmpty()) return r;
    return AnchorScreenRect(rcWindow.left, rcWindow.top,
                            vis.x, bounds_.y, vis.w, bounds_.h,
                            window_->DpiScale());
}

void MenuBar::OpenMenu(int i) {
    if (!flyout_ || i < 0 || i >= static_cast<int>(items_.size())) return;
    // SetItems() closes any currently-open chain, which fires the flyout's
    // onClosed callback. Guard it with switching_ so that callback doesn't clear
    // the openIndex_ we're about to set. Set openIndex_ AFTER SetItems, because
    // the close (and thus the callback) happens synchronously inside SetItems.
    switching_ = true;
    flyout_->SetItems(items_[i].items);
    switching_ = false;
    openIndex_ = i;
    flyout_->ShowBelow(ItemScreenRect(i));
    Invalidate();
}

void MenuBar::CloseMenu() {
    if (flyout_) flyout_->Close();
    openIndex_ = -1;
    Invalidate();
}

void MenuBar::OnPointerMoved(PointerEventArgs& e) {
    int idx = ItemAt(e.position.x);
    if (idx != hovered_) {
        hovered_ = idx;
        Invalidate();
    }
    // While a menu is open, moving onto a different title switches to it.
    // OpenMenu self-manages switching_ around its internal close-then-reopen.
    if (openIndex_ >= 0 && idx >= 0 && idx != openIndex_)
        OpenMenu(idx);
}

void MenuBar::OnPointerLeft() {
    if (hovered_ != -1) {
        hovered_ = -1;
        Invalidate();
    }
}

void MenuBar::OnPointerReleased(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    if (!bounds_.contains(e.position.x, e.position.y)) return;
    int idx = ItemAt(e.position.x);
    if (idx < 0) return;
    e.handled = true;
    // Clicking the already-open title closes it; clicking a closed title opens it.
    if (idx == openIndex_)
        CloseMenu();
    else
        OpenMenu(idx);
}

RectDip MenuBar::VisibleTitleRect(float titleX, float titleW, const RectDip& bar) {
    // At or past the right edge (or degenerate) → nothing of this title is visible.
    // Return a CLEAN empty rect rather than letting the clamp below produce a
    // negative width: isEmpty() would catch that too, but a caller doing arithmetic
    // on .w would get nonsense.
    if (titleW <= 0.0f || titleX >= bar.right()) return RectDip{};
    const float right = titleX + titleW > bar.right() ? bar.right() : titleX + titleW;
    return RectDip{titleX, bar.y, right - titleX, bar.h};
}

void MenuBar::Render(const DrawingContext& dc) {
    if (!Dwrite() || items_.empty()) return;
    const ColorTokens& pal = Theme().colors;

    // Default format is centered / no-wrap — exactly what the titles want.
    IDWriteTextFormat* fmt = Dwrite()->Format(kFontSize);
    if (!fmt) return;

    // Confine everything to the bar: when the window is too narrow to fit every
    // title, the trailing ones must not spill past the right edge. Belt to
    // VisibleTitleRect's braces — it clamps the highlight fill, while this also
    // catches the rounded corners' antialiased fringe and the text (whose layout rect
    // stays full-width on purpose, so a partly-visible title's glyphs are cut off
    // rather than re-centered into the narrower slice).
    ClipGuard clip = dc.PushClip(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()));

    float x = bounds_.x;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const TopItem& it = items_[i];
        const RectDip vis = VisibleTitleRect(x, it.width, bounds_);
        // Titles are laid out left to right, so once one is fully out, so are the rest.
        if (vis.isEmpty()) break;

        // Highlight the open title (stronger) or the hovered one (subtle).
        D2D1_RECT_F hi = D2D1::RectF(vis.x, vis.y, vis.right(), vis.bottom());
        if (i == openIndex_) {
            dc.FillRoundedRect(D2D1::RoundedRect(hi, kCornerRadius, kCornerRadius),
                               D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.18f));
        } else if (i == hovered_) {
            dc.FillRoundedRect(D2D1::RoundedRect(hi, kCornerRadius, kCornerRadius),
                               D2D1::ColorF(pal.accent.r, pal.accent.g, pal.accent.b, 0.10f));
        }

        D2D1_COLOR_F titleColor = EffectiveForeground(pal.textPrimary);
        dc.DrawText(it.title.c_str(), static_cast<UINT32>(it.title.size()), fmt,
                    D2D1::RectF(x, bounds_.y, x + it.width, bounds_.bottom()),
                    titleColor, D2D1_DRAW_TEXT_OPTIONS_CLIP);

        x += it.width;
    }
}

} // namespace fluent
