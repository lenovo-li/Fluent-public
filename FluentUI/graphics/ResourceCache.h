// ResourceCache.h — centralized, epoch-versioned caches for the frequently
// rebuilt drawing resources the roadmap calls out (§13.3): IDWriteTextLayout,
// icon path geometry, and stroke styles. One instance is owned by the host and
// injected into the tree through UIContext, so any attached control reaches the
// same caches instead of rebuilding a layout / geometry every frame.
//
// Why this exists:
//   * TextBox rebuilt an identical IDWriteTextLayout 4-6 times per frame (Render
//     + CaretX + HitIndex + ClampScroll). Centralizing the layout keyed on its
//     inputs turns that into one build reused across every call in the frame.
//   * Icon glyphs (chevrons, tree icons) were emitted as raw primitives every
//     frame. A GeometryCache builds a device-independent ID2D1PathGeometry once,
//     keyed by glyph id + size, and the control positions it with a transform.
//
// Versioning (roadmap §13.3 "key must include DPI/theme"): TextLayout geometry
// is DIP-space and theme-independent (color is applied at draw time, never baked
// in), so for the WP-04 input set neither DPI nor theme actually changes a
// layout. We still expose a coarse `epoch` bumped on OnThemeChanged/OnDpiChanged:
// a stale-epoch lookup misses and rebuilds. It is a cheap, forward-looking
// backstop that becomes genuinely load-bearing in WP-05, when typography tokens
// make font size/weight theme-driven — then a theme change really does change
// layout inputs. Documented as belt-and-suspenders for now.
//
// Lifetime / device loss: everything cached here is a *factory* resource
// (IDWriteTextLayout from the DWrite factory, ID2D1PathGeometry / stroke style
// from the ID2D1Factory) — these survive a D3D device loss. WP-04's DeviceLost
// path still calls Clear() defensively so a rebuilt device starts from a known
// empty cache.
#pragma once

#include "../fl_common.h"
#include <d2d1_1.h>
#include <dwrite.h>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>

namespace fluent {

class DWriteContext;

// Cache hit/miss/size counters, sampled into FrameStats each frame. Plain fields
// (single-threaded UI, roadmap §16); no allocation on the hot path.
struct CacheStats {
    uint32_t hits = 0;     // served from an existing entry
    uint32_t misses = 0;   // built a new entry (a real resource creation)
    uint32_t entries = 0;  // live entries across all caches (sampled)

    void Reset() { hits = 0; misses = 0; entries = 0; }
};

// A small bounded LRU map keyed by `Key`. On a hit the entry moves to the front;
// on a miss `create` builds the value, and the least-recently-used entry is
// evicted past `capacity`. Kept header-only and templated so the pure-logic tests
// can instantiate it with trivial types (e.g. LruCache<int,int>) with no graphics
// dependency, while the typed caches below instantiate it with COM values.
template <class Key, class Value, class Hash = std::hash<Key>>
class LruCache {
public:
    explicit LruCache(size_t capacity = 64) : capacity_(capacity ? capacity : 1) {}

    // Return the cached value for `key`, or build it with `create` (a callable
    // returning Value) on a miss. `hit`/`miss` are incremented accordingly.
    template <class Create>
    Value GetOrCreate(const Key& key, Create&& create, CacheStats* stats) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            order_.splice(order_.begin(), order_, it->second);  // move to front (MRU)
            if (stats) ++stats->hits;
            return it->second->second;
        }
        Value v = create();
        order_.emplace_front(key, v);
        index_[key] = order_.begin();
        if (stats) ++stats->misses;
        if (order_.size() > capacity_) {
            auto last = std::prev(order_.end());
            index_.erase(last->first);
            order_.pop_back();
        }
        return v;
    }

    void Clear() { order_.clear(); index_.clear(); }
    size_t Size() const { return order_.size(); }
    size_t Capacity() const { return capacity_; }

private:
    using Entry = std::pair<Key, Value>;
    using List = std::list<Entry>;
    size_t capacity_;
    List order_;                                              // front = MRU
    std::unordered_map<Key, typename List::iterator, Hash> index_;
};

// Full key for a cached IDWriteTextLayout: the text plus every attribute that
// changes its geometry (the same closure TextBlock's own key uses, generalized).
// `epoch` folds in the cache-wide version so a theme/DPI bump misses cleanly.
struct TextLayoutKey {
    std::wstring text;
    float fontSize = 0.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
    uint32_t epoch = 0;

    bool operator==(const TextLayoutKey& o) const {
        return fontSize == o.fontSize && weight == o.weight &&
               textAlign == o.textAlign && paraAlign == o.paraAlign &&
               wrapping == o.wrapping && maxWidth == o.maxWidth &&
               maxHeight == o.maxHeight && epoch == o.epoch && text == o.text;
    }
};

struct TextLayoutKeyHash {
    size_t operator()(const TextLayoutKey& k) const {
        size_t h = std::hash<std::wstring>{}(k.text);
        auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(std::hash<float>{}(k.fontSize));
        mix(static_cast<size_t>(k.weight));
        mix(static_cast<size_t>(k.textAlign));
        mix(static_cast<size_t>(k.paraAlign));
        mix(static_cast<size_t>(k.wrapping));
        mix(std::hash<float>{}(k.maxWidth));
        mix(std::hash<float>{}(k.maxHeight));
        mix(k.epoch);
        return h;
    }
};

// Key for a cached icon path geometry: a stable glyph id + a size bucket (DIP).
// The geometry is built in a fixed unit/local space; the control positions it
// with a DrawingContext transform, so position is NOT part of the key.
struct GeometryKey {
    uint32_t glyphId = 0;   // stable per glyph shape (see GlyphId below)
    float size = 0.0f;      // nominal size in DIP the geometry was built for
    uint32_t epoch = 0;

    bool operator==(const GeometryKey& o) const {
        return glyphId == o.glyphId && size == o.size && epoch == o.epoch;
    }
};

struct GeometryKeyHash {
    size_t operator()(const GeometryKey& k) const {
        size_t h = std::hash<uint32_t>{}(k.glyphId);
        h ^= std::hash<float>{}(k.size) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<uint32_t>{}(k.epoch) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

// Stable ids for the shared icon glyphs converted to cached geometry (WP-04).
// Only static, single-part, single-color glyphs are cached this way; animated /
// parametric glyphs (checkbox tick growth, radio dot, toggle knob, slider thumb)
// stay parametric and are NOT in this cache.
enum class GlyphId : uint32_t {
    ChevronDown = 1,   // ComboBox collapsed indicator
    ChevronRight = 2,  // MenuFlyout submenu indicator
    CheckMark = 3,     // MenuFlyout checked-item glyph
};

// The host-owned resource cache injected into the tree via UIContext. Holds the
// factories it needs (set once at init, refreshed on device rebuild) and the
// typed caches. Non-owning of the factories (owned by D2DContext / DWriteContext).
class ResourceCache {
public:
    // Wire the factories used to build resources. Called by the host after the
    // device stack is up, and again after a device rebuild (with Clear() first).
    void Initialize(DWriteContext* dwrite, ID2D1Factory* d2dFactory) {
        dwrite_ = dwrite;
        d2dFactory_ = d2dFactory;
    }

    // Current version. Bumped on theme / DPI change; folded into every key so a
    // stale entry can never be reused across a bump.
    uint32_t Epoch() const { return epoch_; }
    void BumpEpoch() { ++epoch_; }

    // Get (or build) the layout for `key` (its `epoch` is filled in here). Returns
    // null if DWrite is unavailable. Reuses DWriteContext's immutable format cache.
    ComPtr<IDWriteTextLayout> GetTextLayout(TextLayoutKey key);

    // Get (or build) a unit-space path geometry for `glyph` at `size`. `build`
    // fills a fresh geometry sink on a miss (called at most once per key). Returns
    // null if the D2D factory is unavailable or the build failed.
    ComPtr<ID2D1PathGeometry> GetGeometry(
        GlyphId glyph, float size,
        const std::function<void(ID2D1GeometrySink*)>& build);

    // Get (or build) a stroke style for the given properties. Infrastructure for
    // dashed borders / focus rects (roadmap §13.3); no caller in WP-04 yet, but
    // built + tested so a future dashed style reuses one object.
    ComPtr<ID2D1StrokeStyle> GetStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& props);

    // Drop every cached GPU/factory resource (device rebuild). Keeps the epoch
    // (logical version) but empties the maps.
    void Clear();

    // Diagnostics: accumulated hit/miss since the last ResetFrameStats, plus the
    // live entry count sampled now. The host copies these into FrameStats/frame.
    CacheStats& Stats() { return stats_; }
    uint32_t LiveEntries() const {
        return static_cast<uint32_t>(layouts_.Size() + geometries_.Size() +
                                     strokes_.Size());
    }
    void ResetFrameStats() { stats_.Reset(); }

private:
    DWriteContext* dwrite_ = nullptr;   // non-owning
    ID2D1Factory* d2dFactory_ = nullptr;  // non-owning
    uint32_t epoch_ = 0;
    CacheStats stats_;

    LruCache<TextLayoutKey, ComPtr<IDWriteTextLayout>, TextLayoutKeyHash> layouts_{128};
    LruCache<GeometryKey, ComPtr<ID2D1PathGeometry>, GeometryKeyHash> geometries_{64};
    // Stroke styles keyed by a packed property blob; tiny fixed set.
    LruCache<uint64_t, ComPtr<ID2D1StrokeStyle>> strokes_{16};
};

} // namespace fluent
