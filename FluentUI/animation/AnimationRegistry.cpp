// AnimationRegistry.cpp — see AnimationRegistry.h.

#include "AnimationRegistry.h"
#include "../core/UIElement.h"

namespace fluent {

void AnimationRegistry::Collect(const std::vector<UIElement*>& roots) {
    active_.clear();
    for (UIElement* e : roots)
        if (e) e->CollectAnimations(active_);
}

bool AnimationRegistry::Tick(float dtSec) {
    // Tick each active element, then compact out the ones that finished. A
    // control's OnAnimationTick advances its state; WantsAnimationTick reports
    // whether it still has motion left. Iterate a copy-free in-place compaction.
    size_t write = 0;
    for (size_t read = 0; read < active_.size(); ++read) {
        UIElement* e = active_[read];
        if (!e) continue;
        e->OnAnimationTick(dtSec);
        if (e->WantsAnimationTick())
            active_[write++] = e;  // still animating: keep it
    }
    active_.resize(write);
    return !active_.empty();
}

void AnimationRegistry::Remove(UIElement* e) {
    if (!e) return;
    for (size_t i = 0; i < active_.size(); ++i) {
        if (active_[i] == e) {
            active_.erase(active_.begin() + i);
            return;  // an element appears at most once (Collect de-dups by tree walk)
        }
    }
}

} // namespace fluent
