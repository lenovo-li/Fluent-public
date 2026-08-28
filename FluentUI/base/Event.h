// Event.h — the FluentUI typed event primitive (roadmap §10).
//
// A minimal multicast event with a *function-pointer* handler slot instead of
// std::function, so subscribing costs no heap allocation (only the slots_ vector
// grows). A handler is `void(*)(void* owner, Sender&, Args&)`: the owner pointer
// is the receiver (usually the subscribing object) that the static thunk casts
// back to its concrete type. This is the "zero-allocation typed event" the
// roadmap prescribes for control notifications (Click/Checked/ValueChanged/...).
//
// Lifetime safety (both directions):
//   * Subscriber destroyed first — Subscribe returns a Subscription (RAII); its
//     destruction marks the slot dead by generation, so a later Raise skips it.
//     The generation guards against a stale Subscription clobbering a *different*
//     handler that reused the same vector index.
//   * Event destroyed first — the Subscription must not then touch freed memory.
//     Each Event owns one lazily-created liveness token (shared_ptr<bool>); the
//     Subscription's unregister closure holds a weak_ptr and no-ops if the token
//     has expired. This one small allocation per event-with-subscribers is the
//     single concession to the otherwise allocation-free design, and it is what
//     makes "raise after the sender is gone" safe (a real case: a control's
//     Event outlived by a Subscription the app still holds).
//
// Reentrancy: a handler may Subscribe or cancel (itself or others) during Raise.
// Cancellation only flips a slot's `live` flag (indices stay stable); compaction
// of dead slots is deferred until the outermost Raise returns.
#pragma once

#include "../core/Subscription.h"
#include <memory>
#include <vector>

namespace fluent {

template <class Sender, class Args>
class Event {
public:
    using Handler = void (*)(void* owner, Sender& sender, Args& args);

    Event() = default;
    // Non-copyable, non-movable: subscriptions capture `this`, so the address
    // must be stable for the event's whole life. Controls hold events by value as
    // members and never move them after subscription.
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    // Register `handler`, invoked with `owner` as its first argument on each
    // Raise. The returned Subscription owns the un-registration: destroy or reset
    // it to stop receiving (RAII — a control stores it as a member so it detaches
    // automatically when destroyed). Handlers fire in subscription order.
    //
    // **YOU MUST SAVE THE RETURNED Subscription** — discarding it immediately
    // unregisters the handler. Store it as a member, or push into a vector if you
    // have multiple subscriptions on the same page/dialog.
    [[nodiscard]] Subscription Subscribe(void* owner, Handler handler) {
        if (!alive_) alive_ = std::make_shared<bool>(true);
        unsigned gen = ++nextGen_;
        slots_.push_back(Slot{owner, handler, gen, true});
        std::weak_ptr<bool> weak = alive_;
        return Subscription([this, gen, weak] {
            // If the event is already gone, there is nothing to clear (and `this`
            // dangles) — the weak token guards that.
            if (weak.expired()) return;
            for (Slot& s : slots_)
                if (s.gen == gen && s.live) { s.live = false; break; }
        });
    }

    // Subscribe an ordinary non-static MEMBER FUNCTION of `owner`.
    //
    //     saveSub_ = save->Click().Subscribe<&SettingsDialog::OnSave>(this);
    //     void SettingsDialog::OnSave(Button&, RoutedEventArgs&) { ... }
    //
    // This is pure sugar over the overload above and changes nothing about the
    // design: the lambda below is captureless, so it converts to a plain function
    // pointer and lands in the same `Handler` slot. STILL ZERO ALLOCATION — no
    // std::function, no type erasure, no vtable. The three-part ceremony each
    // subscription used to need (a static thunk, a static_cast inside it, and the
    // Subscription member) collapses to one line, and the handler becomes a normal
    // member function that can touch members directly.
    //
    // Deliberately NOT provided: an overload taking a capturing lambda. That
    // would require heap-allocating the closure and would quietly break the
    // zero-allocation property this header is built around. If you need captured
    // state, put it on the owner object — that is what `owner` is for.
    template <auto Method, class T>
    [[nodiscard]] Subscription Subscribe(T* owner) {
        return Subscribe(owner, +[](void* o, Sender& s, Args& a) {
            (static_cast<T*>(o)->*Method)(s, a);
        });
    }

    // Invoke every live handler with (sender, args). Safe to call with no
    // subscribers. Handlers may cancel or add subscriptions during the call.
    void Raise(Sender& sender, Args& args) {
        ++raising_;
        // Index-based loop re-reading size(): a handler that Subscribes mid-raise
        // appends a slot, which this loop will then also invoke (matches the "new
        // subscriber sees subsequent raises" intent and never reads freed memory
        // because we re-fetch the reference each turn).
        for (size_t i = 0; i < slots_.size(); ++i) {
            Slot s = slots_[i];  // copy the fields we need before calling out
            if (s.live && s.fn) s.fn(s.owner, sender, args);
        }
        if (--raising_ == 0) Compact();
    }

    // True if at least one live handler is registered. Cheap; used by hosts to
    // skip building args when nothing is listening.
    bool HasSubscribers() const {
        for (const Slot& s : slots_)
            if (s.live) return true;
        return false;
    }

    ~Event() {
        // Mark the event dead so any surviving Subscription's un-registration is a
        // no-op instead of touching this freed object.
        if (alive_) *alive_ = false;
    }

private:
    struct Slot {
        void* owner;
        Handler fn;
        unsigned gen;
        bool live;
    };

    // Drop dead slots once no Raise is in flight (indices are no longer observed).
    void Compact() {
        size_t w = 0;
        for (size_t r = 0; r < slots_.size(); ++r)
            if (slots_[r].live) slots_[w++] = slots_[r];
        slots_.resize(w);
    }

    std::vector<Slot> slots_;
    unsigned nextGen_ = 0;
    int raising_ = 0;
    // Liveness token shared with outstanding Subscriptions (created lazily on the
    // first Subscribe). Set to false in the destructor so a Subscription outliving
    // the event unregisters into a no-op.
    std::shared_ptr<bool> alive_;
};

// SubscriptionBag — holds any number of Subscriptions with one member.
//
// A Subscription must outlive the subscription it owns, so every subscribing
// object needs somewhere to park one. With a fixed set of events that means one
// named member each; with a dynamic set (a dialog building N buttons in a loop)
// it means hand-rolling a container. ContentDialog carried exactly that:
// `std::vector<Subscription> buttonSubs_`, cleared by hand before rebuilding.
//
// This is that container, named, with the clear-before-rebuild step made
// explicit. Teardown order is unchanged: destroying the bag destroys each
// Subscription, which unregisters it — same RAII guarantee, just aggregated.
//
// Move-only for the same reason Subscription is: exactly one owner at a time.
class SubscriptionBag {
public:
    SubscriptionBag() = default;
    SubscriptionBag(SubscriptionBag&&) noexcept = default;
    SubscriptionBag& operator=(SubscriptionBag&&) noexcept = default;
    SubscriptionBag(const SubscriptionBag&) = delete;
    SubscriptionBag& operator=(const SubscriptionBag&) = delete;

    // Take ownership of `sub`. Returns *this so calls can chain.
    SubscriptionBag& Keep(Subscription sub) {
        subs_.push_back(std::move(sub));
        return *this;
    }

    SubscriptionBag& operator+=(Subscription sub) { return Keep(std::move(sub)); }

    // Unregister everything held, in reverse order of addition — the mirror of
    // construction order, matching how scoped members would tear down.
    void Clear() {
        for (auto it = subs_.rbegin(); it != subs_.rend(); ++it)
            it->reset();
        subs_.clear();
    }

    size_t Count() const { return subs_.size(); }
    bool Empty() const { return subs_.empty(); }

private:
    std::vector<Subscription> subs_;
};

}  // namespace fluent
