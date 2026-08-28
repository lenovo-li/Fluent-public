// ResourceCache.cpp — see ResourceCache.h.

#include "ResourceCache.h"
#include "DWriteContext.h"

namespace fluent {

namespace {
const char* kTag = "ResourceCache";

// Pack the stroke-style properties that matter into a 64-bit key. The dash cap /
// line join / dash style fit in a byte each; the miter limit and dash offset are
// quantized. Enough to distinguish the small set of styles a UI uses.
uint64_t PackStroke(const D2D1_STROKE_STYLE_PROPERTIES& p) {
    uint64_t k = 0;
    k |= static_cast<uint64_t>(p.startCap) & 0xF;
    k |= (static_cast<uint64_t>(p.endCap) & 0xF) << 4;
    k |= (static_cast<uint64_t>(p.dashCap) & 0xF) << 8;
    k |= (static_cast<uint64_t>(p.lineJoin) & 0xF) << 12;
    k |= (static_cast<uint64_t>(p.dashStyle) & 0xF) << 16;
    k |= (static_cast<uint64_t>(static_cast<uint32_t>(p.miterLimit * 16.0f)) & 0xFFFF) << 20;
    k |= (static_cast<uint64_t>(static_cast<uint32_t>(p.dashOffset * 16.0f)) & 0xFFFF) << 36;
    return k;
}
} // namespace

ComPtr<IDWriteTextLayout> ResourceCache::GetTextLayout(TextLayoutKey key) {
    if (!dwrite_ || !dwrite_->Valid()) return {};
    key.epoch = epoch_;
    return layouts_.GetOrCreate(key, [&]() -> ComPtr<IDWriteTextLayout> {
        IDWriteTextFormat* fmt = dwrite_->Format(key.fontSize, key.weight,
                                                 key.textAlign, key.paraAlign,
                                                 key.wrapping);
        if (!fmt) return {};
        ComPtr<IDWriteTextLayout> layout;
        HRESULT hr = dwrite_->Factory()->CreateTextLayout(
            key.text.c_str(), static_cast<UINT32>(key.text.size()), fmt,
            key.maxWidth, key.maxHeight, layout.GetAddressOf());
        if (FAILED(hr)) {
            Trace(kTag, "CreateTextLayout failed", hr);
            return {};
        }
        return layout;
    }, &stats_);
}

ComPtr<ID2D1PathGeometry> ResourceCache::GetGeometry(
    GlyphId glyph, float size,
    const std::function<void(ID2D1GeometrySink*)>& build) {
    if (!d2dFactory_) return {};
    GeometryKey key{static_cast<uint32_t>(glyph), size, epoch_};
    return geometries_.GetOrCreate(key, [&]() -> ComPtr<ID2D1PathGeometry> {
        ComPtr<ID2D1PathGeometry> geo;
        HRESULT hr = d2dFactory_->CreatePathGeometry(geo.GetAddressOf());
        if (FAILED(hr) || !geo) {
            Trace(kTag, "CreatePathGeometry failed", hr);
            return {};
        }
        ComPtr<ID2D1GeometrySink> sink;
        hr = geo->Open(sink.GetAddressOf());
        if (FAILED(hr) || !sink) {
            Trace(kTag, "geometry Open failed", hr);
            return {};
        }
        build(sink.Get());
        hr = sink->Close();
        if (FAILED(hr)) {
            Trace(kTag, "geometry sink Close failed", hr);
            return {};
        }
        return geo;
    }, &stats_);
}

ComPtr<ID2D1StrokeStyle> ResourceCache::GetStrokeStyle(
    const D2D1_STROKE_STYLE_PROPERTIES& props) {
    if (!d2dFactory_) return {};
    uint64_t key = PackStroke(props);
    return strokes_.GetOrCreate(key, [&]() -> ComPtr<ID2D1StrokeStyle> {
        ComPtr<ID2D1StrokeStyle> style;
        HRESULT hr = d2dFactory_->CreateStrokeStyle(props, nullptr, 0,
                                                    style.GetAddressOf());
        if (FAILED(hr)) {
            Trace(kTag, "CreateStrokeStyle failed", hr);
            return {};
        }
        return style;
    }, &stats_);
}

void ResourceCache::Clear() {
    layouts_.Clear();
    geometries_.Clear();
    strokes_.Clear();
}

} // namespace fluent
