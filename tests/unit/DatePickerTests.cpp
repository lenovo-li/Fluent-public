// DatePickerTests.cpp — headless unit tests for the DatePicker control.
//
// DatePicker's popup path needs a live window (PopupHost creates its own HWND and
// composition target), so these tests cover only what is exercisable headlessly:
// date state, the ±1-day keyboard arithmetic including month/year rollover, the
// DateChanged contract (programmatic sets stay silent), and layout.

#include "../framework/Test.h"
#include "../../FluentUI/controls/DatePicker.h"

using namespace fluent;

// Event<> takes a raw function pointer, not a lambda; the counter travels through
// the void* owner slot so no global state is needed.
namespace {
void CountDateChanged(void* ctx, DatePicker&, RoutedEventArgs&) {
    (*static_cast<int*>(ctx))++;
}
} // namespace

TEST(DatePicker, DefaultsToToday) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    DatePicker dp;
    EXPECT_EQ(static_cast<int>(st.wYear), dp.SelectedYear());
    EXPECT_EQ(static_cast<int>(st.wMonth), dp.SelectedMonth());
    EXPECT_EQ(static_cast<int>(st.wDay), dp.SelectedDay());
}

TEST(DatePicker, SetSelectedDate) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    EXPECT_EQ(2026, dp.SelectedYear());
    EXPECT_EQ(8, dp.SelectedMonth());
    EXPECT_EQ(15, dp.SelectedDay());
}

// A programmatic set is not user input, so it must not raise DateChanged.
TEST(DatePicker, SetSelectedDateDoesNotRaiseDateChanged) {
    DatePicker dp;
    int fired = 0;
    auto sub = dp.DateChanged().Subscribe(&fired, CountDateChanged);
    dp.SetSelectedDate(2026, 8, 15);
    EXPECT_EQ(0, fired);
}

TEST(DatePicker, ArrowUpAddsOneDay) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_UP;
    dp.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(2026, dp.SelectedYear());
    EXPECT_EQ(8, dp.SelectedMonth());
    EXPECT_EQ(16, dp.SelectedDay());
}

TEST(DatePicker, ArrowDownSubtractsOneDay) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_DOWN;
    dp.OnKeyDownRouted(e);
    EXPECT_TRUE(e.handled);
    EXPECT_EQ(14, dp.SelectedDay());
}

// Stepping past the end of a month must carry into the next one.
TEST(DatePicker, ArrowUpRollsIntoNextMonth) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 31);
    KeyEventArgs e{};
    e.vk = VK_UP;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(2026, dp.SelectedYear());
    EXPECT_EQ(9, dp.SelectedMonth());
    EXPECT_EQ(1, dp.SelectedDay());
}

TEST(DatePicker, ArrowDownRollsIntoPreviousMonth) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 9, 1);
    KeyEventArgs e{};
    e.vk = VK_DOWN;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(2026, dp.SelectedYear());
    EXPECT_EQ(8, dp.SelectedMonth());
    EXPECT_EQ(31, dp.SelectedDay());
}

TEST(DatePicker, ArrowUpRollsIntoNextYear) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 12, 31);
    KeyEventArgs e{};
    e.vk = VK_UP;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(2027, dp.SelectedYear());
    EXPECT_EQ(1, dp.SelectedMonth());
    EXPECT_EQ(1, dp.SelectedDay());
}

TEST(DatePicker, ArrowDownRollsIntoPreviousYear) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 1, 1);
    KeyEventArgs e{};
    e.vk = VK_DOWN;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(2025, dp.SelectedYear());
    EXPECT_EQ(12, dp.SelectedMonth());
    EXPECT_EQ(31, dp.SelectedDay());
}

// Feb 28 -> 29 must only exist in a leap year.
TEST(DatePicker, ArrowUpCrossesLeapDay) {
    DatePicker dp;
    dp.SetSelectedDate(2024, 2, 28);
    KeyEventArgs e{};
    e.vk = VK_UP;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(2, dp.SelectedMonth());
    EXPECT_EQ(29, dp.SelectedDay());
}

TEST(DatePicker, ArrowUpSkipsLeapDayInCommonYear) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 2, 28);
    KeyEventArgs e{};
    e.vk = VK_UP;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(3, dp.SelectedMonth());
    EXPECT_EQ(1, dp.SelectedDay());
}

// The arrow keys ARE user input, so they must raise DateChanged.
TEST(DatePicker, ArrowKeyRaisesDateChanged) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    int fired = 0;
    auto sub = dp.DateChanged().Subscribe(&fired, CountDateChanged);
    KeyEventArgs e{};
    e.vk = VK_UP;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(1, fired);
}

// An already-handled event must be left alone.
TEST(DatePicker, HandledKeyIsIgnored) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_UP;
    e.handled = true;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(15, dp.SelectedDay());
}

// An unrelated key must not change the date and must stay unhandled so it can
// keep bubbling.
TEST(DatePicker, UnrelatedKeyLeavesDateAlone) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = 'A';
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(15, dp.SelectedDay());
    EXPECT_FALSE(e.handled);
}

TEST(DatePicker, Measure) {
    DatePicker dp;
    dp.Measure(1000, 1000);
    EXPECT_NEAR(120.0f, dp.Desired().w, 0.01f);
    EXPECT_NEAR(32.0f, dp.Desired().h, 0.01f);
}

TEST(DatePicker, ArrangeSetsBounds) {
    DatePicker dp;
    dp.Arrange(RectDip{10, 20, 120, 32});
    EXPECT_NEAR(10.0f, dp.Bounds().x, 0.01f);
    EXPECT_NEAR(20.0f, dp.Bounds().y, 0.01f);
    EXPECT_NEAR(120.0f, dp.Bounds().w, 0.01f);
    EXPECT_NEAR(32.0f, dp.Bounds().h, 0.01f);
}

// ButtonBase opts into focus and the click gesture in its constructor.
TEST(DatePicker, IsFocusable) {
    DatePicker dp;
    EXPECT_TRUE(dp.IsFocusable());
}

// Opening the calendar needs a live window; headless it must fail quietly rather
// than crash. Activation with no context is the path a detached control takes.
TEST(DatePicker, ActivateWithoutWindowIsSafe) {
    DatePicker dp;
    dp.SetSelectedDate(2026, 8, 15);
    KeyEventArgs e{};
    e.vk = VK_SPACE;
    dp.OnKeyDownRouted(e);
    EXPECT_EQ(15, dp.SelectedDay());
}
