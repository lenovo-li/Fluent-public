// D2DContext.cpp — create the D3D11/DXGI/D2D device stack.

#include "D2DContext.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

namespace fluent {

namespace {
const char* kTag = "D2DContext";
}

HRESULT D2DContext::Initialize() {
    // BGRA support is required for D2D interop. Single-threaded: all drawing
    // happens on the UI thread; DComp does the GPU compositing off-thread.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    // Only request the debug layer if it is installed; otherwise device
    // creation fails on machines without the Graphics Tools feature.
    // We try with the flag and fall back below if it fails.
#endif

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
        ARRAYSIZE(levels), D3D11_SDK_VERSION,
        d3dDevice_.ReleaseAndGetAddressOf(), nullptr,
        d3dImmediate_.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
        // Fall back to the WARP software renderer (RDP / no GPU).
        Trace(kTag, "hardware D3D11CreateDevice failed, trying WARP", hr);
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
            ARRAYSIZE(levels), D3D11_SDK_VERSION,
            d3dDevice_.ReleaseAndGetAddressOf(), nullptr,
            d3dImmediate_.ReleaseAndGetAddressOf());
    }
    FL_RETURN_IF_FAILED(kTag, hr);

    FL_RETURN_IF_FAILED(kTag, d3dDevice_.As(&dxgiDevice_));

    D2D1_FACTORY_OPTIONS opts = {};
#ifdef _DEBUG
    opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory1), &opts,
                           reinterpret_cast<void**>(
                               d2dFactory_.ReleaseAndGetAddressOf()));
    FL_RETURN_IF_FAILED(kTag, hr);

    FL_RETURN_IF_FAILED(
        kTag, d2dFactory_->CreateDevice(dxgiDevice_.Get(),
                                        d2dDevice_.ReleaseAndGetAddressOf()));

    FL_RETURN_IF_FAILED(
        kTag, d2dDevice_->CreateDeviceContext(
                  D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                  d2dContext_.ReleaseAndGetAddressOf()));

    TraceMsg(kTag, "initialized");
    return S_OK;
}

} // namespace fluent
