// Image.h — bitmap display control (WIC-loaded, D2D-drawn).
//
// Loads PNG / JPG / BMP / ICO / GIF / TIFF through WIC and draws the result with
// ID2D1DeviceContext::DrawBitmap. Four stretch modes matching WPF/WinUI semantics.
//
// Two things about this control are worth knowing before changing it.
//
// **The geometry is a pure function, not a method.** ImageDrawRects computes both
// the destination rect (window DIPs) and the SOURCE rect (image DIPs) for a given
// native size / bounds / stretch mode, as a free function with no device and no
// element state. That is deliberate: this is the only part of an image control that
// has interesting logic, and a headless test cannot reach a real DrawBitmap. Render
// does nothing but hand the two rects to D2D, so testing the function tests the
// control's behavior.
//
// The source rect is what makes cropping work. `None` with an image larger than the
// control, and `UniformToFill`, both have to CROP — and a dest-rect-only clamp
// squashes the image instead (D2D stretches whatever source it is given into
// whatever dest it is given). Both modes therefore narrow the source rect and leave
// the aspect ratio intact.
//
// **Loading needs a device, so it cannot happen in SetSource.** The D2D device
// arrives with the UIContext at attach, and the ordinary usage order is
// `SetSource(...)` then `panel->Add(...)`. SetSource therefore only records the path
// and marks the load pending; the bitmap is built in OnAttachedToTree (and, as a
// backstop, on the first Measure). A file that fails to open is not an error path
// worth propagating — the control draws nothing and the reason goes to the debugger
// via [FluentUI.Image].
#pragma once

#include "../core/FrameworkElement.h"
#include "../base/Event.h"
#include <d2d1_1.h>
#include <string>

struct IWICBitmapSource;

namespace fluent {

enum class ImageStretch {
    None,           // native size, centered, cropped when larger than bounds
    Uniform,        // scale to fit; whole image visible, letterboxed
    UniformToFill,  // scale to fill; aspect preserved, overflow cropped
    Fill,           // scale both axes to the bounds; aspect not preserved
};

// The destination rect (window DIPs) and source rect (image DIPs) for one draw.
// A `src` narrower than the native size means this mode crops.
struct ImageDrawRects {
    RectDip dest;
    RectDip src;
    bool isEmpty() const { return dest.isEmpty() || src.isEmpty(); }
};

// Compute both rects for `native` drawn into `bounds` under `mode`. Pure: no
// device, no element state, unit-testable headless. Returns empty rects when
// either size is degenerate.
ImageDrawRects ComputeImageDrawRects(SizeDip native, RectDip bounds,
                                     ImageStretch mode);

class Image : public FrameworkElement {
public:
    Image();
    ~Image() override;

    // Load from a file path. The bitmap is built at attach (or first Measure),
    // not here — see the header comment.
    void SetSource(std::wstring path);
    const std::wstring& Source() const { return source_; }

    // Load from an already-decoded WIC source. Takes effect immediately when
    // attached, else at attach. Used by an app that decodes from memory/resource.
    void SetSource(IWICBitmapSource* bitmap);

    // P1-18: Fallback source when the primary source fails to load. Exposed
    // exactly the same way: a path loaded at attach/measure. Default: empty (no
    // fallback, draw nothing on failure). The placeholder does not itself have a
    // fallback: if the placeholder also fails to load, the control draws nothing.
    void SetFailureSource(std::wstring path);
    const std::wstring& FailureSource() const { return failureSource_; }

    // P1-18: Fired when the primary source fails to load (file not found, decode
    // error, device loss recovery failure). Not fired when the FALLBACK fails —
    // only the primary source is the application's signal.
    struct ImageFailedArgs { std::wstring path; };
    Event<Image, ImageFailedArgs>& ImageFailed() { return imageFailed_; }

    void SetStretch(ImageStretch s) { SetProperty(stretch_, s, DirtyFlags::Render); }
    ImageStretch Stretch() const { return stretch_; }

    // The image's own size in DIPs (pixels scaled by its embedded DPI metadata),
    // or {0,0} until a load succeeds. This is what Measure sizes to.
    SizeDip NativeSize() const { return nativeSize_; }
    bool IsLoaded() const { return bitmap_ != nullptr; }

    // True once a load has been attempted for the current source, whether or not
    // it succeeded. Exposed so a test can tell "not tried yet" from "tried and
    // failed" — the two are indistinguishable from IsLoaded() alone.
    bool LoadAttempted() const { return loadAttempted_; }

    // Public, matching the base declarations — a derived class must not narrow
    // access to a virtual the framework (and tests) call through the base.
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;

protected:
    void OnAttachedToTree() override;
    void OnDetachedFromTree() override;
    // The device the bitmap lives on is gone; drop it and rebuild on restore.
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

private:
    // Build bitmap_ from source_ (path) or pendingWic_. No-op while detached.
    // When the primary load fails and a failureSource_ is set, attempts to load
    // the fallback. Fires ImageFailed on primary failure (before trying fallback).
    void LoadImage();
    // Try loading failureSource_ as the fallback. Called only when the primary
    // load failed. Does not fire ImageFailed if the fallback itself fails — only
    // the primary source is the app's signal. Helper extracted so the failure-path
    // logic is named rather than repeated inline in two branches.
    void LoadFallback();
    void ClearImage();

    std::wstring source_;
    std::wstring failureSource_;
    ImageStretch stretch_ = ImageStretch::Uniform;
    SizeDip nativeSize_{0.0f, 0.0f};
    ComPtr<ID2D1Bitmap> bitmap_;
    // A caller-supplied WIC source held until a device is available.
    ComPtr<IWICBitmapSource> pendingWic_;
    bool loadAttempted_ = false;
    // Tracked separately from loadAttempted_ so a failed primary does not retry
    // the fallback on every Measure, while a later SetFailureSource still gets one
    // attempt. Reset by SetSource (a new primary deserves a fresh fallback try)
    // and by SetFailureSource.
    bool fallbackAttempted_ = false;
    Event<Image, ImageFailedArgs> imageFailed_;
};

} // namespace fluent
