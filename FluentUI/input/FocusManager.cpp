// FocusManager.cpp — see FocusManager.h.

#include "FocusManager.h"
#include "../core/UIElement.h"

namespace fluent {

void FocusManager::SetFocus(UIElement* e) {
    // Only a focusable element may hold focus; anything else clears it.
    if (e && !e->IsFocusable()) e = nullptr;
    if (e == focused_) return;

    UIElement* old = focused_;
    if (old) old->SetFocused(false);
    focused_ = e;
    if (focused_) focused_->SetFocused(true);

    FocusChangedArgs args{old, focused_};
    focusChanged_.Raise(*this, args);
}

void FocusManager::CollectFocusables(std::vector<UIElement*>& out) const {
    if (!roots_) return;
    for (UIElement* e : *roots_)
        if (e) e->CollectFocusables(out);
}

void FocusManager::MoveNext(bool backward) {
    std::vector<UIElement*> focusables;
    CollectFocusables(focusables);
    if (focusables.empty()) return;

    // Find the current position, then step with wrap-around. With nothing focused,
    // start at the first (forward) or last (backward).
    int current = -1;
    for (int i = 0; i < static_cast<int>(focusables.size()); ++i)
        if (focusables[i] == focused_) { current = i; break; }

    int n = static_cast<int>(focusables.size());
    int next = (current < 0) ? (backward ? n - 1 : 0)
                             : ((current + (backward ? -1 : 1)) % n + n) % n;
    SetFocus(focusables[next]);
}

void FocusManager::MoveDirectional(FocusDirection dir) {
    if (!focused_) return;
    std::vector<UIElement*> focusables;
    CollectFocusables(focusables);
    if (focusables.size() < 2) return;

    const RectDip& cur = focused_->Bounds();
    float cx = cur.x + cur.w * 0.5f;
    float cy = cur.y + cur.h * 0.5f;

    UIElement* best = nullptr;
    float bestDist = 0.0f;
    for (UIElement* cand : focusables) {
        if (cand == focused_) continue;
        const RectDip& r = cand->Bounds();
        float px = r.x + r.w * 0.5f;
        float py = r.y + r.h * 0.5f;
        float dx = px - cx;
        float dy = py - cy;

        // Candidate must lie in the requested direction (dominant axis).
        bool inDir = false;
        switch (dir) {
            case FocusDirection::Up:    inDir = dy < 0 && std::abs(dy) >= std::abs(dx); break;
            case FocusDirection::Down:  inDir = dy > 0 && std::abs(dy) >= std::abs(dx); break;
            case FocusDirection::Left:  inDir = dx < 0 && std::abs(dx) >= std::abs(dy); break;
            case FocusDirection::Right: inDir = dx > 0 && std::abs(dx) >= std::abs(dy); break;
        }
        if (!inDir) continue;

        float dist = dx * dx + dy * dy;
        if (!best || dist < bestDist) { best = cand; bestDist = dist; }
    }
    if (best) SetFocus(best);
}

void FocusManager::OnElementDetached(UIElement* e) {
    // Clear focus if the detaching element holds it. Do NOT call SetFocused(false)
    // on `e` through SetFocus — it may be mid-teardown; just drop the pointer and
    // notify. (The element's own bool is irrelevant once it is leaving the tree.)
    if (focused_ != e) return;
    UIElement* old = focused_;
    focused_ = nullptr;
    FocusChangedArgs args{old, nullptr};
    focusChanged_.Raise(*this, args);
}

}  // namespace fluent
