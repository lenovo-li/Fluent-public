// ButtonBaseTests.cpp — unit tests for ButtonBase / ContentControl / ToggleButton
// state machines (WP-06 Stage 1).
//
// All tests are headless: no GPU, no DWrite, no PopupHost. The activation /
// toggle / state-machine paths are pure logic exercisable without a live window.

#include "../framework/Test.h"
#include "../../FluentUI/controls/primitives/ButtonBase.h"
#include "../../FluentUI/controls/primitives/ToggleButton.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/controls/ToggleSwitch.h"
#include "../../FluentUI/controls/RadioButton.h"

using namespace fluent;

// ---------------------------------------------------------------------------
// Static handlers (Event<> requires raw function pointers, not lambdas).
// Counters are passed via void* owner so no global state is needed.
// ---------------------------------------------------------------------------
namespace {
void CountClicks(void* ctx, Button&, RoutedEventArgs&) { (*static_cast<int*>(ctx))++; }
void CountChecks(void* ctx, CheckBox&, bool&)          { (*static_cast<int*>(ctx))++; }
void CountToggled(void* ctx, ToggleSwitch&, bool&)     { (*static_cast<int*>(ctx))++; }
void CountSelected(void* ctx, RadioButton&, int& v) {
    auto* p = static_cast<int*>(ctx);
    p[0]++;       // p[0] = fired count
    p[1] = v;     // p[1] = last value
}
} // namespace

// ---------------------------------------------------------------------------
// ButtonBase / ContentControl basics
// ---------------------------------------------------------------------------

// A freshly built button is focusable (ButtonBase ctor calls SetFocusable(true)).
TEST(ButtonBase, FocusableByDefault) {
    Button b;
    EXPECT_TRUE(b.IsFocusable());
}

// SetText dirties Measure (ContentControl::SetText uses DirtyFlags::Measure).
TEST(ButtonBase, SetTextDirtiesMeasure) {
    Button b;
    b.SetText(L"Hello");
    EXPECT_TRUE(b.NeedsRemeasure());
}

// Pressing Space fires Click via OnKeyDownRouted → ButtonBase::OnActivate.
TEST(ButtonBase, SpaceKeyFiresClick) {
    Button b;
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);
    KeyEventArgs e;
    e.vk = VK_SPACE;
    b.OnKeyDownRouted(e);
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(e.handled);
}

// Pressing Enter fires Click and marks the event handled.
TEST(ButtonBase, EnterKeyFiresClick) {
    Button b;
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);
    KeyEventArgs e;
    e.vk = VK_RETURN;
    b.OnKeyDownRouted(e);
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(e.handled);
}

// Other keys do not fire Click and do not mark handled.
TEST(ButtonBase, OtherKeyIgnored) {
    Button b;
    int fired = 0;
    auto sub = b.Click().Subscribe(&fired, CountClicks);
    KeyEventArgs e;
    e.vk = VK_TAB;
    b.OnKeyDownRouted(e);
    EXPECT_EQ(fired, 0);
    EXPECT_FALSE(e.handled);
}

// ---------------------------------------------------------------------------
// ToggleButton: CheckBox
// ---------------------------------------------------------------------------

// Default checked state is false.
TEST(ToggleButton, CheckBoxDefaultUnchecked) {
    CheckBox cb;
    EXPECT_FALSE(cb.IsChecked());
}

// SetChecked(true) silently changes state without firing the event.
TEST(ToggleButton, SetCheckedIsSilent) {
    CheckBox cb;
    int fired = 0;
    auto sub = cb.Checked().Subscribe(&fired, CountChecks);
    cb.SetChecked(true);
    EXPECT_TRUE(cb.IsChecked());
    EXPECT_EQ(fired, 0);  // SetChecked never fires the event
}

// SetChecked with the same value is a no-op (state unchanged, no event).
TEST(ToggleButton, SetCheckedNoOpWhenUnchanged) {
    CheckBox cb;
    int fired = 0;
    auto sub = cb.Checked().Subscribe(&fired, CountChecks);
    cb.SetChecked(false);  // already false
    EXPECT_FALSE(cb.IsChecked());
    EXPECT_EQ(fired, 0);
}

// Space key toggles and fires the Checked event once.
TEST(ToggleButton, SpaceTogglesFiringEvent) {
    CheckBox cb;
    int fired = 0;
    auto sub = cb.Checked().Subscribe(&fired, CountChecks);
    KeyEventArgs e;
    e.vk = VK_SPACE;
    cb.OnKeyDownRouted(e);
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(cb.IsChecked());
}

// Two activations restore the original state.
TEST(ToggleButton, DoubleToggleRestores) {
    CheckBox cb;
    KeyEventArgs e; e.vk = VK_SPACE;
    cb.OnKeyDownRouted(e);
    cb.OnKeyDownRouted(e);
    EXPECT_FALSE(cb.IsChecked());
}

// ---------------------------------------------------------------------------
// ToggleButton: ToggleSwitch (thin IsOn/SetOn aliases)
// ---------------------------------------------------------------------------

TEST(ToggleButton, ToggleSwitchIsOnAliasConsistent) {
    ToggleSwitch ts;
    EXPECT_FALSE(ts.IsOn());
    ts.SetOn(true);
    EXPECT_TRUE(ts.IsOn());
    EXPECT_TRUE(ts.IsChecked());  // alias and base agree
}

TEST(ToggleButton, ToggleSwitchFiresToggled) {
    ToggleSwitch ts;
    int fired = 0;
    auto sub = ts.Toggled().Subscribe(&fired, CountToggled);
    KeyEventArgs e; e.vk = VK_SPACE;
    ts.OnKeyDownRouted(e);
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(ts.IsOn());
}

// ---------------------------------------------------------------------------
// RadioButton (ButtonBase subclass, NOT ToggleButton — group-select semantics)
// ---------------------------------------------------------------------------

// Activating a RadioButton fires the Selected event with this button's value.
TEST(ButtonBase, RadioButtonSelectFiresEvent) {
    int group = -1;
    RadioButton rb;
    rb.SetGroup(&group, 42);
    int state[2] = { 0, -1 };  // [0]=fired count, [1]=last value
    auto sub = rb.Selected().Subscribe(state, CountSelected);
    KeyEventArgs e; e.vk = VK_SPACE;
    rb.OnKeyDownRouted(e);
    EXPECT_EQ(state[0], 1);
    EXPECT_EQ(state[1], 42);
    EXPECT_EQ(group, 42);
    EXPECT_TRUE(rb.IsSelected());
}

// A press on the already-selected button is a no-op: no event, no re-fire.
TEST(ButtonBase, RadioButtonReSelectNoOp) {
    int group = 42;
    RadioButton rb;
    rb.SetGroup(&group, 42);
    int state[2] = { 0, -1 };
    auto sub = rb.Selected().Subscribe(state, CountSelected);
    KeyEventArgs e; e.vk = VK_SPACE;
    rb.OnKeyDownRouted(e);
    EXPECT_EQ(state[0], 0);   // already selected, Select() is a no-op
}

// Two RadioButtons in the same group are mutually exclusive.
TEST(ButtonBase, RadioButtonGroupExclusive) {
    int group = -1;
    RadioButton r1, r2;
    r1.SetGroup(&group, 1);
    r2.SetGroup(&group, 2);

    KeyEventArgs e; e.vk = VK_SPACE;
    r1.OnKeyDownRouted(e);
    EXPECT_TRUE(r1.IsSelected());
    EXPECT_FALSE(r2.IsSelected());

    r2.OnKeyDownRouted(e);
    EXPECT_FALSE(r1.IsSelected());
    EXPECT_TRUE(r2.IsSelected());
}
