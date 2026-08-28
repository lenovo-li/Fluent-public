// PopupState.h — explicit popup lifecycle state + pure transition rules.
//
// Phase B1 of the popup-lifetime refactor (roadmap §5.5) replaces the implicit
// `bool open_` on PopupHost with a four-state machine so intermediate states
// (Opening / Closing) are representable and re-entrancy during close is guarded.
//
// The transition *decisions* live here as pure functions so they can be unit
// tested without a HWND / DComp device. PopupHost holds a PopupState and calls
// these to decide whether an Open/Close request should proceed.
#pragma once

namespace fluent {

enum class PopupState {
    Closed,   // not shown; the default and post-close state
    Opening,  // building graphics / drawing the first frame (transient)
    Open,     // fully shown and interactive
    Closing,  // running close side effects (onClose_); rejects re-open (transient)
};

// Should an Open() request proceed given the current state? Only a Closed popup
// may open. Opening/Open are already (becoming) open; Closing must finish first
// (re-open during close is rejected in v1 rather than queued).
inline bool CanOpen(PopupState s) {
    return s == PopupState::Closed;
}

// Should a Close() request proceed? Only a fully Open popup runs the close
// sequence. Closed/Closing are no-ops (idempotent); Opening is treated as
// not-yet-open and also a no-op (the Open path will settle it).
inline bool CanClose(PopupState s) {
    return s == PopupState::Open;
}

// IsOpen for external callers: true only in the fully-Open state.
inline bool IsOpenState(PopupState s) {
    return s == PopupState::Open;
}

} // namespace fluent
