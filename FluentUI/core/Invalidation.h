// Invalidation.h — layout/render dirty flags for the element tree.
//
// Phase 2 of the tree refactor separates "needs re-layout" from "needs repaint".
// Historically every change funneled through Invalidate() and repainted the
// whole window; content that changed its size still relied on the next WM_SIZE
// to re-run layout. These flags let a change say precisely what it dirtied:
//
//   Measure   — the element's desired size may have changed (text, font, items).
//               Forces a Measure pass, which implies Arrange + Render.
//   Arrange   — the element's position/size within its slot may have changed
//               (alignment, margin) but its desired size did not. Implies Render.
//   Render    — only the pixels changed (hover, pressed, caret blink, color).
//
// A dirty Measure implies Arrange and Render; a dirty Arrange implies Render.
// The bits are a plain bitmask so an element can accumulate several between
// frames and the host can test/clear them cheaply.
#pragma once

namespace fluent {

enum class DirtyFlags : unsigned {
    None    = 0,
    Measure = 1u << 0,
    Arrange = 1u << 1,
    Render  = 1u << 2,
};

inline DirtyFlags operator|(DirtyFlags a, DirtyFlags b) {
    return static_cast<DirtyFlags>(static_cast<unsigned>(a) |
                                   static_cast<unsigned>(b));
}
inline DirtyFlags operator&(DirtyFlags a, DirtyFlags b) {
    return static_cast<DirtyFlags>(static_cast<unsigned>(a) &
                                   static_cast<unsigned>(b));
}
inline DirtyFlags& operator|=(DirtyFlags& a, DirtyFlags b) {
    a = a | b;
    return a;
}
inline bool Any(DirtyFlags f) { return f != DirtyFlags::None; }
inline bool Has(DirtyFlags f, DirtyFlags test) { return Any(f & test); }

// Expand a raw dirty request into its implied flags: Measure implies Arrange and
// Render; Arrange implies Render. Callers set the highest-level flag they need
// and the tree records the full closure, so the host never re-layouts when only
// pixels changed, and never repaints stale geometry after a size change.
inline DirtyFlags ExpandDirty(DirtyFlags f) {
    if (Has(f, DirtyFlags::Measure)) f |= DirtyFlags::Arrange | DirtyFlags::Render;
    if (Has(f, DirtyFlags::Arrange)) f |= DirtyFlags::Render;
    return f;
}

} // namespace fluent
