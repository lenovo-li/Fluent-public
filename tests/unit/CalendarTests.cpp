// CalendarTests.cpp — headless unit tests for the Calendar control.
//
// Calendar draws itself entirely in Render(), so everything except the pixels is
// testable without a device: the date helpers, the 42-cell grid mapping, cell
// geometry, hit-testing, month navigation, and keyboard selection.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Calendar.h"

using namespace fluent;

namespace {
void CountSelectionChanged(void* ctx, Calendar&, RoutedEventArgs&) {
    (*static_cast<int*>(ctx))++;
}
} // namespace

// --- Date helpers ------------------------------------------------------------

TEST(Calendar, LeapYearDivisibleByFour) {
    EXPECT_TRUE(CalIsLeapYear(2024));
    EXPECT_TRUE(CalIsLeapYear(2020));
    EXPECT_FALSE(CalIsLeapYear(2023));
}

// The century rule: divisible by 100 is NOT a leap year unless also by 400.
TEST(Calendar, LeapYearCenturyRule) {
    EXPECT_FALSE(CalIsLeapYear(1900));
    EXPECT_FALSE(CalIsLeapYear(2100));
    EXPECT_TRUE(CalIsLeapYear(2000));
    EXPECT_TRUE(CalIsLeapYear(2400));
}

TEST(Calendar, DaysInFebruary) {
    EXPECT_EQ(28, CalDaysInMonth(2023, 2));
    EXPECT_EQ(29, CalDaysInMonth(2024, 2));
    EXPECT_EQ(28, CalDaysInMonth(1900, 2));
    EXPECT_EQ(29, CalDaysInMonth(2000, 2));
}

TEST(Calendar, DaysInThirtyDayMonths) {
    EXPECT_EQ(30, CalDaysInMonth(2024, 4));
    EXPECT_EQ(30, CalDaysInMonth(2024, 6));
    EXPECT_EQ(30, CalDaysInMonth(2024, 9));
    EXPECT_EQ(30, CalDaysInMonth(2024, 11));
}

TEST(Calendar, DaysInThirtyOneDayMonths) {
    EXPECT_EQ(31, CalDaysInMonth(2024, 1));
    EXPECT_EQ(31, CalDaysInMonth(2024, 3));
    EXPECT_EQ(31, CalDaysInMonth(2024, 5));
    EXPECT_EQ(31, CalDaysInMonth(2024, 7));
    EXPECT_EQ(31, CalDaysInMonth(2024, 8));
    EXPECT_EQ(31, CalDaysInMonth(2024, 10));
    EXPECT_EQ(31, CalDaysInMonth(2024, 12));
}

// Sakamoto's algorithm, checked against known dates. 0 = Sunday.
TEST(Calendar, DayOfWeekKnownDates) {
    EXPECT_EQ(1, CalDayOfWeek(2024, 1, 1));   // Mon
    EXPECT_EQ(4, CalDayOfWeek(2024, 8, 1));   // Thu
    EXPECT_EQ(6, CalDayOfWeek(2000, 1, 1));   // Sat
    EXPECT_EQ(2, CalDayOfWeek(2026, 8, 4));   // Tue
}

// January/February are shifted into the previous year by the algorithm; verify
// that adjustment did not break the leap-year handling.
TEST(Calendar, DayOfWeekAcrossLeapDay) {
    EXPECT_EQ(4, CalDayOfWeek(2024, 2, 29));  // Thu
    EXPECT_EQ(5, CalDayOfWeek(2024, 3, 1));   // Fri
}

TEST(Calendar, AddMonthsForward) {
    int y = 2024, m = 8, d = 15;
    CalAddMonths(y, m, d, 1);
    EXPECT_EQ(2024, y);
    EXPECT_EQ(9, m);
    EXPECT_EQ(15, d);
}

TEST(Calendar, AddMonthsBackward) {
    int y = 2024, m = 8, d = 15;
    CalAddMonths(y, m, d, -1);
    EXPECT_EQ(2024, y);
    EXPECT_EQ(7, m);
    EXPECT_EQ(15, d);
}

TEST(Calendar, AddMonthsCrossesYearBackward) {
    int y = 2024, m = 1, d = 15;
    CalAddMonths(y, m, d, -1);
    EXPECT_EQ(2023, y);
    EXPECT_EQ(12, m);
    EXPECT_EQ(15, d);
}

TEST(Calendar, AddMonthsCrossesYearForward) {
    int y = 2024, m = 12, d = 15;
    CalAddMonths(y, m, d, 1);
    EXPECT_EQ(2025, y);
    EXPECT_EQ(1, m);
    EXPECT_EQ(15, d);
}

TEST(Calendar, AddMonthsMultipleYears) {
    int y = 2024, m = 3, d = 10;
    CalAddMonths(y, m, d, 25);
    EXPECT_EQ(2026, y);
    EXPECT_EQ(4, m);
    EXPECT_EQ(10, d);
}

// Jan 31 + 1 month has no Feb 31 to land on, so the day clamps to the month's
// length — and the clamp must respect leap years.
TEST(Calendar, AddMonthsClampsDayToLeapFebruary) {
    int y = 2024, m = 1, d = 31;
    CalAddMonths(y, m, d, 1);
    EXPECT_EQ(2, m);
    EXPECT_EQ(29, d);
}

TEST(Calendar, AddMonthsClampsDayToCommonFebruary) {
    int y = 2023, m = 1, d = 31;
    CalAddMonths(y, m, d, 1);
    EXPECT_EQ(2, m);
    EXPECT_EQ(28, d);
}

TEST(Calendar, AddMonthsClampsDayToThirtyDayMonth) {
    int y = 2024, m = 5, d = 31;
    CalAddMonths(y, m, d, 1);  // May 31 -> June has 30
    EXPECT_EQ(6, m);
    EXPECT_EQ(30, d);
}

// --- Selection and navigation ------------------------------------------------

TEST(Calendar, DefaultsToToday) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    Calendar cal;
    EXPECT_EQ(static_cast<int>(st.wYear), cal.SelectedYear());
    EXPECT_EQ(static_cast<int>(st.wMonth), cal.SelectedMonth());
    EXPECT_EQ(static_cast<int>(st.wDay), cal.SelectedDay());
}

// Setting the selection also moves the view to that month — selecting a date the
// user cannot see would be invisible.
TEST(Calendar, SetSelectedDateMovesView) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    EXPECT_EQ(2026, cal.SelectedYear());
    EXPECT_EQ(8, cal.SelectedMonth());
    EXPECT_EQ(15, cal.SelectedDay());
    EXPECT_EQ(2026, cal.ViewedYear());
    EXPECT_EQ(8, cal.ViewedMonth());
}

// The reverse is not true: paging the view must leave the selection alone.
TEST(Calendar, SetViewedMonthLeavesSelectionAlone) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    cal.SetViewedMonth(2025, 3);
    EXPECT_EQ(2025, cal.ViewedYear());
    EXPECT_EQ(3, cal.ViewedMonth());
    EXPECT_EQ(2026, cal.SelectedYear());
    EXPECT_EQ(8, cal.SelectedMonth());
    EXPECT_EQ(15, cal.SelectedDay());
}

TEST(Calendar, PreviousMonth) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    cal.PreviousMonth();
    EXPECT_EQ(2024, cal.ViewedYear());
    EXPECT_EQ(7, cal.ViewedMonth());
}

TEST(Calendar, NextMonth) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    cal.NextMonth();
    EXPECT_EQ(2024, cal.ViewedYear());
    EXPECT_EQ(9, cal.ViewedMonth());
}

TEST(Calendar, PreviousMonthCrossesYear) {
    Calendar cal;
    cal.SetViewedMonth(2024, 1);
    cal.PreviousMonth();
    EXPECT_EQ(2023, cal.ViewedYear());
    EXPECT_EQ(12, cal.ViewedMonth());
}

TEST(Calendar, NextMonthCrossesYear) {
    Calendar cal;
    cal.SetViewedMonth(2024, 12);
    cal.NextMonth();
    EXPECT_EQ(2025, cal.ViewedYear());
    EXPECT_EQ(1, cal.ViewedMonth());
}

// Paging twelve times must land exactly one year later.
TEST(Calendar, TwelveNextMonthsAdvancesOneYear) {
    Calendar cal;
    cal.SetViewedMonth(2024, 5);
    for (int i = 0; i < 12; ++i) cal.NextMonth();
    EXPECT_EQ(2025, cal.ViewedYear());
    EXPECT_EQ(5, cal.ViewedMonth());
}

// --- Cell mapping ------------------------------------------------------------

// Aug 2024 starts on a Thursday, so the grid opens with four July days and cell 4
// is Aug 1.
TEST(Calendar, CellDateLeadingDaysFromPreviousMonth) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    int y = 0, m = 0, d = 0;
    cal.CellDate(0, y, m, d);
    EXPECT_EQ(2024, y);
    EXPECT_EQ(7, m);
    EXPECT_EQ(28, d);

    cal.CellDate(4, y, m, d);
    EXPECT_EQ(2024, y);
    EXPECT_EQ(8, m);
    EXPECT_EQ(1, d);
}

// A month starting on Sunday needs no leading days at all.
TEST(Calendar, CellDateMonthStartingOnSunday) {
    Calendar cal;
    cal.SetViewedMonth(2024, 9);  // Sep 1 2024 is a Sunday
    int y = 0, m = 0, d = 0;
    cal.CellDate(0, y, m, d);
    EXPECT_EQ(2024, y);
    EXPECT_EQ(9, m);
    EXPECT_EQ(1, d);
}

// The tail of the grid spills into the following month.
TEST(Calendar, CellDateTrailingDaysFromNextMonth) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    int y = 0, m = 0, d = 0;
    cal.CellDate(41, y, m, d);  // last cell: 42 days from Jul 28 => Sep 7
    EXPECT_EQ(2024, y);
    EXPECT_EQ(9, m);
    EXPECT_EQ(7, d);
}

// The leading spill must cross a year boundary correctly.
TEST(Calendar, CellDateLeadingDaysCrossYear) {
    Calendar cal;
    cal.SetViewedMonth(2027, 1);  // Jan 1 2027 is a Friday
    int y = 0, m = 0, d = 0;
    cal.CellDate(0, y, m, d);
    EXPECT_EQ(2026, y);
    EXPECT_EQ(12, m);
    EXPECT_EQ(27, d);
}

// The 42 cells must be strictly consecutive days with no gap or repeat.
TEST(Calendar, CellDatesAreConsecutive) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    int py = 0, pm = 0, pd = 0;
    cal.CellDate(0, py, pm, pd);
    for (int i = 1; i < 42; ++i) {
        int y = 0, m = 0, d = 0;
        cal.CellDate(i, y, m, d);
        // Advance the previous date by one day and compare.
        int ey = py, em = pm, ed = pd + 1;
        if (ed > CalDaysInMonth(ey, em)) {
            ed = 1;
            CalAddMonths(ey, em, ed, 1);
        }
        EXPECT_EQ(ey, y);
        EXPECT_EQ(em, m);
        EXPECT_EQ(ed, d);
        py = y; pm = m; pd = d;
    }
}

TEST(Calendar, CellDateOutOfRangeYieldsZero) {
    Calendar cal;
    int y = 1, m = 1, d = 1;
    cal.CellDate(-1, y, m, d);
    EXPECT_EQ(0, y);
    EXPECT_EQ(0, m);
    EXPECT_EQ(0, d);

    y = m = d = 1;
    cal.CellDate(42, y, m, d);
    EXPECT_EQ(0, y);
    EXPECT_EQ(0, m);
    EXPECT_EQ(0, d);
}

// CellIndexOf must be the exact inverse of CellDate.
TEST(Calendar, CellIndexOfInvertsCellDate) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    for (int i = 0; i < 42; ++i) {
        int y = 0, m = 0, d = 0;
        cal.CellDate(i, y, m, d);
        EXPECT_EQ(i, cal.CellIndexOf(y, m, d));
    }
}

TEST(Calendar, CellIndexOfKnownDates) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    EXPECT_EQ(4, cal.CellIndexOf(2024, 8, 1));
    EXPECT_EQ(18, cal.CellIndexOf(2024, 8, 15));
}

TEST(Calendar, CellIndexOfDateOutsideViewIsMinusOne) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    EXPECT_EQ(-1, cal.CellIndexOf(2024, 12, 1));
    EXPECT_EQ(-1, cal.CellIndexOf(2023, 8, 15));
}

// --- Geometry and hit-testing ------------------------------------------------

TEST(Calendar, Measure) {
    Calendar cal;
    cal.Measure(1000, 1000);
    EXPECT_NEAR(7 * 36.0f, cal.Desired().w, 0.01f);
    EXPECT_NEAR(36.0f + 24.0f + 6 * 32.0f, cal.Desired().h, 0.01f);
}

// The grid starts below the nav bar and the weekday header, and CellRect is in
// absolute DIPs (bounds-relative offsets folded in).
TEST(Calendar, CellRectFirstCell) {
    Calendar cal;
    cal.Arrange(RectDip{0, 0, 252, 276});
    RectDip r = cal.CellRect(0);
    EXPECT_NEAR(0.0f, r.x, 0.01f);
    EXPECT_NEAR(60.0f, r.y, 0.01f);  // nav 36 + header 24
    EXPECT_NEAR(36.0f, r.w, 0.01f);
    EXPECT_NEAR(32.0f, r.h, 0.01f);
}

TEST(Calendar, CellRectIsRowMajor) {
    Calendar cal;
    cal.Arrange(RectDip{0, 0, 252, 276});
    RectDip c8 = cal.CellRect(8);  // row 1, col 1
    EXPECT_NEAR(36.0f, c8.x, 0.01f);
    EXPECT_NEAR(60.0f + 32.0f, c8.y, 0.01f);
}

// CellRect must track the arranged origin, not assume (0,0).
TEST(Calendar, CellRectHonorsArrangedOrigin) {
    Calendar cal;
    cal.Arrange(RectDip{100, 50, 252, 276});
    RectDip r = cal.CellRect(0);
    EXPECT_NEAR(100.0f, r.x, 0.01f);
    EXPECT_NEAR(50.0f + 60.0f, r.y, 0.01f);
}

TEST(Calendar, CellRectOutOfRangeIsEmpty) {
    Calendar cal;
    cal.Arrange(RectDip{0, 0, 252, 276});
    EXPECT_TRUE(cal.CellRect(-1).isEmpty());
    EXPECT_TRUE(cal.CellRect(42).isEmpty());
}

// HitTestCell takes control-relative coordinates.
TEST(Calendar, HitTestCellFindsRowAndColumn) {
    Calendar cal;
    EXPECT_EQ(0, cal.HitTestCell(1.0f, 61.0f));
    EXPECT_EQ(7, cal.HitTestCell(1.0f, 61.0f + 32.0f));
    EXPECT_EQ(8, cal.HitTestCell(37.0f, 61.0f + 32.0f));
    EXPECT_EQ(41, cal.HitTestCell(6 * 36.0f + 1.0f, 60.0f + 5 * 32.0f + 1.0f));
}

// Above the grid (the nav bar and weekday header) is not a cell.
TEST(Calendar, HitTestCellRejectsHeaderArea) {
    Calendar cal;
    EXPECT_EQ(-1, cal.HitTestCell(50.0f, 10.0f));   // nav bar
    EXPECT_EQ(-1, cal.HitTestCell(50.0f, 45.0f));   // weekday header
}

TEST(Calendar, HitTestCellRejectsOutsidePoints) {
    Calendar cal;
    EXPECT_EQ(-1, cal.HitTestCell(-1.0f, 100.0f));         // left of grid
    EXPECT_EQ(-1, cal.HitTestCell(7 * 36.0f, 100.0f));     // right of grid
    EXPECT_EQ(-1, cal.HitTestCell(50.0f, 60.0f + 6 * 32.0f));  // below grid
}

// --- Keyboard ----------------------------------------------------------------

TEST(Calendar, RightArrowAdvancesOneDay) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_RIGHT;
    cal.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(16, cal.SelectedDay());
}

TEST(Calendar, LeftArrowGoesBackOneDay) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_LEFT;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(14, cal.SelectedDay());
}

TEST(Calendar, DownArrowAdvancesOneWeek) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_DOWN;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(22, cal.SelectedDay());
}

TEST(Calendar, UpArrowGoesBackOneWeek) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_UP;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(8, cal.SelectedDay());
}

// Moving off the end of the month must page the view, not just the date.
TEST(Calendar, ArrowPastMonthEndPagesView) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 31);
    KeyEventArgs e{};
    e.vk = VK_RIGHT;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(9, cal.SelectedMonth());
    EXPECT_EQ(1, cal.SelectedDay());
    EXPECT_EQ(9, cal.ViewedMonth());
}

TEST(Calendar, ArrowBeforeMonthStartPagesView) {
    Calendar cal;
    cal.SetSelectedDate(2026, 9, 1);
    KeyEventArgs e{};
    e.vk = VK_LEFT;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(8, cal.SelectedMonth());
    EXPECT_EQ(31, cal.SelectedDay());
    EXPECT_EQ(8, cal.ViewedMonth());
}

TEST(Calendar, WeekJumpCrossesYear) {
    Calendar cal;
    cal.SetSelectedDate(2026, 12, 29);
    KeyEventArgs e{};
    e.vk = VK_DOWN;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(2027, cal.SelectedYear());
    EXPECT_EQ(1, cal.SelectedMonth());
    EXPECT_EQ(5, cal.SelectedDay());
}

TEST(Calendar, WeekJumpCrossesLeapDay) {
    Calendar cal;
    cal.SetSelectedDate(2024, 2, 26);
    KeyEventArgs e{};
    e.vk = VK_DOWN;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(3, cal.SelectedMonth());
    EXPECT_EQ(4, cal.SelectedDay());  // 26 + 7 = 33 -> Mar 4 (Feb has 29)
}

// Arrow keys move the selection without committing it; only Enter commits.
TEST(Calendar, ArrowKeyDoesNotRaiseSelectionChanged) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    int fired = 0;
    auto sub = cal.SelectionChanged().Subscribe(&fired, CountSelectionChanged);
    KeyEventArgs e{};
    e.vk = VK_RIGHT;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(0, fired);
}

TEST(Calendar, EnterRaisesSelectionChanged) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    int fired = 0;
    auto sub = cal.SelectionChanged().Subscribe(&fired, CountSelectionChanged);
    KeyEventArgs e{};
    e.vk = VK_RETURN;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(1, fired);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(15, cal.SelectedDay());  // Enter commits, it does not move
}

TEST(Calendar, HandledKeyIsIgnored) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_RIGHT;
    e.handled = true;
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(15, cal.SelectedDay());
}

TEST(Calendar, UnrelatedKeyLeavesSelectionAloneAndUnhandled) {
    Calendar cal;
    cal.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = 'A';
    cal.OnKeyDownRouted(e);
    EXPECT_EQ(15, cal.SelectedDay());
    EXPECT_FALSE(e.handled);
}

// --- Pointer -----------------------------------------------------------------

TEST(Calendar, ClickOnCellSelectsThatDate) {
    Calendar cal;
    cal.SetSelectedDate(2024, 8, 20);
    cal.Arrange(RectDip{0, 0, 252, 276});
    int fired = 0;
    auto sub = cal.SelectionChanged().Subscribe(&fired, CountSelectionChanged);

    // Cell 4 is Aug 1 2024; click its center.
    RectDip c4 = cal.CellRect(4);
    PointerEventArgs e{};
    e.position = {c4.x + c4.w * 0.5f, c4.y + c4.h * 0.5f};
    cal.OnPointerPressed(e);

    EXPECT_TRUE(e.handled);
    EXPECT_EQ(1, fired);
    EXPECT_EQ(2024, cal.SelectedYear());
    EXPECT_EQ(8, cal.SelectedMonth());
    EXPECT_EQ(1, cal.SelectedDay());
}

// Clicking a trailing spill day selects it and moves the view to its month.
TEST(Calendar, ClickOnSpilloverCellSelectsAndPagesView) {
    Calendar cal;
    cal.SetSelectedDate(2024, 8, 20);
    cal.Arrange(RectDip{0, 0, 252, 276});

    RectDip last = cal.CellRect(41);  // Sep 7 2024
    PointerEventArgs e{};
    e.position = {last.x + last.w * 0.5f, last.y + last.h * 0.5f};
    cal.OnPointerPressed(e);

    EXPECT_EQ(9, cal.SelectedMonth());
    EXPECT_EQ(7, cal.SelectedDay());
    EXPECT_EQ(9, cal.ViewedMonth());
}

TEST(Calendar, ClickOnPreviousArrowPagesBack) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    cal.Arrange(RectDip{0, 0, 252, 276});
    PointerEventArgs e{};
    e.position = {10.0f, 18.0f};  // left 40 DIP of the nav bar
    cal.OnPointerPressed(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(7, cal.ViewedMonth());
}

TEST(Calendar, ClickOnNextArrowPagesForward) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    cal.Arrange(RectDip{0, 0, 252, 276});
    PointerEventArgs e{};
    e.position = {252.0f - 10.0f, 18.0f};  // right 40 DIP of the nav bar
    cal.OnPointerPressed(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(9, cal.ViewedMonth());
}

// The nav bar's middle (the "2026年8月" title) is not a button.
TEST(Calendar, ClickOnNavTitleDoesNothing) {
    Calendar cal;
    cal.SetViewedMonth(2024, 8);
    cal.Arrange(RectDip{0, 0, 252, 276});
    PointerEventArgs e{};
    e.position = {126.0f, 18.0f};
    cal.OnPointerPressed(e);
    EXPECT_FALSE(e.handled);
    EXPECT_EQ(8, cal.ViewedMonth());
}

TEST(Calendar, HandledPointerEventIsIgnored) {
    Calendar cal;
    cal.SetSelectedDate(2024, 8, 20);
    cal.Arrange(RectDip{0, 0, 252, 276});
    RectDip c4 = cal.CellRect(4);
    PointerEventArgs e{};
    e.position = {c4.x + c4.w * 0.5f, c4.y + c4.h * 0.5f};
    e.handled = true;
    cal.OnPointerPressed(e);
    EXPECT_EQ(20, cal.SelectedDay());
}

// --- Hover -------------------------------------------------------------------

// Idle must cost zero: with no hover the fade is settled and no tick is wanted.
TEST(Calendar, NoAnimationTickWhenIdle) {
    Calendar cal;
    EXPECT_FALSE(cal.WantsAnimationTick());
}

// Hovering a cell re-targets the fade, which must request ticks until it settles.
TEST(Calendar, HoverRequestsAnimationTickUntilSettled) {
    Calendar cal;
    cal.Arrange(RectDip{0, 0, 252, 276});
    RectDip c0 = cal.CellRect(0);
    PointerEventArgs e{};
    e.position = {c0.x + c0.w * 0.5f, c0.y + c0.h * 0.5f};
    cal.OnPointerMoved(e);
    EXPECT_TRUE(cal.WantsAnimationTick());

    // Tick generously; the fade must reach its target and stop asking.
    for (int i = 0; i < 200 && cal.WantsAnimationTick(); ++i)
        cal.OnAnimationTick(0.016f);
    EXPECT_FALSE(cal.WantsAnimationTick());
}

// Leaving must fade back out, then settle again.
TEST(Calendar, PointerLeftFadesHoverBackOut) {
    Calendar cal;
    cal.Arrange(RectDip{0, 0, 252, 276});
    RectDip c0 = cal.CellRect(0);
    PointerEventArgs e{};
    e.position = {c0.x + c0.w * 0.5f, c0.y + c0.h * 0.5f};
    cal.OnPointerMoved(e);
    for (int i = 0; i < 200 && cal.WantsAnimationTick(); ++i)
        cal.OnAnimationTick(0.016f);

    cal.OnPointerLeft();
    EXPECT_TRUE(cal.WantsAnimationTick());
    for (int i = 0; i < 200 && cal.WantsAnimationTick(); ++i)
        cal.OnAnimationTick(0.016f);
    EXPECT_FALSE(cal.WantsAnimationTick());
}

// Moving onto the header (not a cell) must clear hover rather than keep the last
// cell lit.
TEST(Calendar, MovingOffGridClearsHover) {
    Calendar cal;
    cal.Arrange(RectDip{0, 0, 252, 276});
    RectDip c0 = cal.CellRect(0);
    PointerEventArgs hover{};
    hover.position = {c0.x + c0.w * 0.5f, c0.y + c0.h * 0.5f};
    cal.OnPointerMoved(hover);
    for (int i = 0; i < 200 && cal.WantsAnimationTick(); ++i)
        cal.OnAnimationTick(0.016f);

    PointerEventArgs onHeader{};
    onHeader.position = {126.0f, 18.0f};
    cal.OnPointerMoved(onHeader);
    // Hover target went back to 0, so the fade has work to do again.
    EXPECT_TRUE(cal.WantsAnimationTick());
}

TEST(Calendar, IsFocusable) {
    Calendar cal;
    EXPECT_TRUE(cal.IsFocusable());
}
