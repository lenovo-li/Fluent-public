// DisplaySyncScheduler.h — 自适应显示器刷新率调度器
//
// 根据显示器刷新率（60Hz/120Hz/240Hz）自动调整布局和渲染预算，
// 确保在不同刷新率下都能达到流畅体验：
//   - 60Hz:  16.67ms/帧 → Layout 预算 5.0ms, Render 预算 8.3ms
//   - 120Hz:  8.33ms/帧 → Layout 预算 3.75ms (跨1.5帧), Render 预算 4.2ms
//   - 240Hz:  4.17ms/帧 → Layout 预算 2.5ms (跨2帧), Render 预算 2.1ms
//
// 使用 DWM Composition Timing API 检测精确刷新率，并提供：
//   - 节流：根据刷新率决定是否执行布局（高刷新率允许跳帧）
//   - 预算检查：判断操作是否超出帧预算
//   - VSync 同步：通过 DWM 在垂直同步时机执行回调
#pragma once

#include <chrono>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <Windows.h>

namespace fluent {

class DisplaySyncScheduler {
public:
    struct DisplayInfo {
        int refreshRate = 60;           // Hz
        double frameInterval = 16.67;   // ms/帧
        double layoutBudget = 5.0;      // ms (layout 阶段预算)
        double renderBudget = 8.33;     // ms (render 阶段预算)
        HMONITOR hMonitor = nullptr;
    };

    static DisplaySyncScheduler& Instance() {
        static DisplaySyncScheduler instance;
        return instance;
    }

    // 初始化：检测显示器刷新率并计算帧预算
    void Initialize(HWND hwnd);
    void Shutdown();

    const DisplayInfo& GetDisplayInfo() const { return displayInfo_; }

    // 节流检查：根据刷新率决定是否应该执行布局/渲染
    // 高刷新率时允许跨帧执行布局以留出更多预算
    bool ShouldPerformLayout();
    bool ShouldPerformRender();

    // 预算检查：判断某个操作是否超出帧预算
    bool IsLayoutBudgetExceeded(double elapsedMs) const {
        return elapsedMs > displayInfo_.layoutBudget;
    }
    bool IsRenderBudgetExceeded(double elapsedMs) const {
        return elapsedMs > displayInfo_.renderBudget;
    }

    // VSync 同步：在下一个垂直同步时机执行回调
    void RequestAnimationFrame(std::function<void()> callback);

private:
    DisplaySyncScheduler() = default;
    ~DisplaySyncScheduler() { Shutdown(); }
    DisplaySyncScheduler(const DisplaySyncScheduler&) = delete;
    DisplaySyncScheduler& operator=(const DisplaySyncScheduler&) = delete;

    void DetectRefreshRate(HWND hwnd);
    void UpdateFrameBudget();
    void VsyncWorker();

    DisplayInfo displayInfo_;
    std::chrono::steady_clock::time_point lastLayoutTime_;
    std::chrono::steady_clock::time_point lastRenderTime_;

    // VSync 同步线程
    std::vector<std::function<void()>> pendingCallbacks_;
    std::mutex callbackMutex_;
    std::thread vsyncThread_;
    std::atomic<bool> running_{false};
};

} // namespace fluent
