// RepeatButton.cpp

#include "RepeatButton.h"

namespace fluent {

bool RepeatButton::WantsAnimationTick() const {
    // OR with the base: Button drives its hover/press tint from the same callback,
    // and returning false while that tint is mid-flight would freeze it half-lit.
    return repeating_ || Button::WantsAnimationTick();
}

void RepeatButton::OnAnimationTick(float dtSec) {
    Button::OnAnimationTick(dtSec);   // tint animation
    if (!repeating_) return;

    accumSec_ += dtSec;

    // Armed phase: the hold has not yet lasted InitialDelay, so no repeat is due.
    // The press itself already fired once (OnStateChanged), which is why the first
    // repeat is delayed rather than immediate — otherwise a plain click would fire
    // twice in quick succession.
    if (armed_) {
        if (accumSec_ < initialDelaySec_) return;
        armed_ = false;
        accumSec_ = 0.0f;
        OnActivate();
        return;
    }

    // Repeat phase. A while loop, not a single if: a long frame (the resize hitch
    // this repo has been chasing, or a debugger break) must not silently swallow
    // the repeats that were due during it. Capped so a multi-second stall cannot
    // deliver hundreds of clicks at once.
    int fired = 0;
    constexpr int kMaxPerFrame = 4;
    while (accumSec_ >= intervalSec_ && fired < kMaxPerFrame) {
        accumSec_ -= intervalSec_;
        ++fired;
        OnActivate();
    }
    // Whatever remains after the cap is discarded rather than carried: carrying it
    // would make the next frame fire the cap again and turn one stall into a burst
    // that outlives the stall.
    if (fired == kMaxPerFrame) accumSec_ = 0.0f;
}

void RepeatButton::OnStateChanged() {
    Button::OnStateChanged();   // repaint

    // The base UIElement click gesture already owns pointer capture and the
    // pointerDown_/pointerInside_ pair; State() is their resolved product. Driving
    // the burst from the STATE rather than from the pointer events means every way
    // out of a press is covered by construction — release inside, release outside,
    // drag off the control, and being disabled mid-hold all leave Pressed, and none
    // of them needs its own handler here.
    if (State() == VisualState::Pressed) {
        if (!repeating_) {
            repeating_ = true;
            armed_ = true;
            accumSec_ = 0.0f;
            OnActivate();   // fire once on press, like a plain Button
        }
    } else {
        StopRepeat();
    }
}

void RepeatButton::OnActivate() {
    // Delegate to Button, which raises Click (or opens the flyout, if one is set —
    // a RepeatButton with a flyout is a contradiction, but the base handles it and
    // silently disagreeing with it here would be worse).
    Button::OnActivate();
}

void RepeatButton::StopRepeat() {
    if (!repeating_) return;
    repeating_ = false;
    armed_ = false;
    accumSec_ = 0.0f;
    // WantsAnimationTick() now returns false once the tint settles, so the
    // framework stops calling OnAnimationTick on its own.
}

} // namespace fluent
