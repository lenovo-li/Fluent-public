// DeviceLostControllerTests.cpp — pure-logic tests for the GPU device-loss state
// machine and HRESULT classifier (roadmap §17, WP-04). No GPU is involved (and
// none can be made to drop its device in this environment), so only the logic is
// exercised here; the real device rebuild path is flagged unverifiable-headless
// in the WP-04 report.

#include "../framework/Test.h"
#include "../../FluentUI/graphics/DeviceLostController.h"
#include <d2derr.h>
#include <dxgi.h>

using namespace fluent;

// --- HRESULT classification -------------------------------------------------

TEST(DeviceLost, ClassifySuccessIsOk) {
    EXPECT_TRUE(ClassifyDeviceHResult(S_OK) == DeviceStatus::Ok);
}

TEST(DeviceLost, ClassifyRecreateTargetIsLost) {
    EXPECT_TRUE(ClassifyDeviceHResult(D2DERR_RECREATE_TARGET) == DeviceStatus::Lost);
}

TEST(DeviceLost, ClassifyDeviceRemovedIsLost) {
    EXPECT_TRUE(ClassifyDeviceHResult(DXGI_ERROR_DEVICE_REMOVED) == DeviceStatus::Lost);
    EXPECT_TRUE(ClassifyDeviceHResult(DXGI_ERROR_DEVICE_RESET) == DeviceStatus::Lost);
}

TEST(DeviceLost, ClassifyOccludedIsTransient) {
    EXPECT_TRUE(ClassifyDeviceHResult(DXGI_STATUS_OCCLUDED) == DeviceStatus::Transient);
}

TEST(DeviceLost, ClassifyUnknownFailureIsTransient) {
    // An unexpected failure is retried, not treated as a full device loss.
    EXPECT_TRUE(ClassifyDeviceHResult(E_FAIL) == DeviceStatus::Transient);
}

// --- State machine ----------------------------------------------------------

TEST(DeviceLost, StartsHealthy) {
    DeviceLostController c;
    EXPECT_TRUE(c.IsHealthy());
    EXPECT_TRUE(c.State() == DeviceState::Healthy);
    EXPECT_EQ(c.RecoveryCount(), 0u);
}

TEST(DeviceLost, OkFrameStaysHealthy) {
    DeviceLostController c;
    EXPECT_FALSE(c.NotifyFrameResult(DeviceStatus::Ok));
    EXPECT_TRUE(c.IsHealthy());
}

TEST(DeviceLost, TransientFrameStaysHealthy) {
    DeviceLostController c;
    EXPECT_FALSE(c.NotifyFrameResult(DeviceStatus::Transient));
    EXPECT_TRUE(c.IsHealthy());
}

TEST(DeviceLost, LostFrameTransitionsOnce) {
    DeviceLostController c;
    EXPECT_TRUE(c.NotifyFrameResult(DeviceStatus::Lost));   // Healthy -> Lost
    EXPECT_TRUE(c.State() == DeviceState::Lost);
    // A second Lost while already Lost does not re-trigger (returns false).
    EXPECT_FALSE(c.NotifyFrameResult(DeviceStatus::Lost));
}

TEST(DeviceLost, RebuildSuccessRestoresHealthy) {
    DeviceLostController c;
    c.MarkLost();
    EXPECT_TRUE(c.BeginRebuild());
    EXPECT_TRUE(c.State() == DeviceState::Rebuilding);
    EXPECT_TRUE(c.EndRebuild(true));
    EXPECT_TRUE(c.IsHealthy());
    EXPECT_EQ(c.RecoveryCount(), 1u);
}

TEST(DeviceLost, RebuildFailureStaysLost) {
    DeviceLostController c;
    c.MarkLost();
    EXPECT_TRUE(c.BeginRebuild());
    EXPECT_FALSE(c.EndRebuild(false));
    EXPECT_TRUE(c.State() == DeviceState::Lost);
    EXPECT_EQ(c.RecoveryCount(), 0u);
    // A later retry can still succeed.
    EXPECT_TRUE(c.BeginRebuild());
    EXPECT_TRUE(c.EndRebuild(true));
    EXPECT_TRUE(c.IsHealthy());
    EXPECT_EQ(c.RecoveryCount(), 1u);
}

TEST(DeviceLost, BeginRebuildOnlyFromLost) {
    DeviceLostController c;
    EXPECT_FALSE(c.BeginRebuild());  // Healthy: nothing to rebuild
}

TEST(DeviceLost, MultipleRecoveriesCounted) {
    DeviceLostController c;
    for (int i = 0; i < 3; ++i) {
        c.NotifyFrameResult(DeviceStatus::Lost);
        c.BeginRebuild();
        c.EndRebuild(true);
    }
    EXPECT_EQ(c.RecoveryCount(), 3u);
    EXPECT_TRUE(c.IsHealthy());
}
