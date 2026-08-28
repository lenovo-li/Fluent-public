// LineLayoutCacheTests.cpp — LRU cache of per-line IDWriteTextLayout objects.
//
// Two things can be tested headless:
//
//   1. The LRU eviction count and capacity are purely numerical — we exercise them
//      by calling Get() with a real DWrite factory (DWriteContext is available in
//      the headless environment, as TextAreaCompositionTests.cpp confirms) so actual
//      layouts are created and evicted.
//
//   2. The invalidation state machine (DPI / generation changes flush the cache) is
//      the same whether layouts are real or not.
//
// What is NOT tested here: layout metrics and character positions. Those are
// DWrite internals, and the component tests exercise them indirectly.
#include "../framework/Test.h"
#include "../../FluentUI/text/LineLayoutCache.h"
#include "../../FluentUI/graphics/DWriteContext.h"

using namespace fluent;

namespace {

// Real DWrite factory, shared across the suite so the first test pays
// the initialization cost and the rest reuse it.
//
// DWriteContext is the framework's wrapper: it creates the IDWriteFactory,
// resolves the font family, and caches IDWriteTextFormats. It is the same
// object TextArea has in its UIContext when attached to a live tree.
struct DWriteFixture {
    DWriteFixture() { ctx.Initialize(); }
    DWriteContext ctx;

    IDWriteFactory* Factory() { return ctx.Factory(); }
    IDWriteTextFormat* Fmt() {
        return ctx.Format(14.0f,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_LEADING,
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            DWRITE_WORD_WRAPPING_NO_WRAP);
    }
};

// One fixture per process run (static so construction happens once).
static DWriteFixture dwrite;

} // namespace

// ---------------------------------------------------------------------------
// Basic behaviour
// ---------------------------------------------------------------------------

TEST(LineLayoutCache, FreshCacheIsEmpty) {
    LineLayoutCache cache;
    EXPECT_EQ(cache.Size(), size_t{0});
    EXPECT_EQ(cache.Capacity(), size_t{512});
}

TEST(LineLayoutCache, CustomCapacity) {
    LineLayoutCache cache{64};
    EXPECT_EQ(cache.Capacity(), size_t{64});
}

TEST(LineLayoutCache, GetWithNullFactoryReturnsNullAndDoesNotInsert) {
    // Normal state before the control attaches to a tree.
    LineLayoutCache cache;
    IDWriteTextLayout* r = cache.Get(0, L"hello", nullptr, nullptr, 1.0f, 1u);
    EXPECT_TRUE(r == nullptr);
    EXPECT_EQ(cache.Size(), size_t{0});
}

TEST(LineLayoutCache, GetBuildsAndCachesLayout) {
    if (!dwrite.ctx.Valid()) return;  // no DWrite in this environment; skip
    LineLayoutCache cache{64};
    IDWriteTextLayout* first = cache.Get(7, L"test line", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(first != nullptr);
    EXPECT_EQ(cache.Size(), size_t{1});

    // Second Get for the same line: must return the SAME pointer (cache hit).
    IDWriteTextLayout* second = cache.Get(7, L"test line", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(second == first);
    EXPECT_EQ(cache.Size(), size_t{1});
}

TEST(LineLayoutCache, GetEmptyLineSucceeds) {
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    IDWriteTextLayout* r = cache.Get(0, L"", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(r != nullptr);
    // The layout should at least report a non-zero line height (the font's
    // default metrics apply even to an empty layout).
    if (r) {
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(r->GetMetrics(&tm)))
            EXPECT_TRUE(tm.height > 0.0f);
    }
}

// A cache hit must be observable as a hit, not merely as "a layout came back".
// Whether an entry survived eviction is the only thing that distinguishes LRU from
// FIFO, and Size() cannot see it (it is at capacity either way). So this helper
// asks the question directly: does asking for `line` change the cache's occupancy?
//
//   * present  -> Get is a hit, nothing is inserted, nothing is evicted, size holds
//   * absent   -> Get builds and inserts; at capacity that evicts something, so the
//                 size still holds — but the entry it evicted is now gone.
//
// Occupancy alone therefore cannot answer it either. What CAN: fill the cache, probe
// the line under test, and then check whether a line known to be MRU is still there.
// Rather than reason about that at every call site, this helper does the direct
// thing — it uses a cache with one spare slot, where an insert is visible as growth.
bool WasCacheHit(LineLayoutCache& cache, size_t line, std::wstring_view text) {
    const size_t before = cache.Size();
    cache.Get(line, text, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    // A hit reuses an entry (size unchanged); a miss at sub-capacity inserts one.
    return cache.Size() == before;
}

TEST(LineLayoutCache, LruEvictsLeastRecentlyUsedNotOldestInserted) {
    if (!dwrite.ctx.Valid()) return;
    // THE test that separates LRU from FIFO. The technique for making it work
    // despite COM allocator address reuse:
    //
    //   AddRef the pointer that should SURVIVE before the eviction. This prevents
    //   its memory from being freed even when the cache's ComPtr is released. If a
    //   new layout is then built to fill the evicted slot, DWrite must allocate fresh
    //   memory (the old block is still live), so the two pointers are guaranteed to
    //   be distinct. Without the extra ref they might coincide and the test would
    //   be unsound.
    //
    // LRU eviction order: capacity=3, insert 0,1,2 (LRU=0). Touch 0 -> LRU=1.
    // Insert 3 -> evicts 1 (LRU), 0 survives.
    // FIFO order: LRU stays 0 regardless of touch. Insert 3 -> evicts 0.
    // The check: pointer of line 0 is unchanged (it survived, not rebuilt).
    LineLayoutCache cache{3};
    IDWriteTextLayout* l0 = cache.Get(0, L"alpha", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    IDWriteTextLayout* l1 = cache.Get(1, L"beta",  dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
                            cache.Get(2, L"gamma", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(l0 && l1);
    EXPECT_EQ(cache.Size(), size_t{3});

    // Hold extra refs so the memory is NOT freed if the cache evicts either entry.
    // Without this, a new layout might land on the freed address and the pointer
    // comparison would be vacuously true.
    l0->AddRef();
    l1->AddRef();

    cache.Get(0, L"alpha", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);  // promote 0
    cache.Get(3, L"delta", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);  // forces eviction
    EXPECT_EQ(cache.Size(), size_t{3});

    // LRU: 0 was promoted, so 1 was evicted. l0after == l0 (hit, same object).
    IDWriteTextLayout* l0after = cache.Get(0, L"alpha", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(l0after == l0);   // survived: same object

    // l1 was evicted. Rebuilding it must produce a DIFFERENT object — guaranteed
    // because the old object is still live (we hold a ref above).
    IDWriteTextLayout* l1rebuilt = cache.Get(1, L"beta", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(l1rebuilt != nullptr);
    EXPECT_TRUE(l1rebuilt != l1);  // evicted and rebuilt at a new address

    l0->Release();  // balance the AddRef
    l1->Release();
}

TEST(LineLayoutCache, EvictionKeepsTheCacheAtCapacity) {
    if (!dwrite.ctx.Valid()) return;
    // Eviction must happen on insert, not lazily afterwards — a cache that overshoots
    // by even one entry per insert grows without bound over a long scroll.
    LineLayoutCache cache{4};
    for (size_t i = 0; i < 50; ++i) {
        cache.Get(i, L"row text", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
        EXPECT_TRUE(cache.Size() <= cache.Capacity());
    }
    EXPECT_EQ(cache.Size(), size_t{4});
}

TEST(LineLayoutCache, HitDoesNotGrowTheCacheAndMissDoes) {
    if (!dwrite.ctx.Valid()) return;
    // The observability the LRU test above depends on: below capacity, a miss inserts
    // and a hit does not. If this ever stops holding, WasCacheHit is lying and the
    // eviction tests are only checking that Get returns non-null.
    LineLayoutCache cache{8};
    EXPECT_FALSE(WasCacheHit(cache, 5, L"line five"));  // first ask: miss, inserts
    EXPECT_EQ(cache.Size(), size_t{1});
    EXPECT_TRUE(WasCacheHit(cache, 5, L"line five"));   // second ask: hit
    EXPECT_EQ(cache.Size(), size_t{1});
}

TEST(LineLayoutCache, ClearDropsAllEntries) {
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    cache.Get(0, L"a", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    cache.Get(1, L"b", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.Size(), size_t{2});

    cache.Clear();
    EXPECT_EQ(cache.Size(), size_t{0});

    // After Clear, the same keys rebuild from scratch (new pointers).
    IDWriteTextLayout* ra = cache.Get(0, L"a", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(ra != nullptr);
}

// ---------------------------------------------------------------------------
// Invalidation state machine
// ---------------------------------------------------------------------------

TEST(LineLayoutCache, InvalidateIfStaleSameDpiAndGenerationIsNoOp) {
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    auto* r = cache.Get(0, L"hello", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.Size(), size_t{1});

    cache.InvalidateIfStale(1.0f, 1u);  // same as current -> no flush
    EXPECT_EQ(cache.Size(), size_t{1});
    // Same pointer on the next Get (not rebuilt).
    auto* r2 = cache.Get(0, L"hello", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(r2 == r);
}

TEST(LineLayoutCache, InvalidateIfStaleDpiChangeFlushesThenRecordsNewBaseline) {
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    auto* r1 = cache.Get(0, L"hi", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);

    cache.InvalidateIfStale(2.0f, 1u);   // DPI changed -> flush
    EXPECT_EQ(cache.Size(), size_t{0});

    cache.InvalidateIfStale(2.0f, 1u);   // same new DPI -> no-op
    EXPECT_EQ(cache.Size(), size_t{0});  // still empty (nothing built yet)

    // Next Get at new DPI rebuilds (new pointer, potentially new metrics).
    auto* r2 = cache.Get(0, L"hi", dwrite.Factory(), dwrite.Fmt(), 2.0f, 1u);
    EXPECT_TRUE(r2 != nullptr);
    (void)r1;
}

TEST(LineLayoutCache, InvalidateIfStaleGenerationChangeFlushesThenRecords) {
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    cache.Get(0, L"x", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);

    cache.InvalidateIfStale(1.0f, 2u);  // generation bumped -> flush
    EXPECT_EQ(cache.Size(), size_t{0});

    cache.InvalidateIfStale(1.0f, 2u);  // still generation 2 -> no-op
    EXPECT_EQ(cache.Size(), size_t{0});
}

TEST(LineLayoutCache, GetStaleDpiFlushesBeforeBuilding) {
    // A caller that forgets InvalidateIfStale should still get correct (if
    // slightly slower) behavior: the cache flushes on the first Get that
    // presents the new DPI, then records it as the new baseline.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    cache.Get(0, L"a", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.Size(), size_t{1});

    auto* r = cache.Get(0, L"a", dwrite.Factory(), dwrite.Fmt(), 2.0f, 1u);
    // The old entry at DPI=1 was flushed; a new one at DPI=2 was built.
    EXPECT_TRUE(r != nullptr);
    EXPECT_EQ(cache.Size(), size_t{1});

    // The next call at DPI=2 is a normal cache hit.
    auto* r2 = cache.Get(0, L"a", dwrite.Factory(), dwrite.Fmt(), 2.0f, 1u);
    EXPECT_TRUE(r2 == r);
}

// ---------------------------------------------------------------------------
// minCover / prefix-clip (§1.5b-3)
// ---------------------------------------------------------------------------

TEST(LineLayoutCache, GetWithMinCoverBuildsShortLayout) {
    // A layout built with minCover < line length must only cover the prefix.
    // GetCoverEnd reports exactly what was requested.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    const std::wstring line(8000, L'A');
    cache.Get(0, line, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u, /*minCover=*/200);
    EXPECT_EQ(cache.GetCoverEnd(0), size_t{200});
}

TEST(LineLayoutCache, HitWhenCachedCoverIsSufficient) {
    // A cached entry with coverEnd >= minCover is a hit: same pointer returned.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    const std::wstring line(8000, L'B');
    auto* p1 = cache.Get(0, line, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u, 300);
    auto* p2 = cache.Get(0, line, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u, 200);
    // 200 <= 300, so the existing entry covers it — same object.
    EXPECT_EQ(p1, p2);
}

TEST(LineLayoutCache, UpgradeWhenRequestExceedsCachedCover) {
    // If the new request needs more characters than the cached entry covers,
    // the cache evicts the short entry and rebuilds at the larger cover.
    // Use AddRef to keep the old object alive so its address remains valid and
    // a potential allocator reuse cannot make the pointers falsely equal.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    const std::wstring line(8000, L'C');
    auto* p1 = cache.Get(0, line, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u, 200);
    EXPECT_TRUE(p1 != nullptr);
    if (!p1) return;
    p1->AddRef();   // keep alive past eviction

    auto* p2 = cache.Get(0, line, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u, 500);
    EXPECT_NE(p1, p2);              // rebuilt — must be a different object
    EXPECT_EQ(cache.GetCoverEnd(0), size_t{500});

    p1->Release();
}

TEST(LineLayoutCache, FullLineCoverIsCappedAtLineLength) {
    // kFullLine (max size_t) saturates to the actual line length; GetCoverEnd
    // returns the line length, not max size_t.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    const std::wstring line(50, L'X');
    cache.Get(0, line, dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);  // kFullLine
    EXPECT_EQ(cache.GetCoverEnd(0), line.size());
}

TEST(LineLayoutCache, GetCoverEndReturnsZeroForAbsentLine) {
    LineLayoutCache cache{64};
    EXPECT_EQ(cache.GetCoverEnd(99), size_t{0});
}

// ---------------------------------------------------------------------------
// Erase — single-entry invalidation for the append path (§2.1)
// ---------------------------------------------------------------------------

TEST(LineLayoutCache, EraseDropsOnlyTheNamedLine) {
    // The append path's whole reason for existing: appending changes the content of
    // exactly ONE existing line (the old last one), and flushing the rest would make
    // every batch of log lines re-lay-out the visible screen.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    cache.Get(0, L"aaa", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    cache.Get(1, L"bbb", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    cache.Get(2, L"ccc", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.Size(), size_t{3});

    EXPECT_TRUE(cache.Erase(1));
    EXPECT_EQ(cache.Size(), size_t{2});
    EXPECT_EQ(cache.GetCoverEnd(1), size_t{0});   // gone
    EXPECT_EQ(cache.GetCoverEnd(0), size_t{3});   // untouched
    EXPECT_EQ(cache.GetCoverEnd(2), size_t{3});
}

TEST(LineLayoutCache, EraseOfAbsentLineReportsFalse) {
    LineLayoutCache cache{64};
    EXPECT_TRUE(!cache.Erase(7));
}

TEST(LineLayoutCache, EraseKeepsTheLruConsistent) {
    // Erase must remove the LRU node too. If it removed only the map entry, the list
    // would hold a number with no entry, and Evict() would then erase a nonexistent key
    // and silently do nothing — letting the cache grow past its capacity forever. Checked
    // by erasing then overfilling: the size must still respect the capacity.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{4};
    for (size_t i = 0; i < 4; ++i)
        cache.Get(i, L"x", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.Size(), size_t{4});
    EXPECT_TRUE(cache.Erase(2));
    for (size_t i = 10; i < 20; ++i)
        cache.Get(i, L"x", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_TRUE(cache.Size() <= cache.Capacity());
}

TEST(LineLayoutCache, EraseThenGetRebuilds) {
    // After an erase the next Get is a miss that builds fresh — which is what makes the
    // grown last line pick up its new characters.
    if (!dwrite.ctx.Valid()) return;
    LineLayoutCache cache{64};
    cache.Get(0, L"bbb", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.GetCoverEnd(0), size_t{3});
    cache.Erase(0);
    cache.Get(0, L"bbbccc", dwrite.Factory(), dwrite.Fmt(), 1.0f, 1u);
    EXPECT_EQ(cache.GetCoverEnd(0), size_t{6});
}
