// AnimationRegistry.h — the active set of currently-animating elements
// (roadmap §6.1: "Active animation list, forbid scanning the whole tree every
// frame").
//
// The host rebuilds this set at discrete trigger points — input events or
// program-driven state changes — by calling Collect() over its element roots.
// Between triggers the timer ticks only the collected set, so per-frame cost is
// O(animating elements) rather than O(whole tree).
//
// Depends on UIElement (to call CollectAnimations / OnAnimationTick /
// WantsAnimationTick) but not on any window or COM type, so it is unit-testable
// against a plain UIElement subtree.
#pragma once

#include <vector>

namespace fluent {

class UIElement;

class AnimationRegistry {
public:
    // Rebuild the active set from the given roots. Each root's CollectAnimations
    // appends the elements that currently want a tick (leaves add themselves,
    // panels recurse). Call this once per trigger, not per frame.
    void Collect(const std::vector<UIElement*>& roots);

    // Advance every element in the active set by dt seconds, then drop the ones
    // that no longer want a tick. Returns true while the set is still non-empty
    // (i.e. the host should keep the timer running). Does not render.
    bool Tick(float dtSec);

    // Drop an element from the active set immediately (e.g. it is being detached
    // from the tree / destroyed). No-op if it was not animating. Keeps a torn-down
    // element from being ticked between the detach and the next Collect.
    void Remove(UIElement* e);

    bool Empty() const { return active_.empty(); }
    size_t Count() const { return active_.size(); }

private:
    std::vector<UIElement*> active_;
};

} // namespace fluent
