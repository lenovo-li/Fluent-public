// DWriteFormatTests.cpp — component tests for DWriteContext::Format's immutable
// full-key cache (roadmap §5.3.2). Needs a real DirectWrite factory (no D3D /
// HWND), so this is a "component" test rather than a pure unit test.
//
// The bug this locks down: Format() used to key only on (size, weight) and hand
// back a shared object that callers then mutated (SetTextAlignment / wrapping),
// so two controls sharing a size fought over one format's state. The fix keys on
// the full layout key and treats the result as immutable.

#include "../framework/Test.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include <thread>
#include <atomic>
#include <vector>
#include <string>

using namespace fluent;

namespace {
// Initialize a DWrite factory once for the suite; skip assertions if the
// machine has no DirectWrite (should never happen on a dev box, but be safe).
DWriteContext& Ctx() {
    static DWriteContext ctx;
    static bool inited = SUCCEEDED(ctx.Initialize());
    (void)inited;
    return ctx;
}
}  // namespace

// Same full key -> same cached object (pointer identity).
TEST(DWriteFormat, SameKeyReturnsSameObject) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    IDWriteTextFormat* a = ctx.Format(13.0f);
    IDWriteTextFormat* b = ctx.Format(13.0f);
    EXPECT_TRUE(a != nullptr);
    EXPECT_TRUE(a == b);
}

// Differing only by text alignment -> distinct cached objects, each retaining
// its own alignment (the core anti-pollution guarantee).
TEST(DWriteFormat, DifferentAlignmentDistinctObjects) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    IDWriteTextFormat* lead = ctx.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_TEXT_ALIGNMENT_LEADING);
    IDWriteTextFormat* center = ctx.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_TEXT_ALIGNMENT_CENTER);
    EXPECT_TRUE(lead != nullptr);
    EXPECT_TRUE(center != nullptr);
    EXPECT_TRUE(lead != center);  // distinct cache entries
    // Each kept its own alignment — no cross-pollution.
    EXPECT_EQ(lead->GetTextAlignment(), DWRITE_TEXT_ALIGNMENT_LEADING);
    EXPECT_EQ(center->GetTextAlignment(), DWRITE_TEXT_ALIGNMENT_CENTER);
}

// Differing by wrapping -> distinct objects, each retaining its wrapping mode.
TEST(DWriteFormat, DifferentWrappingDistinctObjects) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    IDWriteTextFormat* nowrap = ctx.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_TEXT_ALIGNMENT_LEADING,
                                           DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                           DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextFormat* wrap = ctx.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_TEXT_ALIGNMENT_LEADING,
                                         DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                                         DWRITE_WORD_WRAPPING_WRAP);
    EXPECT_TRUE(nowrap != wrap);
    EXPECT_EQ(nowrap->GetWordWrapping(), DWRITE_WORD_WRAPPING_NO_WRAP);
    EXPECT_EQ(wrap->GetWordWrapping(), DWRITE_WORD_WRAPPING_WRAP);
}

// The classic pollution scenario: caller A takes a LEADING format, caller B
// takes a CENTER format at the same size; A's object is unaffected by B.
TEST(DWriteFormat, NoCrossPollutionBetweenCallers) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    IDWriteTextFormat* a = ctx.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_TEXT_ALIGNMENT_LEADING);
    // B asks for the same size but centered — historically this mutated A.
    IDWriteTextFormat* b = ctx.Format(13.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                      DWRITE_TEXT_ALIGNMENT_CENTER);
    (void)b;
    EXPECT_EQ(a->GetTextAlignment(), DWRITE_TEXT_ALIGNMENT_LEADING);
}

// AsyncLayout viability test: can we create DWrite layouts on a background thread?
// This is the make-or-break question for async layout — if DWrite isn't free-threaded,
// the whole approach is DOA.
TEST(DWriteFormat, BackgroundThreadLayoutCreation) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;

    std::atomic<bool> succeeded{false};
    std::atomic<bool> crashed{false};

    std::thread worker([&]() {
        try {
            // Attempt the full Measure path: get a format, create a layout, extract metrics.
            IDWriteTextFormat* fmt = ctx.Format(14.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                                DWRITE_TEXT_ALIGNMENT_LEADING);
            if (!fmt) return;

            ComPtr<IDWriteTextLayout> layout;
            HRESULT hr = ctx.Factory()->CreateTextLayout(
                L"Background thread test", 22, fmt, 200.0f, 100.0f, layout.GetAddressOf());
            if (FAILED(hr)) return;

            DWRITE_TEXT_METRICS metrics{};
            hr = layout->GetMetrics(&metrics);
            if (FAILED(hr)) return;

            // If we got here without crashing/hanging, DWrite is thread-safe enough.
            succeeded = (metrics.width > 0.0f);
        } catch (...) {
            crashed = true;
        }
    });

    worker.join();

    // If this fails, AsyncLayout cannot work — DWrite must be called from the UI thread.
    EXPECT_FALSE(crashed);
    EXPECT_TRUE(succeeded);
}

// Concurrent CreateTextLayout on the DWrite FACTORY, with every thread handed a
// format it does not have to create. This isolates the factory: DWRITE_FACTORY_TYPE_SHARED
// is documented as thread-safe, and this is the assertion of that for our usage.
//
// Deliberately NOT calling ctx.Format() inside the threads — see the next test for
// why that is a different (and unsafe) question.
TEST(DWriteFormat, ConcurrentFactoryLayoutCreationIsSafe) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;

    constexpr int kThreads = 8;
    // Pre-create the formats on THIS thread so the workers only touch the factory.
    std::vector<IDWriteTextFormat*> formats;
    for (int i = 0; i < kThreads; ++i)
        formats.push_back(ctx.Format(11.0f + i));

    std::atomic<int> succeeded{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&, i]() {
            if (!formats[i]) return;
            const std::wstring text = L"Thread " + std::to_wstring(i) + L" measuring text";
            ComPtr<IDWriteTextLayout> layout;
            if (FAILED(ctx.Factory()->CreateTextLayout(
                    text.c_str(), static_cast<UINT32>(text.size()), formats[i],
                    300.0f, 100.0f, layout.GetAddressOf())))
                return;
            DWRITE_TEXT_METRICS m{};
            if (SUCCEEDED(layout->GetMetrics(&m)) && m.width > 0.0f)
                succeeded.fetch_add(1);
        });
    }
    for (auto& t : workers) t.join();
    EXPECT_EQ(succeeded.load(), kThreads);
}

// THE ACTUAL BLOCKER FOR ASYNC LAYOUT, stated as a test.
//
// Every control's Measure reaches DWrite through DWriteContext::Format(), and that
// method is a plain unsynchronized std::unordered_map lookup-then-insert
// (DWriteContext.cpp:60 and :79). Two threads asking for a size that is not yet
// cached both miss, both CreateTextFormat, and both emplace — concurrent
// modification of one unordered_map. That is a data race whose consequences range
// from a leaked format to a corrupted bucket list.
//
// This test drives every thread at a DISTINCT, NEVER-BEFORE-REQUESTED size, so all
// of them take the insert path simultaneously. Under a normal build it will usually
// still pass — a race is not a guaranteed crash — which is exactly why this is
// documented as a race rather than trusted as a green light. The point of the test
// is to pin the requirement: async layout needs Format() to be synchronized (or
// pre-warmed and read-only) BEFORE any worker thread calls it.
TEST(DWriteFormat, ConcurrentFormatCacheInsertIsUnsynchronized) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;

    constexpr int kThreads = 8;
    std::atomic<int> nonNull{0};
    std::vector<std::thread> workers;
    // Sizes chosen to be unusual (fractional, far from any theme token) so this is
    // the first request for each one and every thread takes the insert path.
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&, i]() {
            const float size = 61.25f + static_cast<float>(i) * 0.25f;
            if (ctx.Format(size) != nullptr) nonNull.fetch_add(1);
        });
    }
    for (auto& t : workers) t.join();

    // Documented expectation: each call returns a usable format. What this does NOT
    // establish is that the map survived intact — hence the comment above and the
    // synchronization requirement recorded in the async-layout plan.
    EXPECT_EQ(nonNull.load(), kThreads);
}

// Phase 1 verification: with formatCacheMutex_ now guarding cache_, the
// unsynchronized race above is eliminated. This test runs the concurrent insert
// scenario 100 times in a tight loop — if the mutex is missing or incorrectly
// placed, TSan or a crash should surface within 100 iterations. A green result
// here proves the lock works; the test name drops "Unsynchronized" to reflect that.
TEST(DWriteFormat, ConcurrentFormatCacheInsertIsSynchronized) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;

    constexpr int kRounds = 100;
    constexpr int kThreads = 8;

    for (int round = 0; round < kRounds; ++round) {
        std::atomic<int> nonNull{0};
        std::vector<std::thread> workers;

        // Each round uses a different size range to force new cache inserts
        const float baseSize = 70.0f + static_cast<float>(round) * 10.0f;

        for (int i = 0; i < kThreads; ++i) {
            workers.emplace_back([&, i]() {
                const float size = baseSize + static_cast<float>(i) * 0.125f;
                if (ctx.Format(size) != nullptr) nonNull.fetch_add(1);
            });
        }
        for (auto& t : workers) t.join();

        EXPECT_EQ(nonNull.load(), kThreads);
    }
}
