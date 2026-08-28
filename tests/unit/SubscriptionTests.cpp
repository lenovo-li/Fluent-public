// SubscriptionTests.cpp — unit tests for the RAII Subscription primitive (B2).
// Pure logic: verifies auto-unregister on destruction, move semantics, reset
// idempotency, and empty-subscription safety.

#include "../framework/Test.h"
#include "../../FluentUI/core/Subscription.h"

using namespace fluent;

// Destruction runs the un-register action exactly once.
TEST(Subscription, DestructionUnregistersOnce) {
    int count = 0;
    {
        Subscription sub([&] { ++count; });
        EXPECT_TRUE(sub.active());
        EXPECT_EQ(count, 0);
    }
    EXPECT_EQ(count, 1);
}

// reset() unregisters immediately and is idempotent.
TEST(Subscription, ResetIsIdempotent) {
    int count = 0;
    Subscription sub([&] { ++count; });
    sub.reset();
    EXPECT_EQ(count, 1);
    EXPECT_FALSE(sub.active());
    sub.reset();               // second reset: no-op
    EXPECT_EQ(count, 1);
}

// An empty (default) subscription does nothing on destruction / reset.
TEST(Subscription, EmptyIsSafe) {
    Subscription sub;
    EXPECT_FALSE(sub.active());
    sub.reset();               // no crash, no effect
    EXPECT_FALSE(sub.active());
}

// Move construction transfers ownership; only the destination unregisters.
TEST(Subscription, MoveConstructTransfersOwnership) {
    int count = 0;
    {
        Subscription a([&] { ++count; });
        Subscription b(std::move(a));
        EXPECT_FALSE(a.active());   // moved-from is empty
        EXPECT_TRUE(b.active());
        EXPECT_EQ(count, 0);        // neither fired yet
    }
    EXPECT_EQ(count, 1);            // only b's destruction fired it
}

// Move assignment releases the destination's existing registration first.
TEST(Subscription, MoveAssignReleasesExisting) {
    int first = 0, second = 0;
    Subscription a([&] { ++first; });
    Subscription b([&] { ++second; });
    a = std::move(b);              // a's original registration released now
    EXPECT_EQ(first, 1);           // replaced -> unregistered
    EXPECT_EQ(second, 0);          // b's action now owned by a, not yet fired
    a.reset();
    EXPECT_EQ(second, 1);
}

// Self move-assignment is safe (does not fire or lose the registration).
TEST(Subscription, SelfMoveAssignSafe) {
    int count = 0;
    Subscription a([&] { ++count; });
    a = std::move(a);              // guarded no-op
    EXPECT_EQ(count, 0);
    EXPECT_TRUE(a.active());
    a.reset();
    EXPECT_EQ(count, 1);
}
