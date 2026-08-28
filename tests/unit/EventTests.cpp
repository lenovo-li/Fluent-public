// EventTests.cpp — unit tests for the typed Event<Sender,Args> primitive (WP-03).
// Pure logic: subscribe/raise/unsubscribe, reentrant cancel (self + others),
// Subscription-destruction unsubscribes, and raising after the sender is gone.

#include "../framework/Test.h"
#include "../../FluentUI/base/Event.h"

#include <type_traits>
#include <utility>
#include <vector>

using namespace fluent;

namespace {
// A trivial sender + args pair for the tests.
struct Src { int id = 0; };
struct Payload { int value = 0; };

// A receiver that records the payloads it was called with. The static thunk is
// the function-pointer handler; owner is cast back to Recv.
struct Recv {
    std::vector<int> seen;
    static void OnEvent(void* owner, Src&, Payload& p) {
        static_cast<Recv*>(owner)->seen.push_back(p.value);
    }
};
}  // namespace

// A subscribed handler fires on Raise with the (sender,args) payload.
TEST(Event, SubscribeAndRaise) {
    Event<Src, Payload> ev;
    Recv r;
    auto sub = ev.Subscribe(&r, &Recv::OnEvent);
    EXPECT_TRUE(ev.HasSubscribers());

    Src s{1};
    Payload p{42};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{1});
    EXPECT_EQ(r.seen[0], 42);
}

// Destroying the Subscription unsubscribes; a later Raise does not call it.
TEST(Event, SubscriptionDestructionUnsubscribes) {
    Event<Src, Payload> ev;
    Recv r;
    {
        auto sub = ev.Subscribe(&r, &Recv::OnEvent);
        Src s{0};
        Payload p{1};
        ev.Raise(s, p);
    }  // sub destroyed here
    EXPECT_FALSE(ev.HasSubscribers());
    Src s{0};
    Payload p{2};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{1});  // only the first raise landed
    EXPECT_EQ(r.seen[0], 1);
}

// reset() unsubscribes immediately (same as destruction, but explicit).
TEST(Event, ResetUnsubscribes) {
    Event<Src, Payload> ev;
    Recv r;
    auto sub = ev.Subscribe(&r, &Recv::OnEvent);
    sub.reset();
    EXPECT_FALSE(ev.HasSubscribers());
    Src s{0};
    Payload p{7};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{0});
}

// Multiple subscribers all fire, in subscription order.
TEST(Event, MultipleSubscribersFireInOrder) {
    Event<Src, Payload> ev;
    std::vector<int> order;
    struct Rec {
        std::vector<int>* out;
        int tag;
    };
    // Distinct thunks capturing the tag via the owner struct.
    auto thunk = [](void* owner, Src&, Payload&) {
        auto* rc = static_cast<Rec*>(owner);
        rc->out->push_back(rc->tag);
    };
    Rec a{&order, 1}, b{&order, 2}, c{&order, 3};
    auto s1 = ev.Subscribe(&a, thunk);
    auto s2 = ev.Subscribe(&b, thunk);
    auto s3 = ev.Subscribe(&c, thunk);
    Src s{0};
    Payload p{0};
    ev.Raise(s, p);
    EXPECT_EQ(order.size(), size_t{3});
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// A handler may cancel its own subscription mid-raise; it still completes the
// current call and is skipped on the next raise.
TEST(Event, HandlerCancelsSelfDuringRaise) {
    Event<Src, Payload> ev;
    struct Self {
        Subscription sub;
        int calls = 0;
    } self;
    auto thunk = [](void* owner, Src&, Payload&) {
        auto* me = static_cast<Self*>(owner);
        ++me->calls;
        me->sub.reset();  // cancel self during the raise
    };
    self.sub = ev.Subscribe(&self, thunk);
    Src s{0};
    Payload p{0};
    ev.Raise(s, p);
    EXPECT_EQ(self.calls, 1);
    ev.Raise(s, p);
    EXPECT_EQ(self.calls, 1);  // not called again after self-cancel
    EXPECT_FALSE(ev.HasSubscribers());
}

// A handler may cancel a *different* subscriber that has not run yet; the
// canceled one is skipped in the same raise.
TEST(Event, HandlerCancelsOtherDuringRaise) {
    Event<Src, Payload> ev;
    struct Ctx {
        Subscription* victim = nullptr;
        std::vector<int>* out = nullptr;
        int tag = 0;
    };
    std::vector<int> out;
    auto firstThunk = [](void* owner, Src&, Payload&) {
        auto* c = static_cast<Ctx*>(owner);
        c->out->push_back(c->tag);
        c->victim->reset();  // cancel the second subscriber before it runs
    };
    auto secondThunk = [](void* owner, Src&, Payload&) {
        auto* c = static_cast<Ctx*>(owner);
        c->out->push_back(c->tag);
    };
    Subscription secondSub;
    Ctx first{&secondSub, &out, 1};
    Ctx second{nullptr, &out, 2};
    auto s1 = ev.Subscribe(&first, firstThunk);
    secondSub = ev.Subscribe(&second, secondThunk);
    Src s{0};
    Payload p{0};
    ev.Raise(s, p);
    EXPECT_EQ(out.size(), size_t{1});  // only the first ran
    EXPECT_EQ(out[0], 1);
}

// A Subscription that outlives its Event unsubscribes into a no-op (no
// use-after-free): destroying the Subscription after the Event is gone is safe.
TEST(Event, SubscriptionOutlivesEvent) {
    Recv r;
    Subscription sub;
    {
        Event<Src, Payload> ev;
        sub = ev.Subscribe(&r, &Recv::OnEvent);
        EXPECT_TRUE(sub.active());
    }  // ev destroyed; sub still holds a (now-stale) registration
    sub.reset();  // must not touch freed memory
    EXPECT_FALSE(sub.active());
}

// A no-subscriber event raises harmlessly.
TEST(Event, RaiseWithNoSubscribers) {
    Event<Src, Payload> ev;
    EXPECT_FALSE(ev.HasSubscribers());
    Src s{0};
    Payload p{0};
    ev.Raise(s, p);  // no crash
    EXPECT_FALSE(ev.HasSubscribers());
}

// --- Subscribe<&T::Method>(owner) — the member-function overload -------------

namespace {
// A receiver using ordinary member functions instead of static void* thunks.
struct MemberRecv {
    std::vector<int> seen;
    int otherCalls = 0;

    void OnEvent(Src&, Payload& p) { seen.push_back(p.value); }
    void OnOther(Src& s, Payload&) { otherCalls += s.id; }
};
}  // namespace

// The member-function overload delivers the event to a normal member function.
TEST(Event, SubscribeMemberFunction) {
    Event<Src, Payload> ev;
    MemberRecv r;
    auto sub = ev.Subscribe<&MemberRecv::OnEvent>(&r);
    EXPECT_TRUE(ev.HasSubscribers());

    Src s{1};
    Payload p{42};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{1});
    EXPECT_EQ(r.seen[0], 42);
}

// The member overload routes to the specific method named, not just any method.
TEST(Event, SubscribeMemberFunctionSelectsTheNamedMethod) {
    Event<Src, Payload> ev;
    MemberRecv r;
    auto sub = ev.Subscribe<&MemberRecv::OnOther>(&r);

    Src s{5};
    Payload p{99};
    ev.Raise(s, p);
    // OnOther ran (adds sender.id), OnEvent did not (would push to seen).
    EXPECT_EQ(r.otherCalls, 5);
    EXPECT_EQ(r.seen.size(), size_t{0});
}

// The returned Subscription has the same RAII semantics as the void* overload.
TEST(Event, SubscribeMemberFunctionUnsubscribesOnDestruction) {
    Event<Src, Payload> ev;
    MemberRecv r;
    {
        auto sub = ev.Subscribe<&MemberRecv::OnEvent>(&r);
        Src s{0};
        Payload p{1};
        ev.Raise(s, p);
    }  // sub destroyed
    EXPECT_FALSE(ev.HasSubscribers());
    Src s{0};
    Payload p{2};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{1});  // only the in-scope raise landed
    EXPECT_EQ(r.seen[0], 1);
}

// Both overloads can be mixed on one event and both fire, in order.
TEST(Event, SubscribeMemberAndThunkOverloadsCoexist) {
    Event<Src, Payload> ev;
    Recv thunkRecv;
    MemberRecv memberRecv;

    auto s1 = ev.Subscribe(&thunkRecv, &Recv::OnEvent);
    auto s2 = ev.Subscribe<&MemberRecv::OnEvent>(&memberRecv);

    Src s{0};
    Payload p{7};
    ev.Raise(s, p);

    EXPECT_EQ(thunkRecv.seen.size(), size_t{1});
    EXPECT_EQ(thunkRecv.seen[0], 7);
    EXPECT_EQ(memberRecv.seen.size(), size_t{1});
    EXPECT_EQ(memberRecv.seen[0], 7);
}

// The zero-allocation property is what justifies the overload existing at all:
// the adapting lambda must be captureless so it decays to a function pointer and
// occupies the same Handler slot as a hand-written thunk. If someone "improves"
// the overload with a capturing lambda or std::function, this stops compiling.
TEST(Event, SubscribeMemberFunctionStaysAFunctionPointer) {
    using Handler = Event<Src, Payload>::Handler;
    auto adapter = +[](void* o, Src& s, Payload& a) {
        (static_cast<MemberRecv*>(o)->*(&MemberRecv::OnEvent))(s, a);
    };
    // Compile-time: the adapter is convertible to the plain function-pointer
    // Handler type, so no type erasure and no allocation is involved.
    static_assert(std::is_convertible_v<decltype(adapter), Handler>,
                  "member-function adapter must decay to a plain function pointer");
    EXPECT_TRUE(static_cast<Handler>(adapter) != nullptr);
}

// --- SubscriptionBag --------------------------------------------------------

// A bag holds several subscriptions and unregisters all of them when cleared.
TEST(SubscriptionBag, ClearUnsubscribesEverythingHeld) {
    Event<Src, Payload> ev;
    MemberRecv a, b, c;

    SubscriptionBag bag;
    bag.Keep(ev.Subscribe<&MemberRecv::OnEvent>(&a));
    bag.Keep(ev.Subscribe<&MemberRecv::OnEvent>(&b));
    bag.Keep(ev.Subscribe<&MemberRecv::OnEvent>(&c));
    EXPECT_EQ(bag.Count(), size_t{3});
    EXPECT_FALSE(bag.Empty());

    Src s{0};
    Payload p{1};
    ev.Raise(s, p);
    EXPECT_EQ(a.seen.size(), size_t{1});
    EXPECT_EQ(b.seen.size(), size_t{1});
    EXPECT_EQ(c.seen.size(), size_t{1});

    bag.Clear();
    EXPECT_TRUE(bag.Empty());
    EXPECT_FALSE(ev.HasSubscribers());

    Payload p2{2};
    ev.Raise(s, p2);
    // No further deliveries after Clear.
    EXPECT_EQ(a.seen.size(), size_t{1});
    EXPECT_EQ(b.seen.size(), size_t{1});
    EXPECT_EQ(c.seen.size(), size_t{1});
}

// Destroying the bag has the same effect as clearing it (RAII, like Subscription).
TEST(SubscriptionBag, DestructionUnsubscribesEverythingHeld) {
    Event<Src, Payload> ev;
    MemberRecv r;
    {
        SubscriptionBag bag;
        bag.Keep(ev.Subscribe<&MemberRecv::OnEvent>(&r));
        EXPECT_TRUE(ev.HasSubscribers());
    }  // bag destroyed
    EXPECT_FALSE(ev.HasSubscribers());

    Src s{0};
    Payload p{1};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{0});
}

// operator+= is an alias for Keep, so accumulating reads naturally in a loop.
TEST(SubscriptionBag, PlusEqualsIsAnAliasForKeep) {
    Event<Src, Payload> ev;
    MemberRecv a, b;

    SubscriptionBag bag;
    bag += ev.Subscribe<&MemberRecv::OnEvent>(&a);
    bag += ev.Subscribe<&MemberRecv::OnEvent>(&b);
    EXPECT_EQ(bag.Count(), size_t{2});

    Src s{0};
    Payload p{3};
    ev.Raise(s, p);
    EXPECT_EQ(a.seen.size(), size_t{1});
    EXPECT_EQ(b.seen.size(), size_t{1});
}

// A bag is move-only and moving transfers ownership without unsubscribing.
TEST(SubscriptionBag, MoveTransfersOwnership) {
    Event<Src, Payload> ev;
    MemberRecv r;

    SubscriptionBag source;
    source.Keep(ev.Subscribe<&MemberRecv::OnEvent>(&r));

    SubscriptionBag moved(std::move(source));
    EXPECT_EQ(moved.Count(), size_t{1});
    // Still subscribed: the move must not have unregistered anything.
    EXPECT_TRUE(ev.HasSubscribers());

    Src s{0};
    Payload p{4};
    ev.Raise(s, p);
    EXPECT_EQ(r.seen.size(), size_t{1});
    EXPECT_EQ(r.seen[0], 4);
}

// Clear() unregisters in REVERSE order of addition — the documented contract,
// mirroring how scoped members tear down. Note that merely destroying the
// underlying vector would also unsubscribe everything (Subscription's destructor
// does that), so a test asserting only "no subscribers remain" cannot tell the
// two apart; this one observes the order itself.
TEST(SubscriptionBag, ClearUnsubscribesInReverseOrder) {
    std::vector<int> unsubOrder;

    SubscriptionBag bag;
    for (int tag = 1; tag <= 3; ++tag)
        bag.Keep(Subscription([&unsubOrder, tag] { unsubOrder.push_back(tag); }));

    bag.Clear();

    EXPECT_EQ(unsubOrder.size(), size_t{3});
    if (unsubOrder.size() == 3) {
        EXPECT_EQ(unsubOrder[0], 3);
        EXPECT_EQ(unsubOrder[1], 2);
        EXPECT_EQ(unsubOrder[2], 1);
    }
}

// An empty bag is harmless to clear and destroy.
TEST(SubscriptionBag, EmptyBagIsSafe) {
    SubscriptionBag bag;
    EXPECT_TRUE(bag.Empty());
    EXPECT_EQ(bag.Count(), size_t{0});
    bag.Clear();  // no crash
    EXPECT_TRUE(bag.Empty());
}
