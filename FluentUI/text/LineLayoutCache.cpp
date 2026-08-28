// LineLayoutCache.cpp — LRU cache of per-line IDWriteTextLayout objects.

#include "LineLayoutCache.h"
#include <limits>

namespace fluent {

IDWriteTextLayout* LineLayoutCache::Get(size_t lineNumber, std::wstring_view lineText,
                                        IDWriteFactory* factory, IDWriteTextFormat* fmt,
                                        float dpiScale, unsigned generation,
                                        size_t minCover) {
    // Clamp minCover so we never ask DWrite to build a layout longer than the line.
    // kFullLine (max size_t) saturates here to lineText.size(), which is correct.
    const size_t cover = std::min(minCover, lineText.size());

    InvalidateIfStale(dpiScale, generation);

    auto it = map_.find(lineNumber);
    if (it != map_.end()) {
        if (it->second.coverEnd >= cover) {
            // Hit: the cached layout covers at least as many characters as needed.
            // Promote to most-recently-used.
            lru_.splice(lru_.begin(), lru_, it->second.lruIt);
            return it->second.layout.Get();
        }
        // The cached layout is shorter than the new request. Evict it so the rebuild
        // below produces a longer one. This happens when a draw query that used a short
        // prefix is followed by a caret query that needs the full line — the upgrade is
        // automatic and the caller does not need to know about it.
        lru_.erase(it->second.lruIt);
        map_.erase(it);
    }

    if (!factory || !fmt) return nullptr;

    // Build a layout covering [0, cover). An empty line (cover == 0) still gets a
    // valid layout reporting the font's default line height — needed for the blank-row
    // height in a multi-line selection.
    const std::wstring_view buildText = lineText.substr(0, cover);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(buildText.data(),
                                         static_cast<UINT32>(buildText.size()), fmt,
                                         boxWidth_, boxHeight_,
                                         layout.GetAddressOf())) || !layout)
        return nullptr;

    if (map_.size() >= capacity_) Evict();

    lru_.push_front(lineNumber);
    Entry entry{std::move(layout), lru_.begin(), cover};
    auto [ins, ok] = map_.emplace(lineNumber, std::move(entry));
    if (!ok) {
        lru_.pop_front();
        return nullptr;
    }
    return ins->second.layout.Get();
}

size_t LineLayoutCache::GetCoverEnd(size_t lineNumber) const {
    auto it = map_.find(lineNumber);
    return (it != map_.end()) ? it->second.coverEnd : 0;
}

void LineLayoutCache::Clear() {
    lru_.clear();
    map_.clear();
}

bool LineLayoutCache::Erase(size_t lineNumber) {
    auto it = map_.find(lineNumber);
    if (it == map_.end()) return false;
    // Both containers, in this order: the LRU node is reached THROUGH the map entry,
    // so erasing the map first would leave the list holding a number with no entry and
    // no way to find the node again — Evict() would then erase a nonexistent key and
    // silently do nothing, letting the cache grow past its capacity.
    lru_.erase(it->second.lruIt);
    map_.erase(it);
    return true;
}

void LineLayoutCache::SetLayoutBox(float widthDip, float heightDip) {
    if (!(widthDip > 0.0f) || !(heightDip > 0.0f)) return;
    if (widthDip == boxWidth_ && heightDip == boxHeight_) return;
    boxWidth_ = widthDip;
    boxHeight_ = heightDip;
    // Every cached layout was built at the old box. Under Wrap that means the old wrap
    // width, so the cached line breaks are wrong — keeping them would draw last frame's
    // wrapping at this frame's width during a resize drag.
    Clear();
}

void LineLayoutCache::InvalidateIfStale(float dpiScale, unsigned generation) {
    if (dpiScale == lastDpi_ && generation == lastGeneration_) return;
    Clear();
    lastDpi_ = dpiScale;
    lastGeneration_ = generation;
}

void LineLayoutCache::Evict() {
    if (lru_.empty()) return;
    map_.erase(lru_.back());
    lru_.pop_back();
}

} // namespace fluent
