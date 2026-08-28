// Calendar.cpp
#include "Calendar.h"
#include "../graphics/DrawingContext.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DWriteContext.h"
#include <sstream>
#include <algorithm>
#include <tuple>

namespace fluent {

// --- Date helpers ------------------------------------------------------------

int CalDayOfWeek(int y, int m, int d) {
    // Tomohiko Sakamoto's algorithm: returns 0=Sun, 1=Mon, ..., 6=Sat.
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= (m < 3);
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

void CalAddMonths(int& y, int& m, int& d, int delta) {
    int totalMonths = y * 12 + (m - 1) + delta;
    y = totalMonths / 12;
    m = (totalMonths % 12) + 1;
    // Clamp day to valid range for the new month.
    int maxDay = CalDaysInMonth(y, m);
    if (d > maxDay) d = maxDay;
}

void CalAddDays(int& y, int& m, int& d, int delta) {
    // Walk whole months rather than day-by-day, so a large delta (a 42-cell grid
    // start offset, PageUp by a month) costs a handful of iterations instead of
    // `delta` of them. The month step here deliberately does NOT go through
    // CalAddMonths: that one clamps the day, which is exactly what breaks day
    // normalization (see the note in the header).
    d += delta;

    // Carry forward: while the day is past the end of its month, subtract that
    // month's length and advance one month.
    while (d > CalDaysInMonth(y, m)) {
        d -= CalDaysInMonth(y, m);
        if (++m > 12) { m = 1; ++y; }
    }

    // Borrow backward: while the day is before the 1st, step to the previous
    // month and add ITS length. The order matters — the length added must be the
    // previous month's, not the one we came from.
    while (d < 1) {
        if (--m < 1) { m = 12; --y; }
        d += CalDaysInMonth(y, m);
    }
}

// --- Calendar ----------------------------------------------------------------

Calendar::Calendar() {
    // Initialize to today.
    SYSTEMTIME st;
    GetLocalTime(&st);
    selYear_ = st.wYear;
    selMonth_ = st.wMonth;
    selDay_ = st.wDay;
    viewYear_ = selYear_;
    viewMonth_ = selMonth_;
    viewDay_ = selDay_;

    SetFocusable(true);
}

void Calendar::SetSelectedDate(int year, int month, int day) {
    // Reject a date the constraints forbid, programmatic caller or not (P1-12/13).
    // The alternative — accept it and let Render fade it — would leave the control
    // reporting a selection the user can never reach by clicking, and would let a
    // stale SetSelectedDate silently defeat a min/max set afterwards.
    if (!IsDateEnabled(year, month, day)) return;

    selYear_ = year;
    selMonth_ = month;
    selDay_ = day;
    viewYear_ = year;
    viewMonth_ = month;
    viewDay_ = day;
    Invalidate();
}

void Calendar::GetSelectedDate(int& year, int& month, int& day) const {
    year = selYear_;
    month = selMonth_;
    day = selDay_;
}

void Calendar::SetViewedMonth(int year, int month) {
    viewYear_ = year;
    viewMonth_ = month;
    Invalidate();
}

void Calendar::PreviousMonth() {
    CalAddMonths(viewYear_, viewMonth_, viewDay_, -1);
    Invalidate();
}

void Calendar::NextMonth() {
    CalAddMonths(viewYear_, viewMonth_, viewDay_, 1);
    Invalidate();
}

// --- Date constraints (P1-12, P1-13) -----------------------------------------

void Calendar::SetMinDate(int year, int month, int day) {
    hasMinDate_ = true;
    minYear_ = year;
    minMonth_ = month;
    minDay_ = day;
    Invalidate();
}

void Calendar::SetMaxDate(int year, int month, int day) {
    hasMaxDate_ = true;
    maxYear_ = year;
    maxMonth_ = month;
    maxDay_ = day;
    Invalidate();
}

void Calendar::ClearMinDate() {
    hasMinDate_ = false;
    Invalidate();
}

void Calendar::ClearMaxDate() {
    hasMaxDate_ = false;
    Invalidate();
}

void Calendar::GetMinDate(int& year, int& month, int& day) const {
    year = minYear_;
    month = minMonth_;
    day = minDay_;
}

void Calendar::GetMaxDate(int& year, int& month, int& day) const {
    year = maxYear_;
    month = maxMonth_;
    day = maxDay_;
}

void Calendar::SetBlackoutDates(const std::vector<std::tuple<int,int,int>>& dates) {
    blackoutDates_ = dates;
    std::sort(blackoutDates_.begin(), blackoutDates_.end());
    Invalidate();
}

void Calendar::ClearBlackoutDates() {
    blackoutDates_.clear();
    Invalidate();
}

bool Calendar::IsDateBlackedOut(int y, int m, int d) const {
    auto it = std::lower_bound(blackoutDates_.begin(), blackoutDates_.end(),
                               std::make_tuple(y, m, d));
    return it != blackoutDates_.end() && *it == std::make_tuple(y, m, d);
}

bool Calendar::IsDateEnabled(int y, int m, int d) const {
    // Check min/max bounds.
    if (hasMinDate_) {
        if (y < minYear_ || (y == minYear_ && m < minMonth_) ||
            (y == minYear_ && m == minMonth_ && d < minDay_)) {
            return false;
        }
    }
    if (hasMaxDate_) {
        if (y > maxYear_ || (y == maxYear_ && m > maxMonth_) ||
            (y == maxYear_ && m == maxMonth_ && d > maxDay_)) {
            return false;
        }
    }
    // Check blackout list.
    if (IsDateBlackedOut(y, m, d)) {
        return false;
    }
    return true;
}

void Calendar::CellDate(int i, int& y, int& m, int& d) const {
    if (i < 0 || i >= 42) {
        y = m = d = 0;
        return;
    }

    // The grid starts on the Sunday on or before the 1st of the viewed month, so
    // cell i is (1st of month) + (i - dow) days, where dow is the 1st's weekday.
    // CalAddDays handles both signs and every month/year boundary, so there is no
    // normalization loop here to get wrong (an earlier version normalized by hand
    // through CalAddMonths and produced Aug 31 for every cell past the overflow —
    // CalAddMonths clamps the day, which is right for month navigation and wrong
    // for day arithmetic. See the comment on CalAddDays).
    y = viewYear_;
    m = viewMonth_;
    d = 1;
    CalAddDays(y, m, d, i - CalDayOfWeek(viewYear_, viewMonth_, 1));
}

int Calendar::CellIndexOf(int y, int m, int d) const {
    for (int i = 0; i < 42; ++i) {
        int cy, cm, cd;
        CellDate(i, cy, cm, cd);
        if (cy == y && cm == m && cd == d) return i;
    }
    return -1;
}

RectDip Calendar::CellRect(int cellIndex) const {
    if (cellIndex < 0 || cellIndex >= 42) return RectDip();
    int row = cellIndex / 7;
    int col = cellIndex % 7;
    float x = bounds_.x + col * cellW_;
    float y = bounds_.y + navH_ + hdrH_ + row * cellH_;
    return RectDip(x, y, cellW_, cellH_);
}

int Calendar::HitTestCell(float dipX, float dipY) const {
    // dipX/dipY are control-relative.
    float gridY = dipY - navH_ - hdrH_;
    if (gridY < 0.0f) return -1;
    if (dipX < 0.0f || dipX >= 7 * cellW_) return -1;
    if (gridY >= 6 * cellH_) return -1;

    int col = static_cast<int>(dipX / cellW_);
    int row = static_cast<int>(gridY / cellH_);
    if (col < 0 || col >= 7 || row < 0 || row >= 6) return -1;
    return row * 7 + col;
}

void Calendar::Measure(float availW, float availH) {
    float w = 7 * cellW_;
    float h = navH_ + hdrH_ + 6 * cellH_;
    SetDesired(SizeDip(w, h));
}

void Calendar::Arrange(const RectDip& finalRect) {
    SetBounds(finalRect);
}

void Calendar::Render(const DrawingContext& dc) {
    const ThemeSnapshot& th = Theme();
    const ColorTokens& colors = th.colors;
    const float corner = EffectiveCornerRadius();

    // Background.
    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        corner, corner);
    dc.FillRoundedRect(bg, EffectiveBackground(colors.cardFill));

    // Navigation bar.
    float navY = bounds_.y;

    // Header text: "2026年8月"
    std::wostringstream oss;
    oss << viewYear_ << L"年" << viewMonth_ << L"月";
    std::wstring headerText = oss.str();

    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            D2D1_RECT_F textRect = D2D1::RectF(
                bounds_.x + 40, navY,
                bounds_.right() - 40, navY + navH_);
            dc.DrawText(headerText.c_str(), static_cast<UINT32>(headerText.size()),
                       fmt, textRect, EffectiveForeground(colors.textPrimary));
        }

        // Navigation arrows.
        if (auto* arrowFmt = dw->Format(16.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_TEXT_ALIGNMENT_CENTER,
                                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            const D2D1_COLOR_F arrowColor = EffectiveForeground(colors.textPrimary);
            D2D1_RECT_F prevRect = D2D1::RectF(bounds_.x, navY,
                                               bounds_.x + 40, navY + navH_);
            dc.DrawText(L"<", 1, arrowFmt, prevRect, arrowColor);

            D2D1_RECT_F nextRect = D2D1::RectF(bounds_.right() - 40, navY,
                                               bounds_.right(), navY + navH_);
            dc.DrawText(L">", 1, arrowFmt, nextRect, arrowColor);
        }
    }

    // Weekday header.
    const wchar_t* weekdays[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
    float hdrY = bounds_.y + navH_;

    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            const D2D1_COLOR_F weekdayColor = EffectiveForeground(colors.textSecondary);
            for (int col = 0; col < 7; ++col) {
                D2D1_RECT_F r = D2D1::RectF(
                    bounds_.x + col * cellW_, hdrY,
                    bounds_.x + (col + 1) * cellW_, hdrY + hdrH_);
                dc.DrawText(weekdays[col], 1, fmt, r, weekdayColor);
            }
        }
    }

    // Day cells.
    SYSTEMTIME today;
    GetLocalTime(&today);

    if (DWriteContext* dw = Dwrite()) {
        if (auto* fmt = dw->Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                   DWRITE_TEXT_ALIGNMENT_CENTER,
                                   DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) {
            for (int i = 0; i < 42; ++i) {
                int y, m, d;
                CellDate(i, y, m, d);

                bool isCurrentMonth = (y == viewYear_ && m == viewMonth_);
                bool isToday = (y == today.wYear && m == today.wMonth && d == today.wDay);
                bool isSelected = (y == selYear_ && m == selMonth_ && d == selDay_);
                bool isEnabled = IsDateEnabled(y, m, d);

                RectDip cellRect = CellRect(i);
                D2D1_ROUNDED_RECT cellRR = D2D1::RoundedRect(
                    D2D1::RectF(cellRect.x, cellRect.y,
                               cellRect.right(), cellRect.bottom()),
                    4.0f, 4.0f);

                // Background for selected date (only if enabled).
                if (isSelected && isEnabled) {
                    dc.FillRoundedRect(cellRR, colors.accent);
                }
                // Hover highlight (only on enabled dates).
                else if (isEnabled && i == hoverCell_ && hoverFade_ > 0.01f) {
                    D2D1_COLOR_F hoverColor = colors.controlFillHover;
                    hoverColor.a *= static_cast<float>(hoverFade_);
                    dc.FillRoundedRect(cellRR, hoverColor);
                }

                // Ring for today (when not selected).
                if (isToday && !isSelected && isEnabled) {
                    dc.DrawRoundedRect(cellRR, EffectiveAccentColor(colors.accent), 1.0f);
                }

                // Day number text.
                std::wstring dayText = std::to_wstring(d);
                D2D1_RECT_F textRect = D2D1::RectF(
                    cellRect.x, cellRect.y,
                    cellRect.right(), cellRect.bottom());

                // Text color: disabled dates are faded gray, others follow original logic.
                D2D1_COLOR_F textColor;
                if (!isEnabled) {
                    textColor = colors.textSecondary;
                    textColor.a *= 0.5f;  // 50% opacity for disabled
                } else if (HasForeground()) {
                    textColor = EffectiveForeground();
                } else if (isSelected) {
                    textColor = D2D1::ColorF(D2D1::ColorF::White);
                } else if (isCurrentMonth) {
                    textColor = colors.textPrimary;
                } else {
                    textColor = colors.textSecondary;
                }

                dc.DrawText(dayText.c_str(), static_cast<UINT32>(dayText.size()),
                           fmt, textRect, textColor);

                // Strikethrough for disabled dates.
                if (!isEnabled) {
                    float midY = cellRect.y + cellRect.h * 0.5f;
                    D2D1_COLOR_F strikeColor = colors.textSecondary;
                    strikeColor.a *= 0.5f;
                    dc.DrawLine(
                        D2D1::Point2F(cellRect.x + 6, midY),
                        D2D1::Point2F(cellRect.right() - 6, midY),
                        strikeColor, 1.0f);
                }
            }
        }
    }
}

void Calendar::OnPointerPressed(PointerEventArgs& e) {
    if (e.handled) return;

    float relX = e.position.x - bounds_.x;
    float relY = e.position.y - bounds_.y;

    // Check navigation buttons.
    if (relY >= 0 && relY < navH_) {
        if (relX >= 0 && relX < 40) {
            PreviousMonth();
            e.handled = true;
            return;
        }
        if (relX >= bounds_.w - 40 && relX < bounds_.w) {
            NextMonth();
            e.handled = true;
            return;
        }
    }

    // Check day cells. A disabled date (outside min/max or blacked out) swallows
    // the click without changing the selection — the same "click lands, nothing
    // happens" feel as a disabled button, rather than letting the click fall
    // through to whatever is behind the calendar.
    int cell = HitTestCell(relX, relY);
    if (cell >= 0) {
        int y, m, d;
        CellDate(cell, y, m, d);
        if (IsDateEnabled(y, m, d)) {
            SetSelectedDate(y, m, d);
            RoutedEventArgs args;
            args.source = this;
            args.originalSource = this;
            selectionChanged_.Raise(*this, args);
        }
        e.handled = true;
    }
}

void Calendar::OnKeyDownRouted(KeyEventArgs& e) {
    if (e.handled) return;

    int delta = 0;
    switch (e.vk) {
    case VK_LEFT:  delta = -1; break;
    case VK_RIGHT: delta = 1; break;
    case VK_UP:    delta = -7; break;
    case VK_DOWN:  delta = 7; break;
    case VK_RETURN: {
        RoutedEventArgs args;
        args.source = this;
        args.originalSource = this;
        selectionChanged_.Raise(*this, args);
        e.handled = true;
        return;
    }
    default:
        return;
    }

    // Move selection by delta days. Skip over disabled dates — keep stepping
    // until we land on an enabled one (or hit a sanity limit to prevent infinite
    // loops when all dates are disabled). Same feel as skipping disabled items
    // in a ComboBox dropdown.
    int y = selYear_, m = selMonth_, d = selDay_;
    int steps = 0;
    const int MAX_STEPS = 365;  // sanity limit to prevent infinite loop
    do {
        CalAddDays(y, m, d, delta);
        steps++;
    } while (!IsDateEnabled(y, m, d) && steps < MAX_STEPS);

    // Only commit the move if we found an enabled date.
    if (IsDateEnabled(y, m, d)) {
        SetSelectedDate(y, m, d);
    }
    e.handled = true;
}

bool Calendar::WantsAnimationTick() const {
    float target = (hoverCell_ >= 0) ? 1.0f : 0.0f;
    return hoverFade_.Animating(target);
}

void Calendar::OnPointerMoved(PointerEventArgs& e) {
    if (e.handled) return;

    float relX = e.position.x - bounds_.x;
    float relY = e.position.y - bounds_.y;
    int cell = HitTestCell(relX, relY);

    // A disabled cell reads as "no hover" rather than "hovered but not painted".
    // Render already skips the highlight for a disabled date, but leaving
    // hoverCell_ pointing at one keeps WantsAnimationTick() true for a fade that
    // never becomes visible — a tick per frame while the pointer rests on a
    // blacked-out day, against the "idle costs zero" rule.
    if (cell >= 0) {
        int y, m, d;
        CellDate(cell, y, m, d);
        if (!IsDateEnabled(y, m, d)) cell = -1;
    }

    if (cell != hoverCell_) {
        hoverCell_ = cell;
        Invalidate();
    }
}

void Calendar::OnPointerLeft() {
    if (hoverCell_ >= 0) {
        hoverCell_ = -1;
        Invalidate();
    }
}

void Calendar::OnAnimationTick(float dtSec) {
    float target = (hoverCell_ >= 0) ? 1.0f : 0.0f;
    hoverFade_.Approach(target, dtSec, Theme().motion.tintTau);
    Invalidate();
}

} // namespace fluent
