// GraphicsHost.cpp
#include "GraphicsHost.h"
#include "../fl_common.h"

namespace fluent {

static constexpr const char* kTag = "GraphicsHost";

HRESULT GraphicsHost::Initialize(HWND hwnd, UINT pixelW, UINT pixelH,
                                  std::function<void()> requestCommit) {
    // NativeWindowHost::InitGraphics order (line 297-310):
    // 1. d2d_.Initialize()
    // 2. dwrite_.Initialize()
    // 3. comp_.Initialize(&d2d_, hwnd, pixelW, pixelH)
    // 4. compBackend_ = make_unique<DCompCompositionBackend>(&comp_, &d2d_, callback)
    // 5. resourceCache_.Initialize(&dwrite_, d2d_.D2DFactory())

    FL_RETURN_IF_FAILED(kTag, d2d_.Initialize());
    FL_RETURN_IF_FAILED(kTag, dwrite_.Initialize());
    FL_RETURN_IF_FAILED(kTag, comp_.Initialize(&d2d_, hwnd, pixelW, pixelH));

    // Composition backend: create once. Holds borrowed pointers (&comp_, &d2d_),
    // so a later device-loss rebuild (Reset + Initialize in place) does not
    // invalidate it — no need to recreate it during recovery.
    if (!compBackend_) {
        compBackend_ = std::make_unique<DCompCompositionBackend>(
            &comp_, &d2d_, std::move(requestCommit));
    }

    // Shared resource caches: DWrite factory (text layout) + D2D factory (icon
    // geometry / stroke styles). Re-pointed after device rebuild.
    resourceCache_.Initialize(&dwrite_, d2d_.D2DFactory());

    return S_OK;
}

void GraphicsHost::Reset() {
    // NativeWindowHost::RecoverDevice order (line 996-997):
    // comp_.Reset(), d2d_.Reset(), then rebuild
    comp_.Reset();
    d2d_.Reset();
    // Note: resourceCache_.Clear() and compBackend_.reset() are NOT called here —
    // the caller (RecoverDevice) does resourceCache_.Clear() before Reset(),
    // and compBackend_ is NOT recreated (it holds borrowed pointers that remain valid).
}

HRESULT GraphicsHost::Rebuild(HWND hwnd, UINT pixelW, UINT pixelH) {
    // NativeWindowHost::RecoverDevice rebuild (line 999-1009):
    // 1. d2d_.Initialize()
    // 2. comp_.Initialize(&d2d_, hwnd, pixelW, pixelH)
    // 3. resourceCache_.Initialize(&dwrite_, d2d_.D2DFactory())
    // Note: compBackend_ is NOT recreated

    HRESULT hr = d2d_.Initialize();
    if (SUCCEEDED(hr))
        hr = comp_.Initialize(&d2d_, hwnd, pixelW, pixelH);
    if (FAILED(hr)) {
        Trace(kTag, "device rebuild FAILED", hr);
        return hr;
    }

    // Re-point the resource cache at the fresh factories
    resourceCache_.Initialize(&dwrite_, d2d_.D2DFactory());

    Trace(kTag, "device rebuilt", S_OK);
    return S_OK;
}

} // namespace fluent
