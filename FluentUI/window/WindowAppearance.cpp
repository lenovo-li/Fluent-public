// WindowAppearance.cpp — see WindowAppearance.h. Pure resolver + DWM apply half.

#include "WindowAppearance.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace fluent {

namespace {
const char* kTag = "WindowAppearance";

// DWM attribute / value constants (may be absent in older SDK headers — mirror
// the values so the build does not depend on the SDK version, matching the
// existing #ifndef fallbacks the host already used).
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// DWM_SYSTEMBACKDROP_TYPE values.
constexpr int kSbtAuto = 0;
constexpr int kSbtNone = 1;
constexpr int kSbtMainWindow = 2;      // Mica
constexpr int kSbtTransientWindow = 3; // Acrylic
constexpr int kSbtTabbedWindow = 4;    // Mica Alt

int BackdropToDwmValue(BackdropKind k) {
    switch (k) {
        case BackdropKind::None:    return kSbtNone;
        case BackdropKind::Mica:    return kSbtMainWindow;
        case BackdropKind::MicaAlt: return kSbtTabbedWindow;
        case BackdropKind::Acrylic: return kSbtTransientWindow;
        case BackdropKind::Auto:    return kSbtAuto;
    }
    return kSbtAuto;
}

int CornerToDwmValue(CornerPreference p) {
    switch (p) {
        case CornerPreference::DoNotRound: return 1;  // DWMWCP_DONOTROUND
        case CornerPreference::Round:      return 2;  // DWMWCP_ROUND
        case CornerPreference::RoundSmall: return 3;  // DWMWCP_ROUNDSMALL
        case CornerPreference::Default:    return 0;  // DWMWCP_DEFAULT
    }
    return 0;
}
} // namespace

ResolvedBackdrop ResolveBackdrop(BackdropKind requested, const AppearanceEnv& env) {
    ResolvedBackdrop r;

    // High contrast owns caption colors and needs opaque, legible surfaces: no
    // material, and the app must not force the dark title bar.
    if (env.highContrast) {
        r.effective = BackdropKind::None;
        r.useSolidFallback = true;
        r.reason = FallbackReason::HighContrast;
        r.suppressDarkTitleBar = true;
        return r;
    }
    // An explicit None is honored (solid window, no fallback "downgrade").
    if (requested == BackdropKind::None) {
        r.effective = BackdropKind::None;
        r.useSolidFallback = true;
        r.reason = FallbackReason::RequestedNone;
        return r;
    }
    // A system backdrop needs desktop composition; RDP has none to tint.
    if (env.remoteSession) {
        r.effective = BackdropKind::None;
        r.useSolidFallback = true;
        r.reason = FallbackReason::RemoteSession;
        return r;
    }
    if (!env.dwmComposition) {
        r.effective = BackdropKind::None;
        r.useSolidFallback = true;
        r.reason = FallbackReason::CompositionOff;
        return r;
    }
    // Material requires Windows 11 22000+ (DWMWA_SYSTEMBACKDROP_TYPE).
    if (env.osBuild < kMinMicaBuild) {
        r.effective = BackdropKind::None;
        r.useSolidFallback = true;
        r.reason = FallbackReason::OsTooOld;
        return r;
    }
    // Transparency effects off: the user opted out of see-through materials.
    if (!env.transparency) {
        r.effective = BackdropKind::None;
        r.useSolidFallback = true;
        r.reason = FallbackReason::TransparencyOff;
        return r;
    }

    // Environment supports a material. Auto picks Mica; an explicit material is
    // honored. No solid fallback needed.
    r.effective = (requested == BackdropKind::Auto) ? BackdropKind::Mica : requested;
    r.useSolidFallback = false;
    r.reason = FallbackReason::None;
    return r;
}

CornerPreference CornerPreferenceForRadius(float radiusDip) {
    if (radiusDip <= 0.0f) return CornerPreference::DoNotRound;
    if (radiusDip <= 8.0f) return CornerPreference::RoundSmall;
    return CornerPreference::Round;
}

BaseFillPlan ResolveBaseFill(const ResolvedBackdrop& resolved,
                             const D2D1_COLOR_F& windowBackground,
                             float opacity) {
    const float o = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    BaseFillPlan p;
    if (resolved.useSolidFallback) {
        // No material: WE are the base layer. Opaque unless the app asked otherwise.
        p.fill = true;
        p.color = D2D1_COLOR_F{windowBackground.r, windowBackground.g,
                               windowBackground.b, o};
        return p;
    }
    // Material present. Leave it alone at full opacity; tint it when the app
    // deliberately dialed the window background down.
    if (o >= 1.0f) {
        p.fill = false;
        p.color = D2D1_COLOR_F{0, 0, 0, 0};
        return p;
    }
    p.fill = true;
    p.color = D2D1_COLOR_F{windowBackground.r, windowBackground.g,
                           windowBackground.b, o};
    return p;
}

AppearanceEnv WindowAppearance::ReadEnvironment() {
    AppearanceEnv env;

    // True OS build via RtlGetVersion (ntdll). GetVersionEx is manifest-gated and
    // caps at 6.2 without an explicit compatibility manifest, so it cannot be
    // trusted to detect Windows 11; RtlGetVersion reports the real numbers.
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (fn) {
            RTL_OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) env.osBuild = vi.dwBuildNumber;
        }
    }

    BOOL comp = FALSE;
    env.dwmComposition = SUCCEEDED(DwmIsCompositionEnabled(&comp)) && comp;

    // "Transparency effects" (Settings > Personalization > Colors).
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value,
                     &size) == ERROR_SUCCESS) {
        env.transparency = value != 0;
    }

    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0))
        env.highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;

    env.remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
    return env;
}

ResolvedBackdrop WindowAppearance::ApplyBackdrop(BackdropKind requested) {
    ResolvedBackdrop r = ResolveBackdrop(requested, ReadEnvironment());
    lastResolved_ = r;
    if (!hwnd_) return r;

    // Apply the effective material. When the resolver chose a solid fallback the
    // effective kind is None (kSbtNone) — DWM paints no material and the host
    // paints its solid window background over it.
    int value = BackdropToDwmValue(r.effective);
    HRESULT hr = DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &value,
                                       sizeof(value));
    if (FAILED(hr)) {
        Trace(kTag, "set SYSTEMBACKDROP_TYPE failed (falling back to solid)", hr);
        // Treat an apply failure as a solid fallback so the host paints opaque.
        lastResolved_.useSolidFallback = true;
        lastResolved_.effective = BackdropKind::None;
    }
    return lastResolved_;
}

bool WindowAppearance::SetDarkTitleBar(bool dark) {
    if (!hwnd_ || lastResolved_.suppressDarkTitleBar) return false;
    BOOL v = dark ? TRUE : FALSE;
    HRESULT hr = DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &v,
                                       sizeof(v));
    if (FAILED(hr)) { Trace(kTag, "set IMMERSIVE_DARK_MODE failed", hr); return false; }
    return true;
}

bool WindowAppearance::SetCornerPreference(CornerPreference pref) {
    if (!hwnd_) return false;
    int value = CornerToDwmValue(pref);
    HRESULT hr = DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE,
                                       &value, sizeof(value));
    if (FAILED(hr)) { Trace(kTag, "set WINDOW_CORNER_PREFERENCE failed (pre-Win11?)", hr); return false; }
    return true;
}

bool WindowAppearance::SetShadowEnabled(bool enabled) {
    if (!hwnd_) return false;
    DWMNCRENDERINGPOLICY policy = enabled ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
    HRESULT hr = DwmSetWindowAttribute(hwnd_, DWMWA_NCRENDERING_POLICY,
                                       &policy, sizeof(policy));
    if (FAILED(hr)) {
        Trace(kTag, "set NCRENDERING_POLICY failed", hr);
        return false;
    }
    MARGINS margins = enabled ? MARGINS{1, 1, 1, 1} : MARGINS{0, 0, 0, 0};
    hr = DwmExtendFrameIntoClientArea(hwnd_, &margins);
    if (FAILED(hr)) {
        Trace(kTag, "DwmExtendFrameIntoClientArea for shadow failed", hr);
        return false;
    }
    return true;
}

bool WindowAppearance::SetCaptionColor(COLORREF color) {
    if (!hwnd_) return false;
    HRESULT hr = DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &color,
                                       sizeof(color));
    if (FAILED(hr)) { Trace(kTag, "set CAPTION_COLOR failed (pre-Win11?)", hr); return false; }
    return true;
}

bool WindowAppearance::SetBorderColor(COLORREF color) {
    if (!hwnd_) return false;
    HRESULT hr = DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &color,
                                       sizeof(color));
    if (FAILED(hr)) { Trace(kTag, "set BORDER_COLOR failed (pre-Win11?)", hr); return false; }
    return true;
}

} // namespace fluent
