// LogSink.h — thread-safe staging buffer between a data source and a text view.
//
// The producer half of the high-throughput log path (§2.3). A hardware acquisition
// thread, a serial reader, a subprocess pipe — none of them run on the UI thread,
// and none of them may touch a control's text buffer, because single-threaded UI is
// an architectural premise of this framework, not a convention to be careful around.
// This is the queue in between: producers Write() from any thread, the UI thread
// Drain()s and hands the result to TextArea::AppendText.
//
// WHY THIS IS NOT PART OF TextArea. The control has no business knowing where its
// text comes from. Keeping the threading here means TextArea's contract stays
// "UI thread only", which is the same contract every other control has, and it means
// this class can be tested with no window, no DWrite and no message pump at all.
//
// THE THREE THINGS THAT MAKE THIS NOT-TRIVIAL, each of which is a bug this design
// avoids rather than a feature it adds:
//
// 1. AT MOST ONE UNCONSUMED WAKEUP. The wakeup fires on the first Write after a
//    Drain and not again until that Drain happens. Posting per Write would queue one
//    message per LINE — at ten thousand lines a second, the UI thread spends its time
//    dispatching wakeups for work it already did, and the queue backlog outlives the
//    burst that caused it. This is precisely the bug ModalFrameHeartbeat was fixed
//    for (see its worker loop), and the reasoning transfers without change: a wakeup
//    already pending has not been serviced yet, so it will do the work this one would
//    have. Dropping it is free.
//
// 2. ONE STRING, NOT A VECTOR OF LINES. Lines are concatenated into a single buffer
//    with '\n' separators, so Drain is a swap and the consumer makes exactly one
//    AppendText call per batch. A vector<wstring> would allocate per line and turn one
//    O(added) append into N appends, each of which re-syncs the scroll extent and
//    invalidates the last line's layout. The batch is the unit that matters.
//
// 3. A BOUND ON PENDING DATA. If the UI thread stalls (a resize drag, a modal loop)
//    while a producer keeps writing, an unbounded staging buffer grows until the stall
//    ends — the exact opposite of what a ring-buffered log view is for. SetMaxPending
//    caps it and drops the OLDEST pending characters, because for a log the newest data
//    is the interesting data and the old lines were headed for TextArea::SetMaxLines'
//    trim anyway. Drops are COUNTED (DroppedChars) rather than silent: data loss the
//    caller cannot detect is worse than data loss it can report.
//
// THE WAKEUP IS INJECTED, not built in. This class calls no Win32 at all — the caller
// supplies a callable that gets the UI thread's attention, which in an app is
// `Application::Post` or a PostMessage to a window, and in a test is a lambda that
// sets a flag. That is what makes the "at most one unconsumed wakeup" rule above
// testable, and it keeps the one genuinely platform-dependent decision (how do I wake
// the UI thread) with the code that already knows the answer.
//
// LOCK CHOICE: a plain mutex, not a lock-free queue. The critical section is one
// string append; a lock-free ring would be more code, more subtle, and would still be
// dwarfed by the DWrite work the consumer does with the result. The repo's rule
// ("prefer a free function over a class, a value over a heap allocation") applies to
// synchronization primitives too — the simplest correct thing until measurement says
// otherwise.
#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace fluent {

class LogSink {
public:
    // Called (on the PRODUCER's thread) when the first data after a drain arrives, to
    // ask the UI thread to come and Drain(). Must be safe to invoke from any thread —
    // Application::Post and PostMessage both are.
    using WakeupFn = std::function<void()>;

    LogSink() = default;
    LogSink(const LogSink&) = delete;
    LogSink& operator=(const LogSink&) = delete;

    // Install the wakeup. Call once during setup, before any producer starts. Passing an
    // empty function is legal and means "no wakeup" — a consumer that polls (a test, or a
    // frame loop that drains unconditionally) needs none, and the pending flag below is
    // still maintained so the same test can assert on it.
    void SetWakeup(WakeupFn fn);

    // Append one chunk from a producer thread. A trailing newline is added if `line`
    // does not already end with one, so a producer can write "foo" or "foo\n"
    // interchangeably — every real log source does one or the other and neither should
    // need to know which this class prefers.
    //
    // A chunk that hits the pending cap is still accepted; the OLDEST staged data is what
    // gets dropped (see SetMaxPending), and DroppedChars() reports how much.
    void WriteLine(std::wstring_view line);
    // Append raw text with no newline handling. For a producer that already has a
    // correctly terminated block (a whole pipe read).
    void Write(std::wstring_view text);

    // Move everything pending into `out` (which is CLEARED first) and re-arm the wakeup.
    // Returns true if `out` is non-empty. UI thread only.
    //
    // `out` is a caller-owned buffer so a consumer draining every frame can reuse one
    // allocation forever: the swap hands the sink `out`'s old capacity to fill next time.
    bool Drain(std::wstring& out);

    // Characters currently staged. Diagnostic — and the observable for "the producer's
    // data has not reached the control yet", which is otherwise invisible.
    size_t PendingChars() const;
    // Is a wakeup outstanding (fired but not yet drained)? This is the state the
    // "at most one unconsumed wakeup" rule is about, exposed so that rule is testable.
    bool WakeupPending() const;
    // How many wakeups have been fired in total. A burst of N writes drained once must
    // produce exactly ONE — a count of N means the coalescing broke.
    size_t WakeupCount() const;
    // Characters discarded because the cap was hit. Non-zero means the consumer could not
    // keep up and the log has gaps; a caller that cares should surface it.
    size_t DroppedChars() const;

    // Cap the pending buffer (characters). 0 = unbounded (the default). When exceeded,
    // the OLDEST pending characters are dropped, trimmed at a line boundary so a partial
    // line never reaches the view.
    void SetMaxPending(size_t chars);
    size_t MaxPending() const;

private:
    // Drop oldest pending characters until the buffer is within maxPending_. Caller holds
    // the lock. No-op when uncapped or already under.
    void EnforceCapLocked();

    // Claim the single wakeup slot; returns true if THIS call claimed it (i.e. the caller
    // should fire the wakeup) and false if one was already outstanding. Caller holds the
    // lock.
    //
    // WHY THE WAKEUP IS FIRED BY THE CALLER, AFTER UNLOCKING, rather than here: it is
    // caller-supplied code running on the producer's thread, and invoking it under our
    // lock would order any lock IT takes against ours — a lock-order inversion that
    // deadlocks the day someone wires the wakeup to something that itself synchronizes.
    // Nothing in this class needs the lock held while it runs.
    bool ClaimWakeupLocked();

    mutable std::mutex mutex_;
    std::wstring pending_;
    WakeupFn wakeup_;
    // Fired but not yet drained. Written on producer threads and read on the UI thread,
    // always under mutex_, so it needs no atomicity of its own.
    bool wakeupPending_ = false;
    size_t wakeupCount_ = 0;
    size_t droppedChars_ = 0;
    size_t maxPending_ = 0;
};

} // namespace fluent
