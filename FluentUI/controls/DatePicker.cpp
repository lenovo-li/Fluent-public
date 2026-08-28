// DatePicker.cpp
#include "DatePicker.h"
#include "Calendar.h"
#include "../services/PopupHost.h"
#include "../services/PopupGeometry.h"
#include "../graphics/DrawingContext.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DWriteContext.h"
#include "../window/WindowServices.h"
#include <sstream>
#include <iomanip>

namespace fluent {

const char* kTag = "DatePicker";

DatePicker::DatePicker() {
    // Initialize to today.
    SYSTEMTIME st;
    GetLocalTime(&st);
    year_ = st.wYear;
    month_ = st.wMonth;
    day_ = st.wDay;
}

DatePicker::~DatePicker() = default;

void DatePicker::SetSelectedDate(int year, int month, int day) {
    if (year_ == year && month_ == month && day_ == day) return;
    year_ = year;
    month_ = month;
    day_ = day;
    Invalidate();
}

void DatePicker::OnActivate() {
    // ButtonBase calls this on click or Space/Enter.
    OpenCalendar();
}

void DatePicker::OpenCalendar() {
    if (!window_ || popupOpen_) return;

    // Create PopupHost on first open (ComboBox pattern).
    if (!popup_) {
        popup_ = std::make_unique<PopupHost>();
        if (FAILED(popup_->Create(window_->Instance(), window_->Hwnd(),
                                  &window_->D2D(), &window_->DWrite()))) {
            TraceMsg(kTag, "OpenCalendar: PopupHost::Create failed");
            popup_.reset();
            return;
        }
        popup_->SetResourceCache(Context().resourceCache);
        popup_->SetTheme(Context().theme);

        // Create the calendar content.
        calendar_ = std::make_shared<Calendar>();
        popup_->SetContent(calendar_.get());
        popup_->SetOnClose([this]() {
            popupOpen_ = false;
        });

        // Wire SelectionChanged: calendar picks a date → close, update, fire event.
        AddContextSubscription(
            calendar_->SelectionChanged().Subscribe(this, [](void* owner, Calendar&, RoutedEventArgs&) {
                auto* self = static_cast<DatePicker*>(owner);
                self->SetSelectedDate(self->calendar_->SelectedYear(),
                                     self->calendar_->SelectedMonth(),
                                     self->calendar_->SelectedDay());
                RoutedEventArgs args;
                args.source = self;
                args.originalSource = self;
                self->dateChanged_.Raise(*self, args);
                self->CloseCalendar();
            })
        );
    }

    // Sync calendar to our selected date.
    calendar_->SetSelectedDate(year_, month_, day_);

    // Position popup below (or above) the button. bounds_ is in window DIPs;
    // PopupHost::Open needs physical SCREEN pixels, so the window's own screen
    // position must be added — the previous code scaled by DPI only and the
    // calendar opened near the top-left corner of the MONITOR instead of under
    // the button. Same conversion as ComboBox/ToolBar.
    RECT rcWindow;
    GetWindowRect(window_->Hwnd(), &rcWindow);
    RECT anchor = AnchorScreenRect(rcWindow.left, rcWindow.top,
                                   bounds_.x, bounds_.y, bounds_.w, bounds_.h,
                                   window_->DpiScale());

    // Calendar is 7 cells × 36 DIP wide, plus nav + header height.
    float popupW = 7 * 36.0f;
    float popupH = 36.0f + 24.0f + 6 * 32.0f;  // nav + hdr + 6 rows

    if (SUCCEEDED(popup_->Open(anchor, popupW, popupH))) {
        popupOpen_ = true;

        // Register light-dismiss callback (click outside closes popup).
        AddContextSubscription(
            window_->RegisterActivePopupDismiss(
                [this](PopupDismissReason reason, HWND otherHwnd, int screenX, int screenY) {
                    if (!popupOpen_) return false;
                    if (reason == PopupDismissReason::Click) {
                        if (popup_ && otherHwnd == popup_->Hwnd())
                            return false;  // click inside popup
                        if (popup_ && popup_->ContainsScreenPoint(screenX, screenY))
                            return false;
                    }
                    CloseCalendar();
                    return true;  // handled
                })
        );
    } else {
        TraceMsg(kTag, "OpenCalendar: PopupHost::Open failed");
    }
}

void DatePicker::CloseCalendar() {
    if (!popup_ || !popupOpen_) return;
    popup_->Close();
    popupOpen_ = false;
}

void DatePicker::AdjustDate(int daysDelta) {
    int y = year_, m = month_, d = day_;
    CalAddDays(y, m, d, daysDelta);

    SetSelectedDate(y, m, d);
    RoutedEventArgs args;
    args.source = this;
    args.originalSource = this;
    dateChanged_.Raise(*this, args);
}

void DatePicker::Measure(float availW, float availH) {
    // Fixed size: enough for "YYYY/MM/DD" plus padding.
    float w = 120.0f;
    float h = 32.0f;
    SetDesired(SizeDip(w, h));
}

void DatePicker::Arrange(const RectDip& finalRect) {
    SetBounds(finalRect);
}

void DatePicker::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& colors = th.colors;
    const float corner = EffectiveCornerRadius();

    // Background: state-driven fill (ButtonBase pattern).
    D2D1_COLOR_F bgColor;
    switch (State()) {
    case VisualState::Hover:
        bgColor = colors.controlFillHover;
        break;
    case VisualState::Pressed:
        bgColor = colors.controlFillPressed;
        break;
    case VisualState::Disabled:
        bgColor = colors.controlFillDefault;
        break;
    default:
        bgColor = colors.controlFillDefault;
        break;
    }

    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);
    dc.FillRoundedRect(bg, EffectiveBackground(bgColor));

    // Border.
    dc.DrawRoundedRect(bg, EffectiveBorderBrush(colors.controlStrokeDefault),
                       EffectiveBorderThickness(1.0f));

    // Focus indicator (when focused).
    if (IsFocused()) {
        D2D1_ROUNDED_RECT focusRect = D2D1::RoundedRect(
            D2D1::RectF(bounds_.x + 1, bounds_.y + 1,
                       bounds_.right() - 1, bounds_.bottom() - 1),
            corner, corner);
        dc.DrawRoundedRect(focusRect, colors.focusStroke, 2.0f);
    }

    // Button text: "YYYY/MM/DD"
    std::wostringstream oss;
    oss << year_ << L"/"
        << std::setw(2) << std::setfill(L'0') << month_ << L"/"
        << std::setw(2) << std::setfill(L'0') << day_;
    std::wstring text = oss.str();

    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F textRect = D2D1::RectF(
                bounds_.x, bounds_.y,
                bounds_.right(), bounds_.bottom());
            D2D1_COLOR_F textColor = EffectiveForeground(colors.textPrimary);
            dc.DrawText(text.c_str(), static_cast<UINT32>(text.size()),
                       fmt, textRect, textColor);
        }
    }
}

void DatePicker::OnKeyDownRouted(KeyEventArgs& e) {
    if (e.handled) return;

    // Arrow keys adjust the date by ±1 day. Handled here rather than delegating,
    // since ButtonBase only knows about the Space/Enter activation gesture.
    switch (e.vk) {
    case VK_UP:
        AdjustDate(1);
        e.handled = true;
        return;
    case VK_DOWN:
        AdjustDate(-1);
        e.handled = true;
        return;
    default:
        break;
    }

    // Space/Enter fall through to ButtonBase → OnActivate → OpenCalendar.
    ButtonBase::OnKeyDownRouted(e);
}

void DatePicker::OnAttachedToTree() {
    ButtonBase::OnAttachedToTree();
    window_ = Window();
}

void DatePicker::OnDetachedFromTree() {
    ButtonBase::OnDetachedFromTree();
    CloseCalendar();
    window_ = nullptr;
}

void DatePicker::OnThemeChanged() {
    Invalidate();
}

} // namespace fluent
