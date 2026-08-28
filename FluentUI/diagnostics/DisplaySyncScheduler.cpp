// DisplaySyncScheduler.cpp

#include "DisplaySyncScheduler.h"
#include <dwmapi.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

namespace fluent {

void DisplaySyncScheduler::Initialize(HWND hwnd) {
    DetectRefreshRate(hwnd);
    UpdateFrameBudget();

    // 启动 VSync 同步线程
    if (!running_.exchange(true)) {
        vsyncThread_ = std::thread([this] { VsyncWorker(); });
    }
}

void DisplaySyncScheduler::Shutdown() {
    if (running_.exchange(false)) {
        if (vsyncThread_.joinable()) {
            vsyncThread_.join();
        }
    }
}

void DisplaySyncScheduler::DetectRefreshRate(HWND hwnd) {
    // 方法1: 通过 DWM 获取精确刷新率
    DWM_TIMING_INFO timingInfo = {sizeof(DWM_TIMING_INFO)};
    HRESULT hr = DwmGetCompositionTimingInfo(nullptr, &timingInfo);

    if (SUCCEEDED(hr) && timingInfo.rateRefresh.uiNumerator > 0) {
        double refreshRate = static_cast<double>(timingInfo.rateRefresh.uiNumerator) /
                            timingInfo.rateRefresh.uiDenominator;
        displayInfo_.refreshRate = static_cast<int>(std::round(refreshRate));
    } else {
        // 方法2: 降级到 EnumDisplaySettings
        displayInfo_.hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);

        MONITORINFOEXW monitorInfo = {sizeof(MONITORINFOEXW)};
        if (GetMonitorInfoW(displayInfo_.hMonitor, &monitorInfo)) {
            DEVMODEW devMode = {0};
            devMode.dmSize = sizeof(DEVMODEW);

            if (EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
                displayInfo_.refreshRate = devMode.dmDisplayFrequency;
            }
        }
    }

    // 降级：无法检测刷新率时默认 60Hz
    if (displayInfo_.refreshRate <= 0 || displayInfo_.refreshRate > 500) {
        displayInfo_.refreshRate = 60;
    }

    displayInfo_.frameInterval = 1000.0 / displayInfo_.refreshRate;
}

void DisplaySyncScheduler::UpdateFrameBudget() {
    // 预算分配策略:
    // - Layout: 30% 帧时间
    // - Render: 50% 帧时间
    // - 系统开销/输入处理: 20% 帧时间

    displayInfo_.layoutBudget = displayInfo_.frameInterval * 0.30;
    displayInfo_.renderBudget = displayInfo_.frameInterval * 0.50;

    // 高刷新率特殊处理：预算太少容易超标，允许跨帧执行
    if (displayInfo_.refreshRate >= 240) {
        // 240Hz (4.17ms/帧) → 允许跨 2 帧执行 layout
        displayInfo_.layoutBudget *= 2.0;
    } else if (displayInfo_.refreshRate >= 120) {
        // 120Hz (8.33ms/帧) → 允许跨 1.5 帧
        displayInfo_.layoutBudget *= 1.5;
    }
}

bool DisplaySyncScheduler::ShouldPerformLayout() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(
        now - lastLayoutTime_).count();

    // 根据刷新率决定最小间隔
    double minInterval = displayInfo_.frameInterval;

    if (displayInfo_.refreshRate >= 240) {
        // 240Hz: 允许每 2 帧做一次 layout
        minInterval *= 2.0;
    } else if (displayInfo_.refreshRate >= 120) {
        // 120Hz: 每 1 帧做一次 layout
        minInterval *= 1.0;
    }

    if (elapsed >= minInterval) {
        lastLayoutTime_ = now;
        return true;
    }

    return false;
}

bool DisplaySyncScheduler::ShouldPerformRender() {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(
        now - lastRenderTime_).count();

    // Render 总是跟随刷新率(不跳帧), 留 5% 容错
    if (elapsed >= displayInfo_.frameInterval * 0.95) {
        lastRenderTime_ = now;
        return true;
    }

    return false;
}

void DisplaySyncScheduler::RequestAnimationFrame(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    pendingCallbacks_.push_back(std::move(callback));
}

void DisplaySyncScheduler::VsyncWorker() {
    while (running_) {
        // 等待 DWM VSync
        DWM_TIMING_INFO timingInfo = {sizeof(DWM_TIMING_INFO)};
        HRESULT hr = DwmGetCompositionTimingInfo(nullptr, &timingInfo);

        if (SUCCEEDED(hr)) {
            // 在 VSync 时机执行回调
            std::vector<std::function<void()>> callbacks;
            {
                std::lock_guard<std::mutex> lock(callbackMutex_);
                callbacks.swap(pendingCallbacks_);
            }

            for (auto& cb : callbacks) {
                if (cb) cb();
            }
        }

        // 等到下一个 VSync (留 5% 容错避免错过)
        auto sleepMs = static_cast<long long>(displayInfo_.frameInterval * 0.95);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

} // namespace fluent
