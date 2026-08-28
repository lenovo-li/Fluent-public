// Subscription.h — RAII handle for a registered callback (roadmap §10.3).
//
// A registrar (e.g. WindowServices' active-popup dismiss / key hooks, or any
// future event source) hands back a Subscription that owns the un-registration.
// When the Subscription is destroyed or reset, the callback is removed — so a
// control storing one as a member automatically unregisters on destruction,
// eliminating dangling `[this]` callbacks after the owner is gone.
//
// Move-only: exactly one Subscription owns a given registration at a time.
// Header-only; no Windows or COM dependency (pure C++), so it is unit-testable
// on its own.
#pragma once

#include <functional>
#include <utility>

namespace fluent {

class Subscription {
public:
    // An empty subscription owns nothing; destroying/resetting it is a no-op.
    Subscription() = default;

    // Take ownership of an un-registration action. Invoked exactly once, on the
    // first of reset()/destruction (guarded so it never fires twice).
    explicit Subscription(std::function<void()> unregister)
        : unregister_(std::move(unregister)) {}

    ~Subscription() { reset(); }

    // Move transfers ownership; the moved-from subscription becomes empty.
    Subscription(Subscription&& other) noexcept
        : unregister_(std::move(other.unregister_)) {
        other.unregister_ = nullptr;
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();  // release any registration we currently hold
            unregister_ = std::move(other.unregister_);
            other.unregister_ = nullptr;
        }
        return *this;
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    // Unregister now (idempotent). Safe on an empty subscription.
    void reset() {
        if (unregister_) {
            auto fn = std::move(unregister_);
            unregister_ = nullptr;  // clear before invoking (reentrancy-safe)
            fn();
        }
    }

    // True while this subscription still owns a live registration.
    bool active() const noexcept { return static_cast<bool>(unregister_); }

private:
    std::function<void()> unregister_;
};

} // namespace fluent
