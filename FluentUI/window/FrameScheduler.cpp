// FrameScheduler.cpp — see FrameScheduler.h.

#include "FrameScheduler.h"

#include "../diagnostics/PerformanceCounters.h"  // QpcFrequency

namespace fluent {

void FrameScheduler::SetWanted(bool wanted) {
    if (wanted && !running_) Arm();
    else if (!wanted && running_) Disarm();
}

void FrameScheduler::Arm() {
    running_ = true;
    lastTickQpc_ = 0;  // first tick uses the nominal interval
    if (arm_) arm_();
}

void FrameScheduler::Disarm() {
    running_ = false;
    if (disarm_) disarm_();
}

float FrameScheduler::ComputeDt(int64_t nowQpc) {
    float dt;
    if (lastTickQpc_ == 0) {
        dt = kIntervalMs / 1000.0f;  // first tick after an arm: nominal
    } else {
        int64_t freq = QpcFrequency();
        dt = freq > 0
                 ? static_cast<float>(static_cast<double>(nowQpc - lastTickQpc_) /
                                      static_cast<double>(freq))
                 : (kIntervalMs / 1000.0f);
    }
    lastTickQpc_ = nowQpc;
    if (dt > kMaxDtSec) dt = kMaxDtSec;  // clamp after a stall
    if (dt < 0.0f) dt = 0.0f;            // guard against a non-monotonic sample
    return dt;
}

} // namespace fluent
