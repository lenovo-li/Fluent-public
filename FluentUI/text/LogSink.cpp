// LogSink.cpp — thread-safe staging buffer between a data source and a text view.

#include "LogSink.h"

namespace fluent {

void LogSink::SetWakeup(WakeupFn fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    wakeup_ = std::move(fn);
}

void LogSink::EnforceCapLocked() {
    if (maxPending_ == 0 || pending_.size() <= maxPending_) return;

    // Over the cap: drop from the FRONT (oldest data), then round the cut FORWARD to just
    // past the next newline so the view never receives a line that starts mid-way
    // through. Forward rather than backward guarantees progress — rounding back could
    // land at 0, drop nothing, and leave the buffer permanently over the cap.
    size_t cut = pending_.size() - maxPending_;
    const size_t nl = pending_.find(L'\n', cut);
    // No newline at or after the cut point means the tail is one unterminated line. Cut
    // at the raw position anyway: keeping it whole would mean honouring the line boundary
    // at the cost of ignoring the cap, and the cap is the property that bounds memory.
    // The consumer sees one truncated line, which DroppedChars() accounts for.
    if (nl != std::wstring::npos) cut = nl + 1;
    if (cut > pending_.size()) cut = pending_.size();
    pending_.erase(0, cut);
    droppedChars_ += cut;
}

bool LogSink::ClaimWakeupLocked() {
    // At most one unconsumed wakeup: the slot is claimed by the first write after a
    // drain and stays claimed until Drain() re-arms it. See the header for why posting
    // per write is the bug this prevents rather than the behaviour it optimizes.
    if (wakeupPending_) return false;
    wakeupPending_ = true;
    ++wakeupCount_;
    return true;
}

void LogSink::Write(std::wstring_view text) {
    if (text.empty()) return;
    WakeupFn fire;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.append(text);
        EnforceCapLocked();
        if (ClaimWakeupLocked() && wakeup_) fire = wakeup_;
    }
    // Outside the lock, deliberately — see ClaimWakeupLocked's declaration.
    if (fire) fire();
}

void LogSink::WriteLine(std::wstring_view line) {
    WakeupFn fire;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.append(line);
        // Terminate the line if the producer did not. Appended BEFORE the cap is
        // enforced, so the cap is judged against the real final size and the trim's
        // "round forward to a newline" sees this line's terminator.
        if (line.empty() || line.back() != L'\n') pending_.push_back(L'\n');
        EnforceCapLocked();
        if (ClaimWakeupLocked() && wakeup_) fire = wakeup_;
    }
    if (fire) fire();
}

bool LogSink::Drain(std::wstring& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();
    // Swap rather than copy: the staged text can be megabytes after a stall, and the
    // consumer's buffer capacity comes back here for the next batch to fill.
    out.swap(pending_);
    // Re-arm INSIDE the lock, atomically with taking the data. A producer that appended
    // between the swap and the re-arm would otherwise see wakeupPending_ still true, skip
    // its wakeup, and leave its data staged with nobody told about it — a log that stops
    // updating until some unrelated later write happens to arrive. Under one lock the
    // only two orderings are both correct: append-then-swap puts the data in this batch,
    // swap-then-append claims a fresh wakeup for the next one.
    wakeupPending_ = false;
    return !out.empty();
}

size_t LogSink::PendingChars() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

bool LogSink::WakeupPending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wakeupPending_;
}

size_t LogSink::WakeupCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wakeupCount_;
}

size_t LogSink::DroppedChars() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return droppedChars_;
}

void LogSink::SetMaxPending(size_t chars) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxPending_ = chars;
    // Lowering the cap applies to what is already staged, for the same reason
    // TextArea::SetMaxLines trims immediately: a caller capping an already-oversized
    // buffer is capping it to release the memory now, not eventually.
    //
    // No wakeup here even if data is pending. A trim only ever REMOVES characters, and
    // whatever put them there already claimed a wakeup — firing another would be a second
    // unconsumed wakeup for one batch, which is the exact invariant this class exists to
    // hold.
    EnforceCapLocked();
}

size_t LogSink::MaxPending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return maxPending_;
}

} // namespace fluent
