// MessageDialogTests.cpp — headless checks for the one-line message box.
//
// The whole class is construction plus a fixed button-to-result mapping, and both
// are reachable without a host: MessageDialog's constructor runs BuildContent
// directly. The static Show() is not covered — it needs a real owner window.
#include "../framework/Test.h"
#include "../../FluentUI/controls/MessageDialog.h"
#include <string>

using namespace fluent;

TEST(MessageDialog, OkHasOneAffirmativeButton) {
    MessageDialog d(L"Saved", L"Your changes were written.", DialogButtons::Ok);
    EXPECT_EQ(d.Title(), std::wstring(L"Saved"));
    EXPECT_EQ(d.ButtonCount(), static_cast<size_t>(1));
}

TEST(MessageDialog, OkCancelAndYesNoBothProduceTwoButtons) {
    MessageDialog okCancel(L"Confirm", L"Proceed?", DialogButtons::OkCancel);
    EXPECT_EQ(okCancel.ButtonCount(), static_cast<size_t>(2));

    MessageDialog yesNo(L"Delete", L"This cannot be undone.", DialogButtons::YesNo);
    EXPECT_EQ(yesNo.ButtonCount(), static_cast<size_t>(2));
}

TEST(MessageDialog, DefaultsToOkWhenButtonsOmitted) {
    MessageDialog d(L"Note", L"Just so you know.");
    EXPECT_EQ(d.ButtonCount(), static_cast<size_t>(1));
}

TEST(MessageDialog, IsSmallerThanTheGenericDialogDefault) {
    MessageDialog d(L"Note", L"One paragraph.");
    // A message box is one paragraph; inheriting DialogWindow's generic 420x260
    // leaves a visibly empty band under the text. MessageDialog uses a tighter
    // title bar (32 DIP vs 48) and allocates enough client area for the fixed
    // layout: title(30) + content(120 for single-line) + spacing(24) + buttons(36).
    EXPECT_NEAR(d.ClientWidth(), 360.0f, 0.001f);
    EXPECT_NEAR(d.ClientHeight(), 210.0f, 0.001f);
}

TEST(MessageDialog, LongMessageGetsRoomForWrappedTextAndActions) {
    MessageDialog d(L"Saved",
                    L"Output directory:\nC:\\Users\\Example\\A-very-long-output-directory-name");
    // Multi-line messages get a taller content slot (180 DIP vs 120) to accommodate
    // wrapped text without clipping the button row.
    EXPECT_NEAR(d.ClientWidth(), 360.0f, 0.001f);
    EXPECT_NEAR(d.ClientHeight(), 270.0f, 0.001f);
}

TEST(MessageDialog, StartsWithNoResolvedResult) {
    MessageDialog d(L"Confirm", L"Proceed?", DialogButtons::YesNo);
    EXPECT_EQ(d.Result(), DialogResult::None);
    EXPECT_FALSE(d.IsDialogOpen());
}
