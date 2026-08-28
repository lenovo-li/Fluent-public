// Animation.cpp

#include "Animation.h"

namespace fluent {

namespace {
const char* kTag = "Animation";
}

ComPtr<IDCompositionAnimation> MakeOffsetTween(IDCompositionDevice2* device,
                                               float fromPx, float toPx,
                                               double durationSec) {
    ComPtr<IDCompositionAnimation> anim;
    if (!device || FAILED(device->CreateAnimation(anim.GetAddressOf()))) {
        TraceMsg(kTag, "CreateAnimation (offset tween) failed");
        return nullptr;
    }
    // Same coefficients the UI-thread evaluator uses (DecelerateCubic), so the
    // hit-test / scrollbar value never disagrees with what the compositor shows.
    const DecelerateCubic k = DecelerateCubic::Make(fromPx, toPx, durationSec);
    anim->AddCubic(/*beginOffset*/ 0.0,
                   /*constant d*/ static_cast<float>(k.d),
                   /*linear  c*/ static_cast<float>(k.c),
                   /*square  b*/ static_cast<float>(k.b),
                   /*cube    a*/ static_cast<float>(k.a));
    anim->End(k.durationSec, static_cast<float>(toPx));  // hold the target
    return anim;
}

ComPtr<IDCompositionAnimation> MakeOffsetSweep(IDCompositionDevice2* device,
                                               float minX, float maxX,
                                               double cycleSec, float phaseDeg) {
    ComPtr<IDCompositionAnimation> anim;
    if (!device || FAILED(device->CreateAnimation(anim.GetAddressOf()))) {
        TraceMsg(kTag, "CreateAnimation (sweep) failed");
        return nullptr;
    }
    if (cycleSec <= 0.0) cycleSec = 1.4;  // guard: fall back to the historical cycle

    // A single sinusoidal primitive, evaluated on the compositor thread:
    //   value(t) = bias + amplitude * sin(360*frequency*t + phase)
    // (sin of an angle in DEGREES; frequency in Hz — see AddSinusoidal docs).
    // Center the swing on the travel midpoint and set amplitude to half the
    // travel so it spans [minX, maxX]. phase = -90 deg makes sin start at -1, so
    // value(0) = bias - amplitude = minX; at the half cycle sin = +1 => maxX;
    // then it eases back. Inherently periodic => loops forever with no End().
    const float travel = maxX - minX;
    const float amplitude = travel * 0.5f;
    const float bias = minX + amplitude;
    const float frequency = static_cast<float>(1.0 / cycleSec);
    anim->AddSinusoidal(/*beginOffset*/ 0.0, bias, amplitude, frequency, phaseDeg);
    return anim;
}

double CaretBlinkHalfPeriodSec() {
    UINT ms = GetCaretBlinkTime();
    // 0 = blinking disabled, INFINITE = caret pinned solid. Neither is a usable
    // period; fall back to the Windows default (matches NativeWindowHost's timer).
    if (ms == 0 || ms == INFINITE) ms = 530;
    return static_cast<double>(ms) / 1000.0;
}

ComPtr<IDCompositionAnimation> MakeBlink(IDCompositionDevice2* device,
                                        double halfPeriodSec) {
    ComPtr<IDCompositionAnimation> anim;
    if (!device || FAILED(device->CreateAnimation(anim.GetAddressOf()))) {
        TraceMsg(kTag, "CreateAnimation (blink) failed");
        return nullptr;
    }
    const double h = halfPeriodSec > 1e-3 ? halfPeriodSec : 0.530;

    // Two CONSTANT segments = a square wave (see header for the AddRepeat semantics).
    anim->AddCubic(/*beginOffset*/ 0.0, /*constant*/ 1.0f, 0.0f, 0.0f, 0.0f);
    anim->AddCubic(/*beginOffset*/ h,   /*constant*/ 0.0f, 0.0f, 0.0f, 0.0f);
    // Replays [2h - 2h, 2h) = [0, 2h). No End() — that would terminate the loop.
    HRESULT hr = anim->AddRepeat(/*beginOffset*/ 2.0 * h, /*durationToRepeat*/ 2.0 * h);
    if (FAILED(hr)) {
        Trace(kTag, "AddRepeat (blink) FAILED", hr);
        return nullptr;  // a non-looping blink would freeze the caret hidden
    }
    return anim;
}

} // namespace fluent
