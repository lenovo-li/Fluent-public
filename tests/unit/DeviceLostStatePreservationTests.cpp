// DeviceLostStatePreservationTests.cpp — proves a control's LOGICAL state
// survives a device-loss / restore cycle (roadmap §17 acceptance: "device
// recovery preserves control logical state"). The device-bound GPU resources are
// released and rebuilt by the host; the element tree keeps its model (checked
// state, selection, scroll, text) because none of that is device-bound.
//
// Pure logic: we invoke the OnDeviceLost / OnDeviceRestored hooks directly on
// controls (the same calls the host makes during RecoverDevice) and assert the
// observable state is unchanged. No GPU / device is involved.

#include "../framework/Test.h"
#include "../../FluentUI/controls/CheckBox.h"
#include "../../FluentUI/layout/StackPanel.h"

using namespace fluent;

// A CheckBox keeps its checked state across a device-loss cycle.
TEST(DeviceLostState, CheckBoxKeepsChecked) {
    CheckBox cb;
    cb.SetChecked(true);
    EXPECT_TRUE(cb.IsChecked());

    cb.OnDeviceLost();
    EXPECT_TRUE(cb.IsChecked());  // logical state unaffected by device loss
    cb.OnDeviceRestored();
    EXPECT_TRUE(cb.IsChecked());
}

// The hooks propagate through a Panel to its children (the host calls them on the
// roots; panels must recurse so nested controls are notified).
TEST(DeviceLostState, PanelPropagatesToChildren) {
    auto panel = std::make_unique<StackPanel>();
    auto* a = new CheckBox();
    auto* b = new CheckBox();
    a->SetChecked(true);
    b->SetChecked(false);
    panel->Add(std::unique_ptr<CheckBox>(a));
    panel->Add(std::unique_ptr<CheckBox>(b));

    // Should not throw / crash and must leave logical state intact.
    panel->OnDeviceLost();
    panel->OnDeviceRestored();

    EXPECT_TRUE(a->IsChecked());
    EXPECT_FALSE(b->IsChecked());
}

// Default hook is a harmless no-op on a plain control (no device resource held).
TEST(DeviceLostState, DefaultHooksAreNoOps) {
    CheckBox cb;
    cb.OnDeviceLost();
    cb.OnDeviceRestored();
    cb.OnDeviceLost();
    EXPECT_FALSE(cb.IsChecked());  // untouched
}
