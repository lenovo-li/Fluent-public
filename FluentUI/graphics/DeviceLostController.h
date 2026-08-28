// DeviceLostController.h — the state machine + HRESULT classifier for GPU device
// loss recovery (roadmap §17). D3D/D2D can drop the device at any time (driver
// reset, GPU TDR, remote-session transition, hybrid-GPU switch); when that
// happens every device-bound resource (swap chains, target bitmaps, brushes)
// must be released and rebuilt while the element tree keeps its logical state
// (text, selection, scroll, focus are all device-independent).
//
// This class is deliberately pure logic — no D2D/DXGI types, no I/O — so the
// transition rules and the HRESULT classification are unit-testable headless
// (there is no way to make a real GPU drop its device in the test environment).
// The host owns one of these, feeds it the HRESULT of each present/draw, and
// drives the actual release+rebuild when the controller says a loss occurred.
#pragma once

#include "../fl_common.h"
#include <cstdint>

namespace fluent {

// How a returned HRESULT should be treated by the frame loop.
enum class DeviceStatus {
    Ok,        // frame succeeded; stay healthy
    Lost,      // device is gone and must be rebuilt (recoverable)
    Transient, // a non-fatal failure (e.g. occluded / a stale target); retry next frame without a rebuild
};

// Classify a present/draw HRESULT (roadmap §17). Device-removal / reset and
// D2DERR_RECREATE_TARGET are "Lost" (rebuild required). DXGI_STATUS_OCCLUDED and
// a plain E_FAIL from a transient target are "Transient" (retry, no rebuild). S_OK
// (and other success codes) are "Ok".
DeviceStatus ClassifyDeviceHResult(HRESULT hr);

// The recovery lifecycle. A frame runs while Healthy; a Lost classification moves
// it to Lost; the host then rebuilds the device (Rebuilding) and, on success,
// returns to Healthy. A failed rebuild keeps it in Lost so the next frame retries
// (bounded by the host's own retry budget).
enum class DeviceState {
    Healthy,
    Lost,
    Rebuilding,
};

class DeviceLostController {
public:
    DeviceState State() const { return state_; }
    bool IsHealthy() const { return state_ == DeviceState::Healthy; }
    // Number of successful recoveries so far (diagnostics / tests).
    uint32_t RecoveryCount() const { return recoveries_; }

    // Feed the classification of a frame's terminal HRESULT. Returns true if this
    // call transitioned Healthy -> Lost (i.e. the host should now run a rebuild).
    // A Transient/Ok result while Healthy is a no-op; an Ok result does not by
    // itself clear a Lost state (only a completed rebuild does, via EndRebuild).
    bool NotifyFrameResult(DeviceStatus status);

    // Mark the device lost explicitly (e.g. a rebuild of a dependent failed).
    // Returns true if it caused a Healthy -> Lost transition.
    bool MarkLost();

    // Begin a rebuild attempt (Lost -> Rebuilding). No-op / false if not Lost.
    bool BeginRebuild();

    // Finish a rebuild attempt. success=true -> Healthy (+1 recovery); false ->
    // back to Lost for a later retry. Returns the resulting IsHealthy().
    bool EndRebuild(bool success);

private:
    DeviceState state_ = DeviceState::Healthy;
    uint32_t recoveries_ = 0;
};

} // namespace fluent
