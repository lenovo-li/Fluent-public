// FrameHost.h — frame scheduling and animation registry.
//
// Encapsulates the frame-timing subsystem that NativeWindowHost used to hold:
// FrameScheduler (arms/disarms the animation clock, computes dt) and
// AnimationRegistry (collects animating elements, ticks them each frame).
#pragma once

#include "../animation/AnimationRegistry.h"
#include "FrameScheduler.h"

namespace fluent {

class UIElement;

// FrameHost owns the frame scheduler and animation registry. The window feeds
// it the element tree to collect animating controls, then ticks the registry
// at the display refresh rate while anything is animating.
class FrameHost {
public:
    FrameHost() = default;

    // Collect animating elements from the tree (called before each frame when
    // the scheduler is running, or on-demand when a control starts animating).
    void Collect(const std::vector<UIElement*>& roots) { anims_.Collect(roots); }

    // Advance all active animations by dt (seconds). Returns true if any are
    // still running (caller should request another frame); false if the active
    // set drained (caller should disarm the scheduler).
    bool Tick(float dt) { return anims_.Tick(dt); }

    // Accessors for the two subsystems
    FrameScheduler& Scheduler() { return scheduler_; }
    AnimationRegistry& Anims() { return anims_; }

    const FrameScheduler& Scheduler() const { return scheduler_; }
    const AnimationRegistry& Anims() const { return anims_; }

private:
    FrameScheduler scheduler_;
    AnimationRegistry anims_;
};

} // namespace fluent
