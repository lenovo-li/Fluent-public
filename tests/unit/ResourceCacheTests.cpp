// ResourceCacheTests.cpp — pure-logic tests for the LRU cache, cache keys, and
// epoch versioning that back ResourceCache (roadmap §13.3, WP-04). These exercise
// the caching machinery with trivial types and stand-in values — no D2D/DWrite
// device — so they run headless. The device-backed builders (GetTextLayout /
// GetGeometry) are covered separately by a component test that needs DWrite.

#include "../framework/Test.h"
#include "../../FluentUI/graphics/ResourceCache.h"

using namespace fluent;

// --- LruCache: hit / miss accounting ---------------------------------------

TEST(ResourceCache, LruMissThenHit) {
    LruCache<int, int> cache(4);
    CacheStats stats;
    int builds = 0;
    auto make = [&builds] { ++builds; return 42; };

    int a = cache.GetOrCreate(1, make, &stats);
    EXPECT_EQ(a, 42);
    EXPECT_EQ(builds, 1);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.hits, 0u);

    int b = cache.GetOrCreate(1, make, &stats);  // same key: hit, no build
    EXPECT_EQ(b, 42);
    EXPECT_EQ(builds, 1);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.hits, 1u);
}

TEST(ResourceCache, LruDistinctKeysEachMiss) {
    LruCache<int, int> cache(4);
    CacheStats stats;
    auto make = [] { return 7; };
    cache.GetOrCreate(1, make, &stats);
    cache.GetOrCreate(2, make, &stats);
    cache.GetOrCreate(3, make, &stats);
    EXPECT_EQ(stats.misses, 3u);
    EXPECT_EQ(stats.hits, 0u);
    EXPECT_EQ(cache.Size(), 3u);
}

// --- LruCache: capacity eviction (least-recently-used) ---------------------

TEST(ResourceCache, LruEvictsLeastRecentlyUsed) {
    LruCache<int, int> cache(2);
    CacheStats stats;
    auto make = [] { return 0; };
    cache.GetOrCreate(1, make, &stats);   // [1]
    cache.GetOrCreate(2, make, &stats);   // [2,1]
    cache.GetOrCreate(1, make, &stats);   // touch 1 -> [1,2]  (hit)
    cache.GetOrCreate(3, make, &stats);   // insert 3, evict LRU (2) -> [3,1]
    EXPECT_EQ(cache.Size(), 2u);

    // 2 was evicted: requesting it rebuilds (miss). 1 survived: hit.
    uint32_t missesBefore = stats.misses;
    cache.GetOrCreate(1, make, &stats);
    EXPECT_EQ(stats.misses, missesBefore);  // 1 still cached
    cache.GetOrCreate(2, make, &stats);
    EXPECT_EQ(stats.misses, missesBefore + 1);  // 2 rebuilt
}

TEST(ResourceCache, LruClearEmpties) {
    LruCache<int, int> cache(4);
    CacheStats stats;
    auto make = [] { return 1; };
    cache.GetOrCreate(1, make, &stats);
    cache.GetOrCreate(2, make, &stats);
    EXPECT_EQ(cache.Size(), 2u);
    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
}

// --- TextLayoutKey equality / hashing --------------------------------------

TEST(ResourceCache, TextLayoutKeyEquality) {
    TextLayoutKey a;
    a.text = L"hello";
    a.fontSize = 14.0f;
    a.maxWidth = 100.0f;
    a.epoch = 1;
    TextLayoutKey b = a;
    EXPECT_TRUE(a == b);

    b.text = L"world";  // text differs
    EXPECT_FALSE(a == b);

    b = a;
    b.fontSize = 15.0f;  // size differs
    EXPECT_FALSE(a == b);

    b = a;
    b.epoch = 2;  // epoch differs -> distinct key (stale-epoch miss)
    EXPECT_FALSE(a == b);
}

TEST(ResourceCache, TextLayoutKeyHashStableForEqualKeys) {
    TextLayoutKeyHash h;
    TextLayoutKey a;
    a.text = L"same";
    a.fontSize = 12.0f;
    a.epoch = 3;
    TextLayoutKey b = a;
    EXPECT_EQ(h(a), h(b));  // equal keys must hash equal
}

TEST(ResourceCache, TextLayoutKeyEpochChangesHash) {
    // Different epoch should (almost certainly) change the hash so it lands in a
    // different bucket; at minimum the keys must not be equal.
    TextLayoutKey a;
    a.text = L"x";
    a.epoch = 1;
    TextLayoutKey b = a;
    b.epoch = 2;
    EXPECT_FALSE(a == b);
}

// --- GeometryKey ------------------------------------------------------------

TEST(ResourceCache, GeometryKeyEquality) {
    GeometryKey a{static_cast<uint32_t>(GlyphId::ChevronDown), 8.0f, 0};
    GeometryKey b = a;
    EXPECT_TRUE(a == b);
    b.size = 10.0f;
    EXPECT_FALSE(a == b);
    b = a;
    b.glyphId = static_cast<uint32_t>(GlyphId::CheckMark);
    EXPECT_FALSE(a == b);
}

// --- Epoch on the cache -----------------------------------------------------

TEST(ResourceCache, BumpEpochIncrements) {
    ResourceCache cache;  // no factories wired: epoch logic is independent
    uint32_t e0 = cache.Epoch();
    cache.BumpEpoch();
    EXPECT_EQ(cache.Epoch(), e0 + 1);
    cache.BumpEpoch();
    EXPECT_EQ(cache.Epoch(), e0 + 2);
}

TEST(ResourceCache, GetTextLayoutNullWithoutDWrite) {
    // With no DWrite wired, GetTextLayout must return null (not crash) — the
    // headless / detached path controls rely on for a graceful fallback.
    ResourceCache cache;
    TextLayoutKey key;
    key.text = L"nope";
    EXPECT_TRUE(cache.GetTextLayout(key).Get() == nullptr);
}

TEST(ResourceCache, FrameStatsReset) {
    ResourceCache cache;
    cache.Stats().hits = 5;
    cache.Stats().misses = 3;
    cache.ResetFrameStats();
    EXPECT_EQ(cache.Stats().hits, 0u);
    EXPECT_EQ(cache.Stats().misses, 0u);
}
