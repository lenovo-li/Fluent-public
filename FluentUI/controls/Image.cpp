// Image.cpp — bitmap display control.

#include "Image.h"
#include "../core/UIContext.h"
#include "../window/WindowServices.h"
#include "../graphics/D2DContext.h"
#include "../graphics/DrawingContext.h"
#include <wincodec.h>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")

namespace fluent {

namespace {
const char* kTag = "Image";

// Lazily-created process-wide WIC factory. COM is initialized by the app.
// Deliberately leaked (raw pointer, never deleted) to avoid calling Release()
// during CRT teardown after CoUninitialize — that ordering is what caused the
// access violation on close when an Image was present but its source was missing.
IWICImagingFactory* GetWICFactory() {
    static IWICImagingFactory* s = nullptr;
    if (!s) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s));
        if (FAILED(hr)) Trace(kTag, "WICFactory create failed", hr);
    }
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// ComputeImageDrawRects — the geometry, as a pure function.
//
// dest: where in the window (DIPs) the image is drawn.
// src:  which portion of the image (image DIPs) is mapped to dest.
//
// A src narrower than nativeSize means the mode crops (None + UniformToFill).
// D2D stretches whatever src it receives into whatever dest it receives, so
// getting src wrong means a squashed or stretched result, not a clip.
// ---------------------------------------------------------------------------

ImageDrawRects ComputeImageDrawRects(SizeDip native, RectDip bounds,
                                     ImageStretch mode)
{
    if (native.w <= 0.0f || native.h <= 0.0f || bounds.w <= 0.0f || bounds.h <= 0.0f)
        return {{}, {}};

    switch (mode) {

    case ImageStretch::None: {
        // Render at native size, centered in bounds. If the image is larger than
        // the bounds on either axis, crop the image (not squash the dest).
        float destW = std::min(native.w, bounds.w);
        float destH = std::min(native.h, bounds.h);
        float destX = bounds.x + (bounds.w - destW) * 0.5f;
        float destY = bounds.y + (bounds.h - destH) * 0.5f;
        // Source: the central `destW x destH` tile of the native image.
        float srcX = (native.w - destW) * 0.5f;
        float srcY = (native.h - destH) * 0.5f;
        return {{destX, destY, destW, destH}, {srcX, srcY, destW, destH}};
    }

    case ImageStretch::Uniform: {
        // Scale so the whole image fits inside bounds, letter-/pillar-boxed.
        float scaleX = bounds.w / native.w;
        float scaleY = bounds.h / native.h;
        float scale  = std::min(scaleX, scaleY);
        float destW  = native.w * scale;
        float destH  = native.h * scale;
        float destX  = bounds.x + (bounds.w - destW) * 0.5f;
        float destY  = bounds.y + (bounds.h - destH) * 0.5f;
        // Source: whole image.
        return {{destX, destY, destW, destH}, {0, 0, native.w, native.h}};
    }

    case ImageStretch::UniformToFill: {
        // Scale so the image fills bounds on both axes, preserving aspect ratio.
        // The axis that overflows is centered and cropped.
        float scaleX = bounds.w / native.w;
        float scaleY = bounds.h / native.h;
        float scale  = std::max(scaleX, scaleY);
        // In image space, the visible area is bounds / scale.
        float visW   = bounds.w / scale;
        float visH   = bounds.h / scale;
        float srcX   = (native.w - visW) * 0.5f;
        float srcY   = (native.h - visH) * 0.5f;
        // Dest: the full bounds.
        return {bounds, {srcX, srcY, visW, visH}};
    }

    case ImageStretch::Fill: {
        // Fill bounds exactly; aspect ratio not preserved. Dest = bounds, src = all.
        return {bounds, {0, 0, native.w, native.h}};
    }

    default:
        return {{}, {}};
    }
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

Image::Image()  = default;
Image::~Image() = default;

void Image::SetSource(std::wstring path) {
    if (source_ == path) return;
    source_        = std::move(path);
    loadAttempted_ = false;
    fallbackAttempted_ = false;
    pendingWic_.Reset();
    ClearImage();
    InvalidateDirty(DirtyFlags::Measure);
}

void Image::SetSource(IWICBitmapSource* bmp) {
    source_.clear();
    pendingWic_    = bmp;
    loadAttempted_ = false;
    fallbackAttempted_ = false;
    ClearImage();
    InvalidateDirty(DirtyFlags::Measure);
}

void Image::SetFailureSource(std::wstring path) {
    if (failureSource_ == path) return;
    failureSource_ = std::move(path);
    // Only invalidate if the primary has already failed and we're showing nothing
    // (or an old fallback) — otherwise the setter is setting up a safety net that
    // has not been needed yet, and re-loading immediately would discard a working
    // primary bitmap.
    if (loadAttempted_ && !bitmap_) InvalidateDirty(DirtyFlags::Measure);
}

void Image::ClearImage() {
    bitmap_.Reset();
    nativeSize_ = {0.0f, 0.0f};
}

void Image::OnAttachedToTree() {
    // Device is now available. A SetSource that ran before attach just recorded
    // the path; finish the load here.
    if (!loadAttempted_ && (!source_.empty() || pendingWic_)) {
        LoadImage();
        if (bitmap_) InvalidateDirty(DirtyFlags::Measure);
    }
}

void Image::OnDetachedFromTree() {}

void Image::OnDeviceLost() {
    // Drop the D2D bitmap (the device it lives on is gone).  The path and native
    // size are still valid — keep them so remeasure is not needed after restore.
    bitmap_.Reset();
}

void Image::OnDeviceRestored() {
    // Rebuild the bitmap on the new device.
    loadAttempted_ = false;
    if (Context().window && (!source_.empty() || pendingWic_)) {
        LoadImage();
        if (bitmap_) Invalidate();
    }
}

void Image::Measure(float availW, float availH) {
    // Load if not attempted yet and we are now on a live tree.
    if (!loadAttempted_ && (!source_.empty() || pendingWic_) && Context().window)
        LoadImage();

    const bool haveNative = nativeSize_.w > 0.0f && nativeSize_.h > 0.0f;
    if (!haveNative) {
        FrameworkElement::Measure(availW, availH);
        return;
    }

    float w = IsAuto(width_)  ? nativeSize_.w : width_;
    float h = IsAuto(height_) ? nativeSize_.h : height_;
    if (availW > 0.0f && w > availW) w = availW;
    if (availH > 0.0f && h > availH) h = availH;
    SetDesired({(w < 0.0f) ? 0.0f : w, (h < 0.0f) ? 0.0f : h});
    ClampDesiredSize();
}

void Image::Render(const DrawingContext& dc) {
    if (!bitmap_) return;

    ImageDrawRects r = ComputeImageDrawRects(nativeSize_, Bounds(), stretch_);
    if (r.isEmpty()) return;

    D2D1_RECT_F dest = {r.dest.x,             r.dest.y,
                        r.dest.x + r.dest.w,  r.dest.y + r.dest.h};
    D2D1_RECT_F src  = {r.src.x,              r.src.y,
                        r.src.x  + r.src.w,   r.src.y  + r.src.h};

    // DrawBitmap accepts an opacity parameter directly — no need for a layer.
    dc.Dc()->DrawBitmap(bitmap_.Get(), dest, dc.Opacity(),
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
}

void Image::LoadImage() {
    ClearImage();

    if (!Context().window) {
        TraceMsg(kTag, "LoadImage: not attached yet");
        return;
    }
    ID2D1DeviceContext* dc = Context().window->D2D().DC();
    if (!dc) {
        TraceMsg(kTag, "LoadImage: D2D device context unavailable");
        return;
    }

    // Device is available — mark attempted so Measure doesn't retry every frame.
    loadAttempted_ = true;

    // --- Path: caller supplied a WIC source directly. -----------------------
    if (pendingWic_) {
        ComPtr<IWICFormatConverter> conv;
        IWICImagingFactory* wic = GetWICFactory();
        if (wic) {
            HRESULT hr = wic->CreateFormatConverter(conv.GetAddressOf());
            if (SUCCEEDED(hr)) {
                hr = conv->Initialize(pendingWic_.Get(),
                                      GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapDitherTypeNone, nullptr, 0.0,
                                      WICBitmapPaletteTypeMedianCut);
            }
            if (SUCCEEDED(hr)) {
                UINT pw = 0, ph = 0;
                pendingWic_->GetSize(&pw, &ph);
                double rx = 96.0, ry = 96.0;
                ComPtr<IWICBitmapFrameDecode> frame;
                if (SUCCEEDED(pendingWic_.As(&frame)))
                    frame->GetResolution(&rx, &ry);
                nativeSize_.w = pw * 96.0f / static_cast<float>(rx);
                nativeSize_.h = ph * 96.0f / static_cast<float>(ry);
                hr = dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr,
                                                   bitmap_.GetAddressOf());
                if (FAILED(hr)) { Trace(kTag, "CreateBitmapFromWicBitmap(wic) failed", hr); ClearImage(); }
            }
        }
        return;
    }

    // --- Path: file path. ---------------------------------------------------
    if (source_.empty()) return;

    IWICImagingFactory* wic = GetWICFactory();
    if (!wic) {
        // No factory means COM was never initialized (or WIC is unregistered) —
        // the app's fault, not the file's. Report it through the SAME channel as a
        // decode failure: a silent return here is indistinguishable from "the
        // control was never asked to load anything", which is exactly what made
        // a missing CoInitializeEx in the demo take four debugging rounds to
        // find. The fallback is attempted too, since it may not need the factory
        // on a future code path and trying costs nothing.
        TraceMsg(kTag, "WIC factory unavailable (is COM initialized?)");
        ImageFailedArgs args{source_};
        imageFailed_.Raise(*this, args);
        LoadFallback();
        return;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic->CreateDecoderFromFilename(
        source_.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateDecoder failed", hr);
        ImageFailedArgs args{source_};
        imageFailed_.Raise(*this, args);
        LoadFallback();
        return;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "GetFrame failed", hr);
        ImageFailedArgs args{source_};
        imageFailed_.Raise(*this, args);
        LoadFallback();
        return;
    }

    ComPtr<IWICFormatConverter> conv;
    hr = wic->CreateFormatConverter(conv.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateFormatConverter failed", hr);
        ImageFailedArgs args{source_};
        imageFailed_.Raise(*this, args);
        LoadFallback();
        return;
    }

    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                          WICBitmapDitherTypeNone, nullptr, 0.0,
                          WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        Trace(kTag, "FormatConverter::Initialize failed", hr);
        ImageFailedArgs args{source_};
        imageFailed_.Raise(*this, args);
        LoadFallback();
        return;
    }

    double dpiX = 96.0, dpiY = 96.0;
    frame->GetResolution(&dpiX, &dpiY);
    UINT pw = 0, ph = 0;
    conv->GetSize(&pw, &ph);
    nativeSize_.w = pw * 96.0f / static_cast<float>(dpiX);
    nativeSize_.h = ph * 96.0f / static_cast<float>(dpiY);

    hr = dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bitmap_.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateBitmapFromWicBitmap(path) failed", hr);
        ClearImage();
        ImageFailedArgs args{source_};
        imageFailed_.Raise(*this, args);
        LoadFallback();
        return;
    }

    FL_TRACEF(kTag, "loaded %ux%u px (%.1fx%.1f DIP)", pw, ph, nativeSize_.w, nativeSize_.h);
}

// ---------------------------------------------------------------------------
// LoadFallback — the P1-18 placeholder path.
//
// Deliberately NOT recursive into LoadImage: routing the fallback through the
// same function would re-raise ImageFailed for the fallback's own failure (the
// app would see two failures for one broken image, the second naming a path it
// never asked for), and a failureSource_ that equals source_ would loop.
//
// It also does not fire ImageFailed and has no fallback of its own — a broken
// placeholder draws nothing, which is the same outcome as having no placeholder
// at all. That is the correct floor: the alternative is an error path that can
// itself fail, and nothing useful to do when it does.
// ---------------------------------------------------------------------------
void Image::LoadFallback() {
    if (failureSource_.empty() || fallbackAttempted_) return;
    fallbackAttempted_ = true;

    // The caller already verified the device; re-fetch rather than threading it
    // through, so this reads as a standalone load.
    if (!Context().window) return;
    ID2D1DeviceContext* dc = Context().window->D2D().DC();
    if (!dc) return;

    IWICImagingFactory* wic = GetWICFactory();
    if (!wic) return;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic->CreateDecoderFromFilename(
        failureSource_.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) { Trace(kTag, "fallback CreateDecoder failed", hr); return; }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) { Trace(kTag, "fallback GetFrame failed", hr); return; }

    ComPtr<IWICFormatConverter> conv;
    hr = wic->CreateFormatConverter(conv.GetAddressOf());
    if (FAILED(hr)) { Trace(kTag, "fallback CreateFormatConverter failed", hr); return; }

    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                          WICBitmapDitherTypeNone, nullptr, 0.0,
                          WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) { Trace(kTag, "fallback Initialize failed", hr); return; }

    double dpiX = 96.0, dpiY = 96.0;
    frame->GetResolution(&dpiX, &dpiY);
    UINT pw = 0, ph = 0;
    conv->GetSize(&pw, &ph);
    nativeSize_.w = pw * 96.0f / static_cast<float>(dpiX);
    nativeSize_.h = ph * 96.0f / static_cast<float>(dpiY);

    hr = dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bitmap_.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "fallback CreateBitmapFromWicBitmap failed", hr);
        ClearImage();
        return;
    }

    FL_TRACEF(kTag, "fallback loaded %ux%u px", pw, ph);
}

} // namespace fluent
