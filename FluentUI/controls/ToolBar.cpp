// ToolBar.cpp
#include "ToolBar.h"
#include "Button.h"
#include "MenuFlyout.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DrawingContext.h"
#include "../core/UIContext.h"
#include "../window/WindowServices.h"
#include "../services/PopupGeometry.h"
#include "../controls/MenuFlyout.h"
#include <algorithm>

namespace fluent {

namespace {
constexpr float kSeparatorWidth = 1.0f;
constexpr float kSeparatorHeight = 16.0f;
constexpr float kButtonHeight = 32.0f;
constexpr float kButtonPadX = 12.0f;
constexpr float kItemSpacing = 4.0f;
}  // namespace

ToolBar::ToolBar() {
    SetFocusable(true);  // so the overflow "..." can receive keyboard focus
}

void ToolBar::AddButton(std::unique_ptr<Button> btn, std::function<void()> onClick) {
    if (!btn) return;
    // ToolBar buttons are style-specialized: transparent and borderless at rest,
    // a quiet fill on hover (the WinUI CommandBar look). Set here rather than at
    // every call site; a plain Standard button inside a command strip reads as a
    // big white tile, which is exactly what the first cut looked like.
    btn->SetKind(Button::Kind::Subtle);
    // The button joins children_ (owned there) so it attaches, renders, and
    // hit-tests through the normal Panel paths. items_ keeps the borrowed pointer
    // plus the overflow bookkeeping (width, separator flags) in strip order.
    Button* raw = btn.get();
    Item item;
    item.button = raw;
    item.clickHandler = std::move(onClick);
    item.isSeparator = false;
    // Wire the button's Click event to the stored handler so the overflow menu
    // can invoke the same action. The Event::Subscribe API takes a plain function
    // pointer + owner, so we use a static thunk that reads the handler from the
    // Item. The returned Subscription MUST be stored (RAII) or the registration
    // dies with the temporary and the click never fires.
    if (item.clickHandler) {
        item.clickSub = raw->Click().Subscribe(this, &ToolBar::OnButtonClickThunk);
    }
    items_.push_back(std::move(item));
    Add(std::move(btn));
    InvalidateMeasure();
}

void ToolBar::AddSeparator() {
    Item item;
    item.isSeparator = true;
    items_.push_back(std::move(item));
    InvalidateMeasure();
}

void ToolBar::SetOverflowItems(std::vector<MenuItem> items) {
    overflowItems_ = std::move(items);
}

SizeDip ToolBar::MeasureOverride(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    float x = 0.0f;
    int visCount = 0;
    bool overflow = false;

    for (size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        if (item.isSeparator) {
            item.width = kSeparatorWidth + kItemSpacing * 2.0f;
        } else if (item.button) {
            item.button->MeasureCached(availW, kButtonHeight);
            item.width = (item.button->Desired().w) + kItemSpacing;
        }

        // Reserve space for the overflow button if this is not the first item and
        // adding it would exceed the available width.
        const bool needOverflowBtn = (visCount > 0) &&
            (x + item.width + overflowBtnWidth_ > availW);
        if (needOverflowBtn) {
            overflow = true;
            break;
        }

        x += item.width;
        ++visCount;
    }

    // If we did not fit all items, reserve the overflow button width.
    if (overflow || visCount < static_cast<int>(items_.size())) {
        overflow = true;
        x += overflowBtnWidth_;
    }

    visibleCount_ = visCount;
    overflowActive_ = overflow;
    return SizeDip{x, kButtonHeight};
}

void ToolBar::ArrangeOverride(const RectDip& content) {
    float x = content.x;
    const float y = content.y + (content.h - kButtonHeight) * 0.5f;

    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        Item& item = items_[i];
        const bool visible = i < visibleCount_;
        if (item.isSeparator) {
            // Separator is drawn by ToolBar::Render, not a child element.
            if (visible) x += item.width;
            continue;
        }
        if (item.button) {
            if (visible) {
                const float w = item.button->Desired().w;
                item.button->Arrange(RectDip{x, y, w, kButtonHeight});
                x += w + kItemSpacing;
            } else {
                // Overflowed buttons are parked far off-screen rather than hidden:
                // SetVisible(false) would zero their Desired size and corrupt the
                // overflow measurement on the next pass. An off-screen empty rect
                // keeps their Desired intact while the render/hit-test culls skip
                // them (Button's 3.75 DIP visual overflow stays far above y=0).
                item.button->Arrange(RectDip{0.0f, -10000.0f, 0.0f, 0.0f});
            }
        }
    }

    if (overflowActive_) {
        overflowBtnRect_ = RectDip{x, y, overflowBtnWidth_, kButtonHeight};
    } else {
        overflowBtnRect_ = RectDip{};
    }
}

void ToolBar::ComputeOverflow(float availW) {
    UNREFERENCED_PARAMETER(availW);
    // Overflow is computed in MeasureOverride; this method exists for future
    // dynamic re-layout when the window resizes.
    InvalidateMeasure();
}

std::vector<MenuItem> ToolBar::BuildOverflowItems() const {
    std::vector<MenuItem> out;
    for (size_t i = visibleCount_; i < items_.size(); ++i) {
        const Item& item = items_[i];
        if (item.isSeparator) {
            out.push_back(MenuItem::Sep());
        } else if (item.button) {
            MenuItem mi;
            mi.text = item.button->Text();
            // Mirror the button's Click handler into the menu item. We store a
            // direct handler lambda on the button at AddButton time (see below),
            // so the overflow menu can invoke the same action without needing to
            // reach into the button's protected activation path.
            mi.onInvoke = item.clickHandler;
            out.push_back(std::move(mi));
        }
    }
    return out;
}

void ToolBar::ShowOverflowMenu() {
    if (!overflowActive_) return;
    if (!overflowFlyout_) {
        overflowFlyout_ = std::make_unique<MenuFlyout>();
        // Hand the flyout the tree context IMMEDIATELY. The only other place it
        // gets one is OnAttachedToTree, which already ran before this flyout
        // existed — so the very first click on "..." opened nothing (no window
        // services), and only after a detach/attach cycle (e.g. switching tabs,
        // which is exactly what the user stumbled into) did the flyout start
        // working.
        if (IsAttached())
            overflowFlyout_->SetOwnerContext(Context());
    }
    overflowFlyout_->SetItems(BuildOverflowItems());

    WindowServices* win = Window();
    if (!win || !win->Hwnd()) return;

    // overflowBtnRect_ is in window DIPs; ShowBelow needs physical SCREEN pixels.
    // The old code multiplied by the DPI scale and stopped there — never adding the
    // window's own screen position — so the menu opened near the top-left of the
    // MONITOR instead of under the "...". Same conversion ComboBox uses.
    RECT rcWindow;
    GetWindowRect(win->Hwnd(), &rcWindow);
    RECT anchor = AnchorScreenRect(rcWindow.left, rcWindow.top,
                                   overflowBtnRect_.x,
                                   overflowBtnRect_.y + overflowBtnRect_.h,
                                   overflowBtnRect_.w, 4.0f,
                                   win->DpiScale());
    overflowFlyout_->ShowBelow(anchor);
}

void ToolBar::Render(const DrawingContext& dc) {
    // Buttons are child elements now: the base walk renders each one (and culls the
    // overflowed ones, which are parked off-screen with empty bounds).
    Panel::Render(dc);

    // Draw separators (they are not child elements).
    float x = bounds_.x;
    const float yCenter = bounds_.y + bounds_.h * 0.5f;
    const float sepTop = yCenter - kSeparatorHeight * 0.5f;

    for (int i = 0; i < visibleCount_ && i < static_cast<int>(items_.size()); ++i) {
        const Item& item = items_[i];
        if (item.isSeparator) {
            const float sepX = x + kItemSpacing;
            dc.FillRect(
                D2D1::RectF(sepX, sepTop, sepX + kSeparatorWidth,
                            sepTop + kSeparatorHeight),
                Theme().colors.controlStrokeDefault);
        }
        x += item.width;
    }

    // Draw the overflow "..." button when active.
    if (overflowActive_) {
        const ColorTokens& pal = Theme().colors;
        const D2D1_COLOR_F fill = overflowBtnHover_
            ? pal.controlFillHover
            : pal.controlFillDefault;
        dc.FillRoundedRect(
            D2D1::RoundedRect(
                D2D1::RectF(overflowBtnRect_.x, overflowBtnRect_.y,
                            overflowBtnRect_.right(), overflowBtnRect_.bottom()),
                4.0f, 4.0f),
            fill);

        // Draw "..." text centered in the button.
        if (Dwrite()) {
            if (auto* fmt = Dwrite()->Format(14.0f, DWRITE_FONT_WEIGHT_BOLD,
                                             DWRITE_TEXT_ALIGNMENT_CENTER,
                                             DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
                dc.DrawText(L"...", 3, fmt,
                            D2D1::RectF(overflowBtnRect_.x, overflowBtnRect_.y,
                                        overflowBtnRect_.right(),
                                        overflowBtnRect_.bottom()),
                            pal.textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }
}

UIElement* ToolBar::HitTestDeep(float dipX, float dipY) {
    if (!HitTest(dipX, dipY)) return nullptr;
    // Visible buttons first (children_; overflowed ones are parked off-screen with
    // empty bounds and can never be hit).
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (!*it) continue;
        if (UIElement* hit = (*it)->HitTestDeep(dipX, dipY)) return hit;
    }
    // The overflow "..." button is drawn by ToolBar itself, so it has no child
    // element to hit — test its rect directly and return the ToolBar.
    if (overflowActive_ && overflowBtnRect_.contains(dipX, dipY)) return this;
    // Empty strip area: return null so wheel events pass through to a surrounding
    // ScrollPanel instead of being swallowed by a non-interactive strip.
    return nullptr;
}

void ToolBar::OnPointerPressed(PointerEventArgs& e) {
    if (e.button != PointerButton::Left) return;
    if (overflowActive_ && overflowBtnRect_.contains(e.position.x, e.position.y)) {
        ShowOverflowMenu();
        e.handled = true;
    }
}

void ToolBar::OnPointerMoved(PointerEventArgs& e) {
    const bool hover = overflowActive_ &&
        overflowBtnRect_.contains(e.position.x, e.position.y);
    if (hover != overflowBtnHover_) {
        overflowBtnHover_ = hover;
        Invalidate();
    }
}

void ToolBar::OnPointerLeft() {
    if (overflowBtnHover_) {
        overflowBtnHover_ = false;
        Invalidate();
    }
}

void ToolBar::OnButtonClickThunk(void* owner, Button& btn, RoutedEventArgs&) {
    auto* self = static_cast<ToolBar*>(owner);
    for (auto& item : self->items_) {
        if (item.button == &btn && item.clickHandler) {
            item.clickHandler();
            return;
        }
    }
}

void ToolBar::OnAttachedToTree() {
    // Attach the overflow flyout to the tree context so it can render/position.
    if (overflowFlyout_)
        overflowFlyout_->SetOwnerContext(Context());
}

void ToolBar::OnDetachedFromTree() {
    if (overflowFlyout_)
        overflowFlyout_->Close();
}

} // namespace fluent
