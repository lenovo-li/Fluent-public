// DeviceLostController.cpp — see DeviceLostController.h.

#include "DeviceLostController.h"
#include <d2derr.h>
#include <dxgi.h>

namespace fluent {

DeviceStatus ClassifyDeviceHResult(HRESULT hr) {
    // DXGI_STATUS_OCCLUDED is a SUCCESS code (window minimized / locked / RDP): the
    // present "succeeded" but nothing was shown. Handle it before the SUCCEEDED
    // gate so it reads as Transient (skip / retry), not a normal Ok frame.
    if (hr == DXGI_STATUS_OCCLUDED) return DeviceStatus::Transient;

    if (SUCCEEDED(hr)) return DeviceStatus::Ok;

    switch (hr) {
        // D2D target tied to a lost device — must recreate the render target and
        // everything derived from it.
        case D2DERR_RECREATE_TARGET:
        // The adapter/device was physically removed or reset (TDR, driver update,
        // GPU hot-unplug). The whole D3D/D2D/DXGI stack must be rebuilt.
        case DXGI_ERROR_DEVICE_REMOVED:
        case DXGI_ERROR_DEVICE_RESET:
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
            return DeviceStatus::Lost;

        default:
            // Unknown failure: treat as transient (retry) rather than tearing down
            // the device on a one-off error. A real loss reports one of the codes
            // above and will be caught on the next frame if it persists.
            return DeviceStatus::Transient;
    }
}

bool DeviceLostController::NotifyFrameResult(DeviceStatus status) {
    if (status == DeviceStatus::Lost) return MarkLost();
    return false;
}

bool DeviceLostController::MarkLost() {
    if (state_ == DeviceState::Healthy) {
        state_ = DeviceState::Lost;
        return true;
    }
    return false;
}

bool DeviceLostController::BeginRebuild() {
    if (state_ != DeviceState::Lost) return false;
    state_ = DeviceState::Rebuilding;
    return true;
}

bool DeviceLostController::EndRebuild(bool success) {
    if (state_ != DeviceState::Rebuilding) return IsHealthy();
    if (success) {
        state_ = DeviceState::Healthy;
        ++recoveries_;
    } else {
        state_ = DeviceState::Lost;
    }
    return IsHealthy();
}

} // namespace fluent
