// FrameRing.h — Fixed-size ring buffer of per-frame CPU timings for P95/P99
// latency export (roadmap §18.3, WP-07).
//
// WP-00's PerformanceCounters.h noted: "The host may keep a small ring of
// recent frames for P95/P99 export (WP-07)." This is that ring.
//
// Design:
//   * Template parameter N is the ring capacity (caller picks a window size;
//     NativeWindowHost uses N=120 ≈ 2 seconds at 60fps).
//   * Push() overwrites the oldest sample once the ring is full so storage is
//     always bounded and Push() is O(1).
//   * Percentile() runs a partial sort on a stack-copy of the live samples —
//     O(n) with std::nth_element — so it is suitable for a HUD that samples
//     once per second, not something called every frame.
//   * No allocation: all storage is inline in the struct.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace fluent {

template <size_t N>
class FrameRing {
    static_assert(N >= 2, "FrameRing needs at least 2 slots");
public:
    // Add a new frame sample (milliseconds). Overwrites the oldest entry when
    // the ring is full.
    void Push(float cpuMs) {
        samples_[head_] = cpuMs;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    // Number of samples currently in the ring (0 → N).
    size_t Count() const { return count_; }

    // Reset to empty.
    void Clear() { head_ = 0; count_ = 0; }

    // Return the p-th percentile of the current samples, where p is in [0, 100].
    // Returns 0 if the ring is empty. Uses a partial sort on a stack copy, so
    // this is O(n) and should not be called every frame.
    float Percentile(float p) const {
        if (count_ == 0) return 0.0f;
        // Copy live samples to a local array.
        std::array<float, N> buf;
        for (size_t i = 0; i < count_; ++i)
            buf[i] = samples_[i];
        // Clamp p to [0, 100].
        if (p <= 0.0f) p = 0.0f;
        if (p >= 100.0f) p = 100.0f;
        // Map to a 0-based rank index.
        size_t rank = static_cast<size_t>(p / 100.0f * static_cast<float>(count_ - 1) + 0.5f);
        if (rank >= count_) rank = count_ - 1;
        auto first = buf.begin();
        auto last  = first + static_cast<ptrdiff_t>(count_);
        auto nth   = first + static_cast<ptrdiff_t>(rank);
        std::nth_element(first, nth, last);
        return buf[rank];
    }

    float P50() const { return Percentile(50.0f); }
    float P95() const { return Percentile(95.0f); }
    float P99() const { return Percentile(99.0f); }

    // Maximum sample in the ring. O(n). Returns 0 if empty.
    float Max() const {
        if (count_ == 0) return 0.0f;
        float m = samples_[0];
        for (size_t i = 1; i < count_; ++i)
            if (samples_[i] > m) m = samples_[i];
        return m;
    }

private:
    std::array<float, N> samples_ = {};
    size_t head_ = 0;   // next write position
    size_t count_ = 0;  // number of valid samples (≤ N)
};

} // namespace fluent
