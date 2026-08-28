// DialogApiTests.cpp — headless checks for the public dialog composition API.
// Native HWND creation and real rendering remain demo/hardware concerns; these
// tests verify that application code can configure a dialog without a host.
#include "../framework/Test.h"
#include "../../FluentUI/controls/ContentDialog.h"

using namespace fluent;

TEST(DialogApi, ContentDialogDefaultsAreSmallAndConfigurable) {
    ContentDialog dialog;
    EXPECT_EQ(dialog.Title(), std::wstring(L"Dialog"));
    EXPECT_EQ(dialog.ButtonCount(), static_cast<size_t>(0));
    EXPECT_NEAR(dialog.ClientWidth(), 420.0f, 0.001f);
    EXPECT_NEAR(dialog.ClientHeight(), 260.0f, 0.001f);

    dialog.SetTitle(L"Confirm");
    dialog.SetClientSize(480.0f, 300.0f);
    dialog.AddButton(L"OK", DialogResult::Primary);
    dialog.AddButton(L"Cancel", DialogResult::Cancel);

    EXPECT_EQ(dialog.Title(), std::wstring(L"Confirm"));
    EXPECT_EQ(dialog.ButtonCount(), static_cast<size_t>(2));
    EXPECT_NEAR(dialog.ClientWidth(), 480.0f, 0.001f);
    EXPECT_NEAR(dialog.ClientHeight(), 300.0f, 0.001f);
}

TEST(DialogApi, DialogResultHasExplicitUnresolvedState) {
    ContentDialog dialog;
    EXPECT_EQ(dialog.Result(), DialogResult::None);
    EXPECT_FALSE(dialog.IsDialogOpen());
}

TEST(DialogApi, LifecycleArgumentsSupportCancelableClosing) {
    DialogClosingArgs closing{DialogResult::Primary, false};
    EXPECT_EQ(closing.result, DialogResult::Primary);
    EXPECT_FALSE(closing.cancel);
    closing.cancel = true;
    EXPECT_TRUE(closing.cancel);

    DialogClosedArgs closed{DialogResult::Secondary};
    EXPECT_EQ(closed.result, DialogResult::Secondary);
}
