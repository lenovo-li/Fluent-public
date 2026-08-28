// WindowServicesSubscriptionTests.cpp — unit tests for the generation-guarded
// Subscription registration pattern (phase B3).
//
// A stale Subscription (whose slot was replaced by a newer Register*/Set* call)
// must NOT clear the newer registration when it is destroyed. This mock
// implements WindowServices with the same generation-counter logic NativeWindowHost
// uses, so the pattern can be verified without a real HWND / device stack.
//
// The mock returns references to default-constructed (uninitialized) D2D/DWrite
// contexts; these tests never touch them — only the popup registration slots.

#include "../framework/Test.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"

using namespace fluent;

namespace {

// Minimal WindowServices with NativeWindowHost's generation-guarded registration.
class MockHost : public WindowServices {
public:
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }

    // Composition layer (Plan B): not exercised by these popup-registration tests.
    // No backend → a control would fall back to its UI-thread path.
    ICompositionBackend* Composition() override { return nullptr; }

    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)> cb) override {
        dismiss_ = std::move(cb);
        unsigned gen = ++dismissGen_;
        return Subscription([this, gen] {
            if (dismissGen_ == gen) { dismiss_ = nullptr; ++dismissGen_; }
        });
    }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)> cb) override {
        key_ = std::move(cb);
        unsigned gen = ++keyGen_;
        return Subscription([this, gen] {
            if (keyGen_ == gen) { key_ = nullptr; ++keyGen_; }
        });
    }

    // Test observers.
    bool HasDismiss() const { return static_cast<bool>(dismiss_); }
    bool HasKey() const { return static_cast<bool>(key_); }

private:
    std::function<bool(PopupDismissReason, HWND, int, int)> dismiss_;
    std::function<bool(UINT)> key_;
    unsigned dismissGen_ = 0;
    unsigned keyGen_ = 0;
    D2DContext d2d_;      // uninitialized; never used by these tests
    DWriteContext dwrite_;
};

}  // namespace

// A live Subscription clears its slot on destruction.
TEST(WindowServicesSub, SubscriptionClearsSlotOnDestroy) {
    MockHost host;
    {
        Subscription s = host.RegisterActivePopupDismiss(
            [](PopupDismissReason, HWND, int, int) { return true; });
        EXPECT_TRUE(host.HasDismiss());
    }
    EXPECT_FALSE(host.HasDismiss());  // destructor cleared it
}

// A stale Subscription must NOT clear a newer registration that replaced it.
TEST(WindowServicesSub, StaleSubscriptionDoesNotClearNewerRegistration) {
    MockHost host;
    Subscription first = host.RegisterActivePopupDismiss(
        [](PopupDismissReason, HWND, int, int) { return true; });

    // A second popup takes over the slot (e.g. a different control opened).
    Subscription second = host.RegisterActivePopupDismiss(
        [](PopupDismissReason, HWND, int, int) { return false; });
    EXPECT_TRUE(host.HasDismiss());

    // Destroying the FIRST (now stale) subscription must leave `second` intact.
    first.reset();
    EXPECT_TRUE(host.HasDismiss());  // newer registration survived

    // Destroying the current owner does clear it.
    second.reset();
    EXPECT_FALSE(host.HasDismiss());
}

// The key-handler slot has independent generation tracking.
TEST(WindowServicesSub, KeyHandlerSlotIsIndependent) {
    MockHost host;
    Subscription k = host.RegisterActivePopupKeyHandler([](UINT) { return true; });
    Subscription d = host.RegisterActivePopupDismiss(
        [](PopupDismissReason, HWND, int, int) { return true; });
    EXPECT_TRUE(host.HasKey());
    EXPECT_TRUE(host.HasDismiss());

    k.reset();
    EXPECT_FALSE(host.HasKey());
    EXPECT_TRUE(host.HasDismiss());  // unaffected
}

// A control that owns its Subscription as a member auto-unregisters when it is
// destroyed while still open — the core anti-dangling guarantee of B4. This
// mirrors ~ComboBox / ~MenuFlyout resetting their subscription members.
TEST(WindowServicesSub, MemberSubscriptionUnregistersOnOwnerDestroy) {
    MockHost host;
    struct FakeControl {
        Subscription sub;
        explicit FakeControl(MockHost& h) {
            sub = h.RegisterActivePopupKeyHandler([](UINT) { return true; });
        }
    };
    {
        FakeControl ctrl(host);
        EXPECT_TRUE(host.HasKey());
    }  // ctrl destroyed with its popup "open"
    EXPECT_FALSE(host.HasKey());  // member Subscription cleared the slot
}
