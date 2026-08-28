// WindowRegistry.h — the Application's window bookkeeping, extracted as a pure
// unit so it can be tested without an HWND or a message pump.
//
// FLUENTUI_INTERNAL — implementation detail of Application. Installed because
// Application.h includes it (for the ShutdownMode enum and the member type), not
// because applications should use it. ShutdownMode itself IS public API; the
// registry template around it is not. See docs/api-surface.md.
//
// WHY THIS IS A SEPARATE TYPE. Application's bookkeeping (which windows exist,
// which of them are primary, and whether the shutdown condition is met) is pure
// data manipulation, but it used to live inline in Application next to the pump.
// That made it untestable: AttachWindow bails on !window.IsOpen(), so a headless
// test could never get a window into the container, and the whole ShutdownMode
// decision was reachable only by running a real message loop with real HWNDs.
//
// This follows the same pattern as PlanRedraw / EnsureVisibleOffset /
// FramePacing / SurfaceTransformFromWindowDip: the judgment is a free-standing
// unit, the I/O stays with the caller. Application keeps the parts that
// genuinely need Win32 (IsOpen / IsMainWindow probes, PostMessage, the pump) and
// delegates every decision here.
//
// The registry NEVER DEREFERENCES the stored pointer. It treats each window as
// an opaque identity, which is precisely what lets a test register arbitrary
// non-null addresses. The caller is responsible for asking the window whether it
// is open and whether it is primary, and for passing those answers in.
//
// GENERATIONS. Each registration gets a monotonically increasing generation. The
// pump snapshots the window list, then calls out to each window — and a callee
// can close a window, or close one and open another that lands at the same
// vector index or even the same heap address. Comparing the pointer alone would
// then let the pump touch a *different* window than the one it snapshotted.
// Contains() therefore compares (pointer, generation) as a pair. This is the
// same stale-handle guard the popup-dismiss slots use in NativeWindowHost.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fluent {

// Controls when the pump stops because of window closures. Mirrors WPF's
// ShutdownMode. See Application.h for the full rationale.
enum class ShutdownMode {
    OnMainWindowClose,   // stop when the last primary window closes
    OnLastWindowClose,   // stop when every registered window closes
    OnExplicitShutdown,  // never stop on window count; only Shutdown()/WM_QUIT
};

// One registered window. `window` is an opaque identity — the registry never
// dereferences it. `isMain` distinguishes a primary window (Window) from a
// secondary one (DialogWindow) and is supplied by the caller at registration
// time, because only the caller can perform the virtual call.
template <class WindowT>
struct WindowEntry {
    WindowT* window = nullptr;
    uint64_t generation = 0;
    bool isMain = false;

    friend bool operator==(const WindowEntry& a, const WindowEntry& b) {
        return a.window == b.window && a.generation == b.generation;
    }
};

// Pure bookkeeping over opaque window identities. Not thread-safe: it is only
// ever touched on the UI thread, which Application enforces before delegating.
template <class WindowT>
class WindowRegistryT {
public:
    using Entry = WindowEntry<WindowT>;

    // Register `window`. `isMain` must be the caller's answer to "is this a
    // primary window". Returns the assigned generation, or 0 if `window` was
    // already registered or null (0 is never a valid generation, so it doubles
    // as the failure signal). Re-registering an existing window is not an error
    // — Application treats it as success — but it does not renew the generation,
    // because outstanding snapshots must keep matching.
    uint64_t Add(WindowT* window, bool isMain) {
        if (!window) return 0;
        if (IndexOf(window) != kNotFound) return 0;
        const uint64_t generation = ++nextGeneration_;
        entries_.push_back(Entry{window, generation, isMain});
        if (isMain) ++mainCount_;
        return generation;
    }

    // Unregister `window`. Returns true if it was present. Keeps mainCount_ in
    // step with the isMain flag recorded at Add() time rather than re-asking the
    // window: by the time a window detaches it is mid-destruction, and a virtual
    // call on it would be answered by an already-sliced object.
    bool Remove(WindowT* window) {
        const size_t index = IndexOf(window);
        if (index == kNotFound) return false;
        if (entries_[index].isMain && mainCount_ > 0) --mainCount_;
        entries_.erase(entries_.begin() + static_cast<ptrdiff_t>(index));
        return true;
    }

    // True if this exact (pointer, generation) pair is still registered. The
    // generation is what makes this a liveness check rather than an
    // address-reuse check — see the header comment.
    bool Contains(const Entry& candidate) const {
        for (const Entry& entry : entries_)
            if (entry == candidate) return true;
        return false;
    }

    // True when `mode`'s stop condition has been reached through closures alone.
    // Explicit Shutdown() and WM_QUIT are handled by the caller and deliberately
    // not represented here — this answers only "have the windows run out".
    bool ShouldStop(ShutdownMode mode) const {
        switch (mode) {
        case ShutdownMode::OnMainWindowClose:  return mainCount_ == 0;
        case ShutdownMode::OnLastWindowClose:  return entries_.empty();
        case ShutdownMode::OnExplicitShutdown: return false;
        }
        return false;
    }

    // A copy of the current entries, for the pump to iterate while calling out
    // into window code that may register or unregister windows. Iterating
    // entries_ directly would invalidate the iterator on the first such call.
    // Every use of a snapshot entry must be guarded by Contains().
    std::vector<Entry> Snapshot() const { return entries_; }

    const std::vector<Entry>& Entries() const { return entries_; }
    size_t Count() const { return entries_.size(); }
    size_t MainCount() const { return mainCount_; }
    bool Empty() const { return entries_.empty(); }

private:
    static constexpr size_t kNotFound = static_cast<size_t>(-1);

    size_t IndexOf(const WindowT* window) const {
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].window == window) return i;
        return kNotFound;
    }

    std::vector<Entry> entries_;
    uint64_t nextGeneration_ = 0;
    size_t mainCount_ = 0;
};

class NativeWindowHost;
using WindowRegistry = WindowRegistryT<NativeWindowHost>;

} // namespace fluent
