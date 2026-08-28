// InputTypes.h — value types shared by the input-routing layer (roadmap §9).
//
// These are the primitives the window translates raw Win32 messages into before
// handing them to the InputManager: a modifier-key bitset (sampled from
// GetKeyState), which pointer button changed, and a point in window DIPs. Pure
// value types with no Windows dependency beyond the integer widths, so they are
// unit-testable on their own.
#pragma once

#include <cstdint>

namespace fluent {

// Keyboard modifiers active at the time of an input event. A bitset so a handler
// can test Ctrl+Shift etc. Sampled from GetKeyState by the window, not stored.
enum class ModifierKeys : uint32_t {
    None = 0,
    Ctrl = 1 << 0,
    Shift = 1 << 1,
    Alt = 1 << 2,
};

inline ModifierKeys operator|(ModifierKeys a, ModifierKeys b) {
    return static_cast<ModifierKeys>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline ModifierKeys operator&(ModifierKeys a, ModifierKeys b) {
    return static_cast<ModifierKeys>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline ModifierKeys& operator|=(ModifierKeys& a, ModifierKeys b) { a = a | b; return a; }
inline bool HasModifier(ModifierKeys set, ModifierKeys flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

// Which pointer button a press/release event concerns. Move/wheel events use
// None (no button transition).
enum class PointerButton { None, Left, Right, Middle };

// A point in window client DIPs (device-independent pixels; physical / dpiScale).
struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

}  // namespace fluent
