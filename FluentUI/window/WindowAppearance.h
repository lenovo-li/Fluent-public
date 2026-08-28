// WindowAppearance.h — window material / chrome policy (roadmap §12).
//
// The old host hard-coded `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE,
// DWMSBT_MAINWINDOW)` and logged on failure — no version gate, no fallback, no
// way to pick a different material, no caption/border/corner control. This lifts
// window appearance into two halves:
//
//   1. A PURE DECISION CORE (ResolveBackdrop): given the environment (OS build,
//      DWM composition on/off, transparency enabled, high-contrast, remote
//      session) and a requested BackdropKind, it decides the *effective* material
//      and whether to fall back to a solid fill — encoding the §12.2 degradation
//      ladder. No Win32 calls, fully unit-testable (WindowAppearanceTests).
//
//   2. A WIN32 APPLY HALF (WindowAppearance): reads the real environment
//      (RtlGetVersion via ntdll for the true build number — GetVersionEx lies
//      without a manifest; DwmIsCompositionEnabled; the transparency/high-contrast
//      system settings), runs the decision core, and applies the result through
//      DWM, checking every call's HRESULT and degrading on failure.
//
// Mica is a WINDOW MATERIAL only (it tints the desktop behind the window); it is
// never a control fill. When it is unavailable (pre-Windows 11, composition off,
// RDP, high-contrast) the window falls back to a solid window-background fill so
// content stays readable — the hard acceptance criterion for WP-05.
#pragma once

#include "../fl_common.h"
#include <d2d1.h>
#include <cstdint>

namespace fluent {

// The material an app asks for. Auto lets the resolver pick the best available
// (Mica on Win11 22H2+, else solid). None forces a plain solid window.
enum class BackdropKind {
    Auto,
    None,     // solid window background, no material
    Mica,     // DWMSBT_MAINWINDOW — desktop-tinted base layer
    MicaAlt,  // DWMSBT_TABBEDWINDOW — stronger tint (tabbed apps)
    Acrylic,  // DWMSBT_TRANSIENTWINDOW — blur (flyouts / transient surfaces)
};

// Why the resolver downgraded the requested material (for tracing / diagnostics).
enum class FallbackReason {
    None,               // request honored as-is
    OsTooOld,           // system backdrop needs Windows 11 build 22000+
    CompositionOff,     // DWM composition disabled
    TransparencyOff,    // user turned off transparency effects
    HighContrast,       // high-contrast theme — material suppressed for legibility
    RemoteSession,      // RDP — no desktop composition to tint
    RequestedNone,      // app explicitly asked for None
};

// The environment inputs the decision core reads. All are resolved by the Win32
// half (or supplied directly in a test).
struct AppearanceEnv {
    uint32_t osBuild = 0;          // Windows build number (e.g. 22621)
    bool dwmComposition = true;    // DwmIsCompositionEnabled
    bool transparency = true;      // "Transparency effects" system setting
    bool highContrast = false;     // high-contrast theme active
    bool remoteSession = false;    // running in an RDP session
};

// The resolved decision. `effective` is the material to actually apply;
// `useSolidFallback` is true when the requested material could not be honored and
// the window must paint a solid background instead (Mica etc. suppressed).
struct ResolvedBackdrop {
    BackdropKind effective = BackdropKind::None;
    bool useSolidFallback = true;
    FallbackReason reason = FallbackReason::None;
    // Whether the dark title bar (immersive dark mode) should be suppressed. Under
    // high contrast the OS owns the caption colors, so the app must not force it.
    bool suppressDarkTitleBar = false;
};

// The minimum Windows 11 build that supports DWMWA_SYSTEMBACKDROP_TYPE.
inline constexpr uint32_t kMinMicaBuild = 22000;

// PURE decision core (roadmap §12.2). No Win32 calls — deterministic in its
// inputs so the full degradation ladder is a unit-test truth table.
ResolvedBackdrop ResolveBackdrop(BackdropKind requested, const AppearanceEnv& env);

// --- Window base fill (the other half of the §12.2 ladder) ----------------
//
// ResolveBackdrop only decides the MATERIAL. Something still has to paint the
// opaque base when that material is absent, and for a long time nothing did:
// `useSolidFallback` was computed, traced, and dropped, while the content surface
// cleared to transparent every frame. On Win11 the DWM Mica layer hid the
// omission; on Win10 (build < 22000) there is no material AND no redirection
// bitmap (WS_EX_NOREDIRECTIONBITMAP), so the whole window showed the desktop
// through — including the semi-transparent control fills, which are authored
// against a material base and are only legible over one.
//
// BaseFillPlan closes that gap as pure math, so the policy is a unit test rather
// than a screenshot.
struct BaseFillPlan {
    bool fill = false;             // paint `color` across the redraw region first
    D2D1_COLOR_F color{0, 0, 0, 0};  // straight alpha; a==1 means fully opaque
};

// Decide the window's base fill.
//   * Solid fallback in effect (no material): fill with `windowBackground` at
//     `opacity` — opaque by default, which is the WP-05 acceptance criterion.
//   * Material active and opacity == 1: DO NOT fill. The material IS the base and
//     painting over it would erase the Mica look.
//   * Material active and opacity < 1: the app deliberately asked for a partly
//     translucent window, so tint the material with `windowBackground` at that
//     alpha (fill, but translucent).
// `opacity` is clamped to [0..1].
BaseFillPlan ResolveBaseFill(const ResolvedBackdrop& resolved,
                             const D2D1_COLOR_F& windowBackground,
                             float opacity);

// DWM corner-rounding preference (roadmap §12.3). Mirror of DWM_WINDOW_CORNER_*.
enum class CornerPreference { Default, DoNotRound, Round, RoundSmall };

// DWM exposes corner presets rather than an arbitrary pixel radius. This helper
// turns the application-facing DIP radius into the closest native policy: zero
// keeps square corners, a compact radius requests the small preset, and larger
// values request the normal Fluent window radius.
CornerPreference CornerPreferenceForRadius(float radiusDip);

// The Win32 apply half. Holds the target HWND and applies resolved appearance
// through DWM, verifying each call and tracing failures. Reads the true OS build
// via RtlGetVersion (ntdll) so a missing manifest does not mask the version.
class WindowAppearance {
public:
    void SetHwnd(HWND hwnd) { hwnd_ = hwnd; }

    // Read the live environment (build / composition / transparency / high
    // contrast / remote) into an AppearanceEnv. Static so tests can bypass it.
    static AppearanceEnv ReadEnvironment();

    // Resolve `requested` against the current live environment and apply the
    // material through DWM. Returns the resolution so the host can react (e.g.
    // paint the solid fallback, suppress the dark title bar). Safe before the
    // HWND exists (no-op apply, still returns the resolution).
    ResolvedBackdrop ApplyBackdrop(BackdropKind requested);

    // Apply the immersive dark-mode caption. No-op (returns false) when the
    // resolution asked to suppress it (high contrast) — pass that through.
    bool SetDarkTitleBar(bool dark);

    // Corner rounding (Win11 build 22000+). Returns false if unsupported/failed.
    bool SetCornerPreference(CornerPreference pref);

    // Ask DWM to render or suppress native non-client shadow. Borderless Fluent
    // windows retain WS_THICKFRAME for resize, but need a one-pixel extended frame
    // to make the DWM shadow unambiguous around custom client-area chrome.
    bool SetShadowEnabled(bool enabled);

    // Caption / border colors (Win11 build 22000+). `colorref` is a Win32 COLORREF
    // (0x00BBGGRR); DWMWA_COLOR_DEFAULT / _NONE sentinels are honored by DWM.
    bool SetCaptionColor(COLORREF color);
    bool SetBorderColor(COLORREF color);

    // The last resolution ApplyBackdrop produced (Auto default until first call).
    const ResolvedBackdrop& LastResolved() const { return lastResolved_; }

private:
    HWND hwnd_ = nullptr;
    ResolvedBackdrop lastResolved_;
};

} // namespace fluent
