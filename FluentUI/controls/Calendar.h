// Calendar.h — self-drawing month-view calendar control.
//
// Draws a 7-column grid entirely in Render(): navigation bar, weekday header,
// and 42 day cells (6 weeks). Doesn't use nested children — pure D2D rendering,
// which keeps hit-testing simple and the control lightweight.
//
// Date arithmetic uses helper functions CalDaysInMonth, CalDayOfWeek, and
// CalAddMonths — all exposed in the header so tests can verify them headlessly.
#pragma once

#include "../core/Control.h"
#include "../base/Event.h"
#include "../animation/AnimatedValue.h"
#include "../input/RoutedEvent.h"
#include <vector>
#include <tuple>

namespace fluent {

// --- Pure date helpers (testable without a device context) -------------------

// True when `y` is a leap year.
inline bool CalIsLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

// Days in month 1-12 of year y.
inline int CalDaysInMonth(int y, int m) {
    const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && CalIsLeapYear(y)) ? 29 : days[m];
}

// Day of week for year y, month m (1-12), day d. Returns 0=Sunday … 6=Saturday.
// Uses Tomohiko Sakamoto's algorithm.
int CalDayOfWeek(int y, int m, int d);

// Add `delta` months to (y, m, d), clamping the day to the new month's length.
// Clamping is what month navigation wants: Jan 31 + 1 month is Feb 28/29, not
// Mar 3. It is NOT usable for normalizing a day-of-month that has overflowed —
// use CalAddDays for that. Reusing this one for day arithmetic is a real bug that
// shipped here once: normalizing "Jul 69" by subtracting 31 and rolling the month
// gave "Aug 38", which this function then clamped to Aug 31, so every calendar
// cell from that point on reported the same date.
void CalAddMonths(int& y, int& m, int& d, int delta);

// Add `delta` days to (y, m, d) — the exact civil-date arithmetic that day-level
// navigation and grid-cell dates need. Never clamps: rolling past the end of a
// month carries into the next one (and past December into the next year), and the
// same in reverse for a negative delta. `delta` may be any magnitude.
void CalAddDays(int& y, int& m, int& d, int delta);

// --- Calendar control --------------------------------------------------------

class Calendar : public Control {
public:
    Calendar();

    // Selected date. Defaults to today at construction.
    void SetSelectedDate(int year, int month, int day);
    void GetSelectedDate(int& year, int& month, int& day) const;
    int SelectedYear() const { return selYear_; }
    int SelectedMonth() const { return selMonth_; }
    int SelectedDay() const { return selDay_; }

    // The month currently displayed (may differ from the selected date's month).
    int ViewedYear() const { return viewYear_; }
    int ViewedMonth() const { return viewMonth_; }
    void SetViewedMonth(int year, int month);

    // Navigate one month forward/backward.
    void PreviousMonth();
    void NextMonth();

    // The date of cell index `i` (0-41, row-major, Sunday-first). Public for tests.
    void CellDate(int i, int& y, int& m, int& d) const;

    // Which cell index (0-41) the given date occupies, or -1 if not visible.
    int CellIndexOf(int y, int m, int d) const;

    // Cell geometry in control-relative DIPs. Public for tests.
    RectDip CellRect(int cellIndex) const;

    // The cell at point (dipX, dipY) relative to the control, or -1.
    int HitTestCell(float dipX, float dipY) const;

    // Date range constraints (P1-12). Dates outside [min, max] are disabled.
    // Default: no constraint (year 1000-9999 effectively unbounded).
    void SetMinDate(int year, int month, int day);
    void SetMaxDate(int year, int month, int day);
    void ClearMinDate();
    void ClearMaxDate();
    bool HasMinDate() const { return hasMinDate_; }
    bool HasMaxDate() const { return hasMaxDate_; }
    void GetMinDate(int& year, int& month, int& day) const;
    void GetMaxDate(int& year, int& month, int& day) const;

    // Blackout dates (P1-13) — specific dates that cannot be selected.
    // Stored as a sorted vector of (year, month, day) triples for O(log n) lookup.
    void SetBlackoutDates(const std::vector<std::tuple<int,int,int>>& dates);
    void ClearBlackoutDates();
    bool IsDateBlackedOut(int y, int m, int d) const;

    // True when the date is selectable (within min/max and not blacked out).
    bool IsDateEnabled(int y, int m, int d) const;

    // Raised when the user selects a date (click or Enter). Payload is unused.
    Event<Calendar, RoutedEventArgs>& SelectionChanged() { return selectionChanged_; }

    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;
    void Render(const DrawingContext& dc) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerLeft() override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    bool WantsAnimationTick() const override;
    void OnAnimationTick(float dtSec) override;

protected:
    void OnThemeChanged() override { Invalidate(); }

private:
    // Layout geometry (computed in Measure, used in Render/HitTest).
    float cellW_ = 36.0f;
    float cellH_ = 32.0f;
    float navH_  = 36.0f;
    float hdrH_  = 24.0f;

    // Date state.
    int selYear_  = 2000, selMonth_  = 1, selDay_  = 1;
    int viewYear_ = 2000, viewMonth_ = 1, viewDay_ = 1;

    // Date constraints (P1-12, P1-13).
    bool hasMinDate_ = false;
    bool hasMaxDate_ = false;
    int minYear_ = 1000, minMonth_ = 1, minDay_ = 1;
    int maxYear_ = 9999, maxMonth_ = 12, maxDay_ = 31;
    std::vector<std::tuple<int,int,int>> blackoutDates_;  // sorted

    // Hover state and animation.
    int hoverCell_ = -1;
    AnimatedValue hoverFade_{0.0f};

    Event<Calendar, RoutedEventArgs> selectionChanged_;
};

} // namespace fluent
