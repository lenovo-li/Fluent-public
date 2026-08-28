// PopupStateTests.cpp — pure-logic tests for the popup state machine rules
// (phase B1). No HWND / DComp: exercises the CanOpen / CanClose / IsOpenState
// decision functions that PopupHost delegates its transitions to.

#include "../framework/Test.h"
#include "../../FluentUI/services/PopupState.h"

using namespace fluent;

// Only a Closed popup may open.
TEST(PopupState, OnlyClosedCanOpen) {
    EXPECT_TRUE(CanOpen(PopupState::Closed));
    EXPECT_FALSE(CanOpen(PopupState::Opening));
    EXPECT_FALSE(CanOpen(PopupState::Open));
    EXPECT_FALSE(CanOpen(PopupState::Closing));  // re-open during close rejected
}

// Only a fully-Open popup begins closing (idempotent elsewhere).
TEST(PopupState, OnlyOpenCanClose) {
    EXPECT_FALSE(CanClose(PopupState::Closed));   // already closed: no-op
    EXPECT_FALSE(CanClose(PopupState::Opening));  // not yet open: no-op
    EXPECT_TRUE(CanClose(PopupState::Open));
    EXPECT_FALSE(CanClose(PopupState::Closing));  // already closing: no-op
}

// IsOpen for external callers is true only in the steady Open state.
TEST(PopupState, IsOpenOnlyInOpenState) {
    EXPECT_FALSE(IsOpenState(PopupState::Closed));
    EXPECT_FALSE(IsOpenState(PopupState::Opening));
    EXPECT_TRUE(IsOpenState(PopupState::Open));
    EXPECT_FALSE(IsOpenState(PopupState::Closing));
}

// The intended happy-path cycle Closed -> Opening -> Open -> Closing -> Closed
// is expressible and each guard agrees at each step.
TEST(PopupState, HappyPathCycleGuards) {
    PopupState s = PopupState::Closed;
    EXPECT_TRUE(CanOpen(s));

    s = PopupState::Opening;    // Open() in progress
    EXPECT_FALSE(CanOpen(s));   // no double-open
    EXPECT_FALSE(CanClose(s));  // not yet closable

    s = PopupState::Open;       // settled open
    EXPECT_TRUE(IsOpenState(s));
    EXPECT_TRUE(CanClose(s));

    s = PopupState::Closing;    // Close() side effects running
    EXPECT_FALSE(CanClose(s));  // reentrant Close is a no-op
    EXPECT_FALSE(CanOpen(s));   // reentrant Open rejected

    s = PopupState::Closed;     // settled closed
    EXPECT_TRUE(CanOpen(s));
}
