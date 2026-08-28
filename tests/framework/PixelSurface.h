// PixelSurface.h — an offscreen D2D render target the tests can read pixels back
// from, so "what actually got painted" becomes an assertable value.
//
// WHY THIS EXISTS. Every visual test in this repo up to now was geometry-only: it
// compared VisualOverflowDip() against the rect a control's Render *should* touch,
// computed by hand in the test. That catches an under-declared overflow, and it
// caught the Hyperlink and Expander ones. It cannot catch the class of bug the user
// photographed: a focus ring whose declared overflow is correct but which is then
// CLIPPED AWAY by an ancestor's ClipGuard before it reaches the surface. Geometry
// says the ring is fine; the pixels say three of its four edges are missing. Only a
// real device context can tell those apart.
//
// The tests stay headless in the sense that matters — no HWND, no window, no message
// loop, no DirectComposition. D2DContext::Initialize() creates a D3D11 device with no
// swap chain and no window handle (falling back to WARP when there is no GPU, which
// is what makes this work on a build agent), and ID2D1DeviceContext can target a
// plain bitmap. So this is an offscreen rasterizer, not a UI harness.
//
// COORDINATES. The target is created at 1.0 DPI scale and the DrawingContext is
// built with dpiScale 1.0, so one DIP is one pixel and a test can name pixel
// coordinates directly from the DIP bounds it assigned. Do not "improve" this by
// scaling: the point of the harness is that the assertion coordinates are readable.
//
// AVAILABILITY. Valid() is false when no D2D device could be created at all (no
// D3D11 runtime on the machine, WARP unavailable). A test MUST check Valid() and
// skip rather than fail — a CI box without a graphics stack is not a product defect.
// Say so in the test output so a skip is never mistaken for a pass.
#pragma once

#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/graphics/DrawingContext.h"
#include "../../FluentUI/fl_common.h"

#include <d2d1_1.h>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace fltest {

// One BGRA pixel as read back off the surface. Straight alpha (D2D stores
// premultiplied; ReadBack un-premultiplies so a test can compare against the theme
// token's channel values directly).
struct Pixel {
    uint8_t b = 0, g = 0, r = 0, a = 0;

    bool operator==(const Pixel& o) const {
        return b == o.b && g == o.g && r == o.r && a == o.a;
    }
    // Channel-wise closeness. Antialiasing means an exact match on a stroked edge
    // is the wrong assertion; every real check here is "near this color" or
    // "distinguishably not the background".
    bool Near(const Pixel& o, int tol) const {
        auto d = [](uint8_t x, uint8_t y) {
            return x > y ? int(x) - int(y) : int(y) - int(x);
        };
        return d(b, o.b) <= tol && d(g, o.g) <= tol && d(r, o.r) <= tol &&
               d(a, o.a) <= tol;
    }
};

class PixelSurface {
public:
    // Creates the device stack and a `w` x `h` offscreen target. Both dimensions are
    // pixels == DIPs (see the coordinates note above).
    PixelSurface(int w, int h) : w_(w), h_(h) {
        if (FAILED(d2d_.Initialize())) return;
        if (FAILED(dwrite_.Initialize())) return;

        ID2D1DeviceContext* dc = d2d_.DC();
        if (!dc) return;

        // CPU_READ is what makes this a test harness rather than a renderer: the
        // target bitmap has to be mappable. D2D requires the read-back bitmap to be
        // a separate CANNOT_DRAW + CPU_READ bitmap, so we draw into a GPU target and
        // CopyFromBitmap into the staging one in ReadBack.
        D2D1_BITMAP_PROPERTIES1 targetProps = {};
        targetProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                   D2D1_ALPHA_MODE_PREMULTIPLIED);
        targetProps.bitmapOptions =
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        targetProps.dpiX = 96.0f;
        targetProps.dpiY = 96.0f;

        D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h));
        if (FAILED(dc->CreateBitmap(size, nullptr, 0, &targetProps,
                                    target_.ReleaseAndGetAddressOf())))
            return;

        D2D1_BITMAP_PROPERTIES1 stagingProps = targetProps;
        stagingProps.bitmapOptions =
            D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;
        if (FAILED(dc->CreateBitmap(size, nullptr, 0, &stagingProps,
                                    staging_.ReleaseAndGetAddressOf())))
            return;

        // The one shared solid brush, exactly as the host creates per frame — this
        // is what DrawingContext wraps, and drawing through anything else would not
        // be testing the real path.
        if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1),
                                             brush_.ReleaseAndGetAddressOf())))
            return;

        valid_ = true;
    }

    bool Valid() const { return valid_; }
    int Width() const { return w_; }
    int Height() const { return h_; }

    fluent::DWriteContext* Dwrite() { return valid_ ? &dwrite_ : nullptr; }

    // Render `fn` into the surface after clearing it to `bg`. `fn` receives a
    // DrawingContext built the way NativeWindowHost builds one: real device context,
    // the shared brush, dpiScale 1.0.
    //
    // `clipHint` mirrors the partial-redraw region the host passes. Pass nullptr for
    // a full-frame paint; pass a rect to reproduce a partial redraw, which is how a
    // residue/ghosting test reproduces the real failing frame.
    template <class Fn>
    void Paint(const Pixel& bg, Fn&& fn, const fluent::RectDip* clipHint = nullptr) {
        if (!valid_) return;
        ID2D1DeviceContext* dc = d2d_.DC();

        dc->SetTarget(target_.Get());
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->Clear(D2D1::ColorF(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f,
                               bg.a / 255.0f));
        {
            fluent::DrawingContext gc(dc, brush_.Get(), 1.0f, nullptr, clipHint, 1.0f);
            fn(gc);
        }
        dc->EndDraw();
        dc->SetTarget(nullptr);
    }

    // Copy the target into the staging bitmap and map it. Call once after Paint,
    // then query with At(). Returns false if the copy or map failed.
    bool ReadBack() {
        if (!valid_) return false;
        if (FAILED(staging_->CopyFromBitmap(nullptr, target_.Get(), nullptr)))
            return false;

        D2D1_MAPPED_RECT mapped = {};
        if (FAILED(staging_->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;

        pixels_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_), Pixel{});
        for (int y = 0; y < h_; ++y) {
            const uint8_t* row = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
            for (int x = 0; x < w_; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4;
                Pixel out;
                out.a = p[3];
                // Un-premultiply so a test can compare against a token's RGB
                // directly. a == 0 carries no color information at all.
                if (out.a == 0) {
                    out.b = out.g = out.r = 0;
                } else {
                    auto un = [&](uint8_t c) {
                        int v = (int(c) * 255 + out.a / 2) / out.a;
                        return static_cast<uint8_t>(v > 255 ? 255 : v);
                    };
                    out.b = un(p[0]);
                    out.g = un(p[1]);
                    out.r = un(p[2]);
                }
                pixels_[static_cast<size_t>(y) * w_ + x] = out;
            }
        }
        staging_->Unmap();
        return true;
    }

    // Pixel at (x, y). Out-of-range reads return a fully transparent pixel rather
    // than asserting: a test that walks an edge may legitimately step outside.
    Pixel At(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return Pixel{};
        return pixels_[static_cast<size_t>(y) * w_ + x];
    }

private:
    int w_ = 0, h_ = 0;
    bool valid_ = false;
    fluent::D2DContext d2d_;
    fluent::DWriteContext dwrite_;
    fluent::ComPtr<ID2D1Bitmap1> target_;
    fluent::ComPtr<ID2D1Bitmap1> staging_;
    fluent::ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<Pixel> pixels_;
};

// A test that needs a device but has none must say so out loud. Returning true here
// means "skip the body"; the printf is deliberate — a silent skip in a 1400-test
// suite is indistinguishable from a pass, which is exactly how a rendering
// regression ships past a green run.
inline bool SkipIfNoDevice(const PixelSurface& s, const char* testName) {
    if (s.Valid()) return false;
    std::printf("  [SKIP] %s: no D2D device available on this machine\n", testName);
    return true;
}

} // namespace fltest
