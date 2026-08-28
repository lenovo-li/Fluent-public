// HyperlinkTests.cpp — headless tests for Hyperlink activation routing.
//
// The interesting decision in this control is which of two things an activation
// does: raise Click, or hand the URI to the shell. ShellExecute cannot run in a
// test, so the control exposes LastActivationOpenedUri() — without that
// observation point, a test could only see "Click did not fire" and could not
// tell a shell hand-off from a control that silently did nothing.
//
// Modifiers are read from the ROUTED ARGS rather than GetKeyState for the same
// reason: a test can set args.modifiers, and cannot set the real keyboard state.

#include "../framework/Test.h"
#include "../../FluentUI/controls/Hyperlink.h"

using namespace fluent;

namespace {

void CountClick(void* owner, Hyperlink&, RoutedEventArgs&) {
    ++*static_cast<int*>(owner);
}

// A completed pointer click with the given modifiers.
PointerEventArgs ClickArgs(ModifierKeys mods = ModifierKeys::None) {
    PointerEventArgs e;
    e.button = PointerButton::Left;
    e.modifiers = mods;
    return e;
}

KeyEventArgs KeyArgs(unsigned vk, ModifierKeys mods = ModifierKeys::None) {
    KeyEventArgs e;
    e.vk = vk;
    e.modifiers = mods;
    return e;
}

}  // namespace

// --- Defaults --------------------------------------------------------------

TEST(Hyperlink, DefaultsAreEmptyAndFocusable) {
    Hyperlink link;
    EXPECT_TRUE(link.Text().empty());
    EXPECT_TRUE(link.Uri().empty());
    EXPECT_TRUE(link.IsFocusable());
    EXPECT_FALSE(link.LastActivationOpenedUri());
}

TEST(Hyperlink, SetTextIsStored) {
    Hyperlink link;
    link.SetText(L"Open docs");
    EXPECT_TRUE(link.Text() == L"Open docs");
}

TEST(Hyperlink, SetUriIsStored) {
    Hyperlink link;
    link.SetUri(L"https://example.com");
    EXPECT_TRUE(link.Uri() == L"https://example.com");
}

TEST(Hyperlink, SetFontSizeIsStored) {
    Hyperlink link;
    link.SetFontSize(20.0f);
    EXPECT_TRUE(link.FontSize().has_value());
    EXPECT_NEAR(link.FontSize().value(), 20.0f, 0.01);
}

// --- Click activation ------------------------------------------------------

TEST(Hyperlink, PlainClickRaisesClickEvent) {
    Hyperlink link;
    link.SetText(L"Link");
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    PointerEventArgs e = ClickArgs();
    link.OnClickRouted(e);

    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(e.handled);
    EXPECT_FALSE(link.LastActivationOpenedUri());
}

TEST(Hyperlink, PlainClickRaisesClickEvenWhenUriIsSet) {
    // A URI alone must not divert a plain click — only Ctrl does.
    Hyperlink link;
    link.SetUri(L"https://example.com");
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    PointerEventArgs e = ClickArgs();
    link.OnClickRouted(e);

    EXPECT_EQ(fired, 1);
    EXPECT_FALSE(link.LastActivationOpenedUri());
}

TEST(Hyperlink, CtrlClickWithoutUriStillRaisesClick) {
    // Ctrl with no URI has nothing to hand off, so it behaves as a plain click
    // rather than swallowing the activation.
    Hyperlink link;
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    PointerEventArgs e = ClickArgs(ModifierKeys::Ctrl);
    link.OnClickRouted(e);

    EXPECT_EQ(fired, 1);
    EXPECT_FALSE(link.LastActivationOpenedUri());
}

TEST(Hyperlink, CtrlClickWithUriGoesToTheShellAndNotToClick) {
    // The branch that matters: Click must NOT fire, and the URI path must be the
    // one taken. Both halves are asserted — a control that did nothing at all
    // would also leave `fired` at 0.
    Hyperlink link;
    link.SetUri(L"https://example.com");
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    PointerEventArgs e = ClickArgs(ModifierKeys::Ctrl);
    link.OnClickRouted(e);

    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(link.LastActivationOpenedUri());
    EXPECT_TRUE(e.handled);
}

TEST(Hyperlink, ShiftClickIsNotTreatedAsCtrl) {
    // Only Ctrl diverts; another modifier must not.
    Hyperlink link;
    link.SetUri(L"https://example.com");
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    PointerEventArgs e = ClickArgs(ModifierKeys::Shift);
    link.OnClickRouted(e);

    EXPECT_EQ(fired, 1);
    EXPECT_FALSE(link.LastActivationOpenedUri());
}

// --- Keyboard activation ---------------------------------------------------

TEST(Hyperlink, SpaceActivates) {
    Hyperlink link;
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    KeyEventArgs e = KeyArgs(VK_SPACE);
    link.OnKeyDownRouted(e);

    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(e.handled);
}

TEST(Hyperlink, EnterActivates) {
    Hyperlink link;
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    KeyEventArgs e = KeyArgs(VK_RETURN);
    link.OnKeyDownRouted(e);

    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(e.handled);
}

TEST(Hyperlink, UnrelatedKeyDoesNotActivateOrConsume) {
    Hyperlink link;
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    KeyEventArgs e = KeyArgs('A');
    link.OnKeyDownRouted(e);

    EXPECT_EQ(fired, 0);
    EXPECT_FALSE(e.handled);
}

TEST(Hyperlink, CtrlEnterWithUriGoesToTheShell) {
    // Keyboard activation honours the same Ctrl rule as the pointer path.
    Hyperlink link;
    link.SetUri(L"https://example.com");
    int fired = 0;
    auto sub = link.Click().Subscribe(&fired, CountClick);

    KeyEventArgs e = KeyArgs(VK_RETURN, ModifierKeys::Ctrl);
    link.OnKeyDownRouted(e);

    EXPECT_EQ(fired, 0);
    EXPECT_TRUE(link.LastActivationOpenedUri());
}

// --- Measure / render safety ----------------------------------------------

TEST(Hyperlink, MeasureWithoutDeviceHonorsExplicitSize) {
    // Headless: no DWrite, so no layout. An explicit size must still lay out.
    Hyperlink link;
    link.SetText(L"Some link text");
    link.SetWidth(120.0f);
    link.SetHeight(20.0f);
    link.Measure(500.0f, 500.0f);
    EXPECT_NEAR(link.Desired().w, 120.0f, 0.01);
    EXPECT_NEAR(link.Desired().h, 20.0f, 0.01);
}

TEST(Hyperlink, MeasureWithEmptyTextIsZeroWithoutExplicitSize) {
    Hyperlink link;
    link.Measure(500.0f, 500.0f);
    EXPECT_NEAR(link.Desired().w, 0.0f, 0.01);
}

TEST(Hyperlink, RenderWithoutLayoutIsSafe) {
    // A null device context would crash if Render reached DrawTextLayout; the
    // early return on a null layout is what makes this safe.
    Hyperlink link;
    link.SetText(L"Link");
    link.Arrange(RectDip{0, 0, 100, 20});
    DrawingContext dc{nullptr, nullptr, 1.0f};
    link.Render(dc);  // must not crash
}

TEST(Hyperlink, CursorIsAHand) {
    Hyperlink link;
    EXPECT_TRUE(link.Cursor() != nullptr);
}
