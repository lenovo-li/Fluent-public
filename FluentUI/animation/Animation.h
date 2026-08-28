// Animation.h — IDCompositionAnimation helpers with Fluent easing.
//
// Animations run on the DComp compositor thread, so they never block the UI
// thread. We approximate the standard Fluent cubic-bezier easings with the
// cubic segments DComp natively supports.
#pragma once

#include "../fl_common.h"
#include <dcomp.h>

namespace fluent {

// Standard Fluent motion durations (seconds).
constexpr double kFastDuration = 0.10;     // hover
constexpr double kNormalDuration = 0.167;  // press / state changes
constexpr double kScrollDuration = 0.30;   // smooth-scroll settle (decelerate)

// A decelerate cubic (approx cubic-bezier 0,0,0,1) mapping `from`→`to` over
// `durationSec`, expressed as the polynomial a*t^3 + b*t^2 + c*t + d that DComp's
// AddCubic evaluates DIRECTLY in value space. The SAME struct is used to (a) build
// the IDCompositionAnimation and (b) evaluate the curve on the UI thread (Eval)
// so a control's hit-test / scrollbar read the identical value the compositor is
// showing — otherwise a click mid-animation would hit the wrong place (§11.10).
// Outside [0, durationSec] the value is clamped to `from` (t<=0) / `to` (t>=dur).
struct DecelerateCubic {
    double d = 0.0;   // constant  (= from)
    double c = 0.0;   // linear
    double b = 0.0;   // square
    double a = 0.0;   // cube
    double from = 0.0, to = 0.0, durationSec = 0.0;

    static DecelerateCubic Make(double from, double to, double durationSec) {
        DecelerateCubic k;
        k.from = from; k.to = to;
        k.durationSec = durationSec > 1e-4 ? durationSec : 1e-4;
        const double delta = to - from;
        const double dur = k.durationSec;
        // Ease-out cubic fitted so value(0)=from, value(dur)=to, slope→0 at dur.
        k.d = from;
        k.c = delta * (3.0 / dur);
        k.b = delta * (-3.0 / (dur * dur));
        k.a = delta * (1.0 / (dur * dur * dur));
        return k;
    }

    // Evaluate at elapsed time t (seconds since the tween started), clamped.
    double Eval(double t) const {
        if (t <= 0.0) return from;
        if (t >= durationSec) return to;
        return ((a * t + b) * t + c) * t + d;
    }
};

// Build a one-shot decelerate OffsetY tween (physical pixels) for smooth
// scrolling on the compositor thread (roadmap §11.6). Uses DecelerateCubic so the
// curve matches the UI-thread evaluator exactly. Holds `toPx` after the duration.
// Returns nullptr on failure (caller falls back to SetOffset). `durationSec`
// defaults to the standard scroll settle.
ComPtr<IDCompositionAnimation> MakeOffsetTween(IDCompositionDevice2* device,
                                               float fromPx, float toPx,
                                               double durationSec = kScrollDuration);

// Build an INFINITELY looping there-and-back offset sweep for an indeterminate
// ProgressBar segment (roadmap Plan B / §9.3). The returned animation drives a
// visual's OffsetX from `minX` to `maxX` and back with a smooth ease-in-out,
// one full cycle every `cycleSec` seconds, forever — evaluated on the DComp
// compositor thread, so it keeps sweeping even while the UI thread is busy
// (window resize, layout). Returns nullptr on failure (caller falls back to the
// UI-thread sweep). Implemented as a single sinusoidal primitive:
//   OffsetX(t) = bias + amplitude * sin(360*frequency*t + phase)   [degrees, Hz]
// with bias = amplitude = (maxX-minX)/2, frequency = 1/cycleSec, phase = -90 so
// it starts exactly at minX, peaks at maxX at the half cycle, and returns. A
// sinusoidal is inherently periodic, so no End()/AddRepeat is needed to loop.
// `phaseDeg` is the sinusoid's start phase in DEGREES (default -90 → starts at
// minX). A phase-continuous re-fit (e.g. when the bar width changes on resize)
// passes the sweep's current phase so the segment does not jump: same normalized
// position, new travel range.
ComPtr<IDCompositionAnimation> MakeOffsetSweep(IDCompositionDevice2* device,
                                               float minX, float maxX,
                                               double cycleSec,
                                               float phaseDeg = -90.0f);

// The OS caret blink half-period (seconds) — the time the caret stays solid, which
// is also the time it stays hidden. Reads GetCaretBlinkTime() and falls back to the
// Windows default when the user disabled blinking (0) or pinned it on (INFINITE).
// Same source the window's blink timer uses, so both paths agree.
double CaretBlinkHalfPeriodSec();

// Build an INFINITELY looping caret blink for a visual's OPACITY: solid (1.0) for
// `halfPeriodSec`, hidden (0.0) for `halfPeriodSec`, forever — evaluated on the DComp
// compositor thread, so the caret keeps blinking with no UI-thread timer and no
// repaint (the UI path costs a full-window Render every half period).
//
// A hard on/off square wave, matching the OS caret and the UI-thread path it
// replaces. DComp has no step primitive, so it is built from two CONSTANT cubic
// segments (only the `constant` coefficient is non-zero) plus a repeat:
//   AddCubic(0,          1.0, 0,0,0)   -> hold solid  over [0, h)
//   AddCubic(h,          0.0, 0,0,0)   -> hold hidden over [h, 2h)
//   AddRepeat(2h, 2h)                  -> loop
// AddRepeat replays the interval [beginOffset - durationToRepeat, beginOffset), i.e.
// [0, 2h) here — NOT [0, durationToRepeat) as the parameter name suggests. Segments
// must be added in increasing beginOffset order, a repeat cannot be the first
// segment, and End() must NOT be called (it would stop the loop).
// Returns nullptr on failure (caller falls back to a static opaque caret).
ComPtr<IDCompositionAnimation> MakeBlink(IDCompositionDevice2* device,
                                         double halfPeriodSec);

} // namespace fluent
