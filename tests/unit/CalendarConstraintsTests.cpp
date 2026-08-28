// CalendarConstraintsTests.cpp — P1-12/13: date constraints (min/max + blackout).

#include "../framework/Test.h"
#include "../../FluentUI/controls/Calendar.h"
#include <tuple>
#include <vector>

using namespace fluent;

// --- P1-12: Min/Max date bounds -----------------------------------------------

TEST(CalendarConstraints, SetMinDate_BlocksEarlierDates) {
    Calendar cal;
    cal.SetMinDate(2020, 5, 10);

    EXPECT_FALSE(cal.IsDateEnabled(2020, 5, 9));   // day before
    EXPECT_TRUE(cal.IsDateEnabled(2020, 5, 10));   // min itself
    EXPECT_TRUE(cal.IsDateEnabled(2020, 5, 11));   // day after
    EXPECT_FALSE(cal.IsDateEnabled(2020, 4, 10));  // month before
    EXPECT_FALSE(cal.IsDateEnabled(2019, 5, 10));  // year before
}

TEST(CalendarConstraints, SetMaxDate_BlocksLaterDates) {
    Calendar cal;
    cal.SetMaxDate(2025, 8, 20);

    EXPECT_TRUE(cal.IsDateEnabled(2025, 8, 19));   // day before
    EXPECT_TRUE(cal.IsDateEnabled(2025, 8, 20));   // max itself
    EXPECT_FALSE(cal.IsDateEnabled(2025, 8, 21));  // day after
    EXPECT_FALSE(cal.IsDateEnabled(2025, 9, 1));   // month after
    EXPECT_FALSE(cal.IsDateEnabled(2026, 1, 1));   // year after
}

TEST(CalendarConstraints, MinAndMaxTogether_OnlyAllowsRange) {
    Calendar cal;
    cal.SetMinDate(2023, 6, 1);
    cal.SetMaxDate(2023, 6, 30);

    EXPECT_FALSE(cal.IsDateEnabled(2023, 5, 31));  // before range
    EXPECT_TRUE(cal.IsDateEnabled(2023, 6, 1));    // first day
    EXPECT_TRUE(cal.IsDateEnabled(2023, 6, 15));   // middle
    EXPECT_TRUE(cal.IsDateEnabled(2023, 6, 30));   // last day
    EXPECT_FALSE(cal.IsDateEnabled(2023, 7, 1));   // after range
}

TEST(CalendarConstraints, ClearMinDate_RemovesLowerBound) {
    Calendar cal;
    cal.SetMinDate(2022, 3, 10);
    EXPECT_FALSE(cal.IsDateEnabled(2022, 3, 9));

    cal.ClearMinDate();
    EXPECT_TRUE(cal.IsDateEnabled(2022, 3, 9));    // now enabled
    EXPECT_TRUE(cal.IsDateEnabled(1900, 1, 1));    // far past enabled
}

TEST(CalendarConstraints, ClearMaxDate_RemovesUpperBound) {
    Calendar cal;
    cal.SetMaxDate(2024, 12, 15);
    EXPECT_FALSE(cal.IsDateEnabled(2024, 12, 16));

    cal.ClearMaxDate();
    EXPECT_TRUE(cal.IsDateEnabled(2024, 12, 16));  // now enabled
    EXPECT_TRUE(cal.IsDateEnabled(2099, 12, 31));  // far future enabled
}

TEST(CalendarConstraints, GetMinDate_ReturnsSetValue) {
    Calendar cal;
    cal.SetMinDate(2021, 7, 14);

    int y, m, d;
    cal.GetMinDate(y, m, d);
    EXPECT_EQ(2021, y);
    EXPECT_EQ(7, m);
    EXPECT_EQ(14, d);
}

TEST(CalendarConstraints, GetMaxDate_ReturnsSetValue) {
    Calendar cal;
    cal.SetMaxDate(2026, 11, 8);

    int y, m, d;
    cal.GetMaxDate(y, m, d);
    EXPECT_EQ(2026, y);
    EXPECT_EQ(11, m);
    EXPECT_EQ(8, d);
}

// --- P1-13: Blackout dates ----------------------------------------------------

TEST(CalendarConstraints, SetBlackoutDates_BlocksSpecificDates) {
    Calendar cal;
    std::vector<std::tuple<int,int,int>> blackout = {
        {2023, 1, 15},
        {2023, 1, 20},
        {2023, 2, 10}
    };
    cal.SetBlackoutDates(blackout);

    EXPECT_FALSE(cal.IsDateEnabled(2023, 1, 15));
    EXPECT_FALSE(cal.IsDateEnabled(2023, 1, 20));
    EXPECT_FALSE(cal.IsDateEnabled(2023, 2, 10));
    EXPECT_TRUE(cal.IsDateEnabled(2023, 1, 14));   // adjacent OK
    EXPECT_TRUE(cal.IsDateEnabled(2023, 1, 21));   // adjacent OK
}

TEST(CalendarConstraints, ClearBlackoutDates_RemovesAllBlackouts) {
    Calendar cal;
    std::vector<std::tuple<int,int,int>> blackout = {{2023, 5, 5}, {2023, 5, 6}};
    cal.SetBlackoutDates(blackout);
    EXPECT_FALSE(cal.IsDateEnabled(2023, 5, 5));

    cal.ClearBlackoutDates();
    EXPECT_TRUE(cal.IsDateEnabled(2023, 5, 5));
    EXPECT_TRUE(cal.IsDateEnabled(2023, 5, 6));
}

TEST(CalendarConstraints, IsDateBlackedOut_FindsExactMatch) {
    Calendar cal;
    std::vector<std::tuple<int,int,int>> blackout = {{2024, 3, 17}};
    cal.SetBlackoutDates(blackout);

    EXPECT_TRUE(cal.IsDateBlackedOut(2024, 3, 17));
    EXPECT_FALSE(cal.IsDateBlackedOut(2024, 3, 16));
    EXPECT_FALSE(cal.IsDateBlackedOut(2024, 3, 18));
}

// --- Combined: min/max + blackout ---------------------------------------------

TEST(CalendarConstraints, BlackoutOverridesMinMax_WhenInsideRange) {
    Calendar cal;
    cal.SetMinDate(2023, 1, 1);
    cal.SetMaxDate(2023, 12, 31);
    std::vector<std::tuple<int,int,int>> blackout = {{2023, 6, 15}};
    cal.SetBlackoutDates(blackout);

    EXPECT_TRUE(cal.IsDateEnabled(2023, 6, 14));   // in range, not blacked
    EXPECT_FALSE(cal.IsDateEnabled(2023, 6, 15));  // in range, but blacked
    EXPECT_TRUE(cal.IsDateEnabled(2023, 6, 16));   // in range, not blacked
}

TEST(CalendarConstraints, MinMaxBlocksEvenWithoutBlackout) {
    Calendar cal;
    cal.SetMinDate(2022, 5, 1);
    cal.SetMaxDate(2022, 5, 31);
    // No blackout dates set.

    EXPECT_FALSE(cal.IsDateEnabled(2022, 4, 30));  // before min
    EXPECT_FALSE(cal.IsDateEnabled(2022, 6, 1));   // after max
    EXPECT_TRUE(cal.IsDateEnabled(2022, 5, 15));   // inside range
}

// --- SetSelectedDate rejection ------------------------------------------------

TEST(CalendarConstraints, SetSelectedDate_RejectsDisabledDate_MinBound) {
    Calendar cal;
    cal.SetMinDate(2023, 3, 10);
    cal.SetSelectedDate(2023, 3, 15);  // valid, sets it
    int y, m, d;
    cal.GetSelectedDate(y, m, d);
    EXPECT_EQ(2023, y);
    EXPECT_EQ(3, m);
    EXPECT_EQ(15, d);

    cal.SetSelectedDate(2023, 3, 9);   // before min, should be rejected
    cal.GetSelectedDate(y, m, d);
    EXPECT_EQ(2023, y);                // still old value
    EXPECT_EQ(3, m);
    EXPECT_EQ(15, d);
}

TEST(CalendarConstraints, SetSelectedDate_RejectsDisabledDate_MaxBound) {
    Calendar cal;
    cal.SetMaxDate(2024, 7, 20);
    cal.SetSelectedDate(2024, 7, 10);
    int y, m, d;
    cal.GetSelectedDate(y, m, d);
    EXPECT_EQ(10, d);

    cal.SetSelectedDate(2024, 7, 21);  // after max, rejected
    cal.GetSelectedDate(y, m, d);
    EXPECT_EQ(10, d);                  // unchanged
}

TEST(CalendarConstraints, SetSelectedDate_RejectsBlackedOutDate) {
    Calendar cal;
    std::vector<std::tuple<int,int,int>> blackout = {{2025, 2, 14}};
    cal.SetBlackoutDates(blackout);
    cal.SetSelectedDate(2025, 2, 13);
    int y, m, d;
    cal.GetSelectedDate(y, m, d);
    EXPECT_EQ(13, d);

    cal.SetSelectedDate(2025, 2, 14);  // blacked out, rejected
    cal.GetSelectedDate(y, m, d);
    EXPECT_EQ(13, d);                  // unchanged
}

