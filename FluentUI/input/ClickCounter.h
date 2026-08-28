// ClickCounter.h — single / double / triple click detection.
//
// Win32 gives you double-click for free (WM_LBUTTONDBLCLK, once the window class has
// CS_DBLCLKS) and nothing beyond it. Triple-click — select the whole line, which every
// text editor has — has to be counted by hand.
//
// Extracted as a small state machine with INJECTED thresholds rather than living inside
// the window procedure, for the reason PlanRedraw and FramePacing are: a window
// procedure needs a real HWND and a real message pump, so logic that lives there cannot
// be tested at all. The caller supplies GetDoubleClickTime() and
// GetSystemMetrics(SM_CXDOUBLECLK) at the call site; this header calls no Win32.
//
// THE RULE, and why it is not just "count clicks":
//   * a click within `intervalMs` of the previous one AND within `slopDip` of its
//     position continues the streak (2, then 3);
//   * anything else restarts at 1.
// Both conditions matter. Time alone would turn two deliberate clicks on different words
// into a double-click on the second one. Position alone would let a click now and a click
// a minute later at the same spot read as a double-click.
//
// The streak SATURATES at 3 rather than continuing to 4, 5, ... A fourth rapid click is
// reported as 3 again, not 4: nothing in a text editor acts on four clicks, and wrapping
// back to 1 would make a jittery mouse (or an impatient user) randomly collapse a line
// selection back to a caret placement.
//
// USING SYSTEM VALUES IS NOT OPTIONAL. The user can change double-click speed in Control
// Panel, and mouse settings for accessibility can widen the movement tolerance
// substantially. Hard-coding 500 ms / 4 px would silently ignore both, which for a user
// who slowed double-click down means the feature simply does not work and there is
// nothing on screen to explain why.
#pragma once

#include <cstdint>

namespace fluent {

// Tracks the click streak. One instance per window (the streak is per-pointer, and this
// framework has one pointer).
struct ClickCounter {
    // Result of the click that was just registered: 1, 2 or 3.
    int count = 0;

    // Register a press and return its click count.
    //
    //   x, y        - press position. Any consistent unit; compare against slop in the
    //                 SAME unit (physical pixels is the natural choice, since that is
    //                 what SM_CXDOUBLECLK is expressed in).
    //   timeMs      - a monotonically non-decreasing millisecond clock. GetMessageTime()
    //                 is the right source in a window procedure: it is the time the
    //                 message was POSTED, so a slow frame between two clicks cannot
    //                 stretch the measured interval and break a genuine double-click.
    //   intervalMs  - GetDoubleClickTime().
    //   slopDip     - GetSystemMetrics(SM_CXDOUBLECLK) (and SM_CYDOUBLECLK; they are
    //                 independent in principle but equal in practice, so one value with
    //                 a box test is enough).
    int Register(float x, float y, uint32_t timeMs, uint32_t intervalMs, float slop) {
        const bool inTime = count > 0 && (timeMs - lastTimeMs_) <= intervalMs;
        // Box test rather than a radius: that is how Win32 itself decides, and a circle
        // would disagree with the OS at the corners for no benefit.
        const bool inPlace = count > 0 &&
                             Abs(x - lastX_) <= slop && Abs(y - lastY_) <= slop;

        if (inTime && inPlace) {
            if (count < 3) ++count;   // saturate; see the header note
        } else {
            count = 1;
        }
        lastX_ = x;
        lastY_ = y;
        lastTimeMs_ = timeMs;
        return count;
    }

    // Forget the streak. Call when something happens that should not be "clicked
    // through" — the window losing activation, or a different button being pressed.
    // Without this, clicking away and back could resume a stale streak.
    void Reset() { count = 0; }

private:
    static float Abs(float v) { return v < 0.0f ? -v : v; }

    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    // Unsigned, and compared via subtraction, which handles the ~49-day
    // GetMessageTime wrap CORRECTLY rather than merely tolerably: modular arithmetic
    // makes (small - large) equal the true elapsed interval across the wrap, so a
    // double-click that happens to straddle the boundary still registers as one. This
    // only works because the subtraction is done in the unsigned type — computing
    // `(int)timeMs - (int)lastTimeMs_` instead would give a huge negative number and
    // silently break the streak once every 49 days.
    uint32_t lastTimeMs_ = 0;
};

} // namespace fluent
