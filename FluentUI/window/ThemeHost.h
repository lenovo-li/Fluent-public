// ThemeHost.h — theme tokens and native window appearance.
//
// Encapsulates the two theme-related subsystems NativeWindowHost used to hold:
// ThemeManager (the token snapshot: colors, spacing, typography, motion — the
// single source of truth the element tree reads) and WindowAppearance (the
// native DWM surface: Mica/Acrylic backdrop, dark title bar, corner radius,
// drop shadow).
//
// They are grouped because a theme change drives both: switching to dark mode
// updates the token snapshot AND flips the native title bar, and a high-contrast
// backdrop resolution feeds back into the token snapshot.
#pragma once

#include "../fl_common.h"
#include "../styling/ThemeManager.h"
#include "WindowAppearance.h"

namespace fluent {

// ThemeHost owns the theme token manager and the native appearance controller.
// The window asks it for the current snapshot (handed to UIContext once at
// startup — the pointer is stable for the window's lifetime) and drives theme
// changes through it.
class ThemeHost {
public:
    ThemeHost() = default;

    // Accessors for the two subsystems
    ThemeManager& Manager() { return manager_; }
    WindowAppearance& Appearance() { return appearance_; }

    const ThemeManager& Manager() const { return manager_; }
    const WindowAppearance& Appearance() const { return appearance_; }

    // Convenience pass-throughs for the hot paths (used on nearly every frame
    // and by every control that reads a colour).
    const ThemeSnapshot& Snapshot() const { return manager_.Snapshot(); }
    const ThemeInputs& Inputs() const { return manager_.Inputs(); }

private:
    ThemeManager manager_;
    WindowAppearance appearance_;
};

} // namespace fluent
