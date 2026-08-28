// ThemeManager.h — owns the one stable ThemeSnapshot for a window (roadmap §11).
//
// The host holds a ThemeManager; it builds a snapshot from three inputs
// (dark mode, high-contrast, resolved OS accent) and hands out a stable pointer
// to it via UIContext.theme. When the theme changes the host calls Set*/Apply
// and the manager overwrites its snapshot IN PLACE and bumps `generation` — the
// pointer parked on every attached element's UIContext stays valid, only the
// pointee changes. This mirrors how the renderer rebuilds d2d_ in place on
// device recovery so borrowed pointers stay live.
//
// The snapshot BUILDER is pure: OS-specific reads (DwmGetColorizationColor,
// UISettings, the AppsUseLightTheme registry key) happen in the host, which
// passes the already-resolved accent color and the dark/high-contrast bools in.
// This keeps ThemeManager testable headlessly and free of live-session APIs.
#pragma once

#include "ThemeTokens.h"

namespace fluent {

// Inputs the host resolves (from OS settings / window state) and feeds the
// builder. Accent is pre-resolved to a D2D color by the host; on read failure
// the host passes the hard-coded fallback (light 0x0067C0 / dark 0x4CC2FF),
// which BuildSnapshot also uses when useCustomAccent is false.
struct ThemeInputs {
    bool dark = false;
    bool highContrast = false;
    bool useCustomAccent = false;  // true => accent/accentHover/... come from OS
    D2D1_COLOR_F accent{};         // resolved OS accent (valid iff useCustomAccent)

    // User override layers (Win11 polish A0). All fields start "unset"; the
    // builder layers the set ones over the Win11 defaults. Unset fields are
    // no-ops, so default inputs reproduce today's snapshot exactly.
    SpacingOverrides spacing;
    TypographyOverrides typography;
    ColorOverrides colors;
    MotionOverrides motion;
};

// Build an immutable snapshot from the inputs. Pure and free of OS calls: the
// default token values reproduce the legacy Palette exactly (zero visual change)
// and `generation` is set by the caller (the manager stamps its epoch). Exposed
// as a free function so tests can build a snapshot without a manager instance.
ThemeSnapshot BuildSnapshot(const ThemeInputs& in, uint32_t generation);

// The inverse direction: turn a complete snapshot into inputs whose override
// layer pins EVERY field to that snapshot's values. Feeding the result back
// through BuildSnapshot reproduces the input snapshot in everything except
// `generation` (which the caller/manager stamps).
//
// This is what makes "load a whole theme at once" work — an app that computed a
// theme offline (parsed JSON, generated per user) does not have to know which of
// the ~35 fields differ from the built-in defaults. Extracted as a free function
// rather than living inside NativeWindowHost::SetTheme so the round-trip is
// unit-testable with no window (the codebase convention for this).
ThemeInputs ThemeInputsFromSnapshot(const ThemeSnapshot& snap);

// True if the OS "apps use light theme" setting is off (i.e. dark). Reads the
// live registry value; the host calls it to seed / follow the system theme.
// Lives here (not on a control) because it is an OS query, not a token.
bool SystemUsesDarkMode();

class ThemeManager {
public:
    ThemeManager() { Rebuild(); }

    // The stable snapshot pointer handed to UIContext.theme. Never invalidated
    // for the manager's lifetime; its pointee is overwritten in place on a
    // theme change (see class comment).
    const ThemeSnapshot& Snapshot() const { return current_; }

    // Current inputs (so the host can flip one field and Rebuild).
    const ThemeInputs& Inputs() const { return inputs_; }

    // Replace the inputs and rebuild the snapshot in place, bumping generation.
    void SetInputs(const ThemeInputs& in) { inputs_ = in; Rebuild(); }
    void SetDark(bool dark) { inputs_.dark = dark; Rebuild(); }
    void SetHighContrast(bool hc) { inputs_.highContrast = hc; Rebuild(); }
    void SetAccent(const D2D1_COLOR_F& accent) {
        inputs_.useCustomAccent = true;
        inputs_.accent = accent;
        Rebuild();
    }

    uint32_t Generation() const { return current_.generation; }

private:
    void Rebuild() { current_ = BuildSnapshot(inputs_, ++generation_); }

    ThemeInputs inputs_;
    ThemeSnapshot current_;
    uint32_t generation_ = 0;
};

} // namespace fluent
