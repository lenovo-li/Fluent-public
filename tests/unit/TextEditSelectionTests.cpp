// TextEditSelectionTests.cpp — P1-3: programmatic selection API

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBox.h"

using namespace fluent;

TEST(TextEditSelection, SelectSetsRangeAndReportsBack) {
    TextBox tb;
    tb.SetText(L"abcdefgh");
    tb.Select(2, 3);  // select "cde"

    EXPECT_EQ(tb.SelectionStart(), 2u);
    EXPECT_EQ(tb.SelectionLength(), 3u);
}

TEST(TextEditSelection, SelectClampsToTextLength) {
    TextBox tb;
    tb.SetText(L"abc");
    tb.Select(10, 5);  // start beyond end

    // Clamped to (3, 0) — no selection, caret at end
    EXPECT_EQ(tb.SelectionStart(), 3u);
    EXPECT_EQ(tb.SelectionLength(), 0u);
}

TEST(TextEditSelection, SelectClampsLengthToAvailableSpace) {
    TextBox tb;
    tb.SetText(L"abcdefgh");
    tb.Select(5, 100);  // start=5, length way beyond end

    // Clamped to (5, 3) — selects "fgh"
    EXPECT_EQ(tb.SelectionStart(), 5u);
    EXPECT_EQ(tb.SelectionLength(), 3u);
}

TEST(TextEditSelection, SelectZeroLengthCollapsesSelection) {
    TextBox tb;
    tb.SetText(L"abcdefgh");
    tb.Select(2, 3);
    EXPECT_EQ(tb.SelectionLength(), 3u);

    tb.Select(4, 0);  // collapse at position 4
    EXPECT_EQ(tb.SelectionStart(), 4u);
    EXPECT_EQ(tb.SelectionLength(), 0u);
}

TEST(TextEditSelection, SelectionStartReturnsMinOfCaretAndAnchor) {
    TextBox tb;
    tb.SetText(L"abcdefgh");

    // Forward selection: anchor < caret
    tb.Select(2, 3);
    EXPECT_EQ(tb.SelectionStart(), 2u);

    // After typing, caret may move — SelectionStart always returns the min
}

TEST(TextEditSelection, SelectionLengthIsAlwaysNonNegative) {
    TextBox tb;
    tb.SetText(L"abcdefgh");
    tb.Select(5, 2);
    EXPECT_EQ(tb.SelectionLength(), 2u);

    tb.Select(3, 0);
    EXPECT_EQ(tb.SelectionLength(), 0u);
}

// --- SelectionChanged event ------------------------------------------------
// Not a listed P1 item, but the natural complement to Select(): an app that sets the
// selection programmatically also wants to observe the user changing it (an enabled
// state driven by "is anything selected", a character-position readout).

namespace {
struct SelectionCapture {
    int fireCount = 0;
    UINT32 start = 9999;
    UINT32 length = 9999;
};

void CaptureSelection(void* ctx, TextEditBase&,
                      TextEditBase::SelectionChangedArgs& args) {
    auto* c = static_cast<SelectionCapture*>(ctx);
    c->fireCount++;
    c->start = args.start;
    c->length = args.length;
}
}  // namespace

TEST(TextEditSelection, SelectionChangedFiresOnSelect) {
    TextBox tb;
    tb.SetText(L"hello world");
    SelectionCapture cap;
    auto sub = tb.SelectionChanged().Subscribe(&cap, CaptureSelection);

    tb.Select(2, 5);
    EXPECT_EQ(cap.fireCount, 1);
    EXPECT_EQ(cap.start, 2u);
    EXPECT_EQ(cap.length, 5u);
}

TEST(TextEditSelection, SelectionChangedReportsCollapsedSelection) {
    TextBox tb;
    tb.SetText(L"abcdef");
    SelectionCapture cap;
    auto sub = tb.SelectionChanged().Subscribe(&cap, CaptureSelection);

    // A zero-length Select still fires: "the caret moved" is information a readout
    // needs, and suppressing it would make the event unusable for that.
    tb.Select(3, 0);
    EXPECT_EQ(cap.fireCount, 1);
    EXPECT_EQ(cap.start, 3u);
    EXPECT_EQ(cap.length, 0u);
}
