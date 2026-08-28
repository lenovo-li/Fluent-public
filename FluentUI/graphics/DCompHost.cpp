// DCompHost.cpp — DComp device/target/visual + content virtual surface.
// See header and global project documentation for the pitfalls this code guards against.

#include "DCompHost.h"

#pragma comment(lib, "dcomp.lib")

namespace fluent {

namespace {
const char* kTag = "DCompHost";
} // namespace

DCompHost::~DCompHost() = default;

HRESULT DCompHost::Initialize(D2DContext* d2d, HWND hwnd, UINT pixelW,
                              UINT pixelH) {
    d2d_ = d2d;
    pixelW_ = pixelW ? pixelW : 1;
    pixelH_ = pixelH ? pixelH : 1;

    // 1) DComp device + target (created once, reused for the window's life).
    //    CreateTargetForHwnd lives on IDCompositionDesktopDevice.
    // Create the DComp device from the D2D DEVICE (not the DXGI device). This is
    // load-bearing: a DComp device created from a DXGI device hands back only
    // IDXGISurface from IDCompositionSurface::BeginDraw, so drawing a composition
    // surface with Direct2D fails with E_NOINTERFACE. Binding it to the D2D device
    // makes BeginDraw return an ID2D1DeviceContext — required now that the WINDOW
    // CONTENT itself is a DComp surface drawn with D2D.
    ComPtr<IDCompositionDesktopDevice> desktop;
    FL_RETURN_IF_FAILED(
        kTag, DCompositionCreateDevice2(
                  d2d_->D2DDevice(), IID_PPV_ARGS(desktop.ReleaseAndGetAddressOf())));
    FL_RETURN_IF_FAILED(kTag, desktop.As(&device_));
    FL_RETURN_IF_FAILED(
        kTag, desktop->CreateTargetForHwnd(hwnd, TRUE,
                                           target_.ReleaseAndGetAddressOf()));
    FL_RETURN_IF_FAILED(
        kTag, device_->CreateVisual(rootVisual_.ReleaseAndGetAddressOf()));
    FL_RETURN_IF_FAILED(
        kTag, device_->CreateVisual(contentVisual_.ReleaseAndGetAddressOf()));
    FL_RETURN_IF_FAILED(kTag, target_->SetRoot(rootVisual_.Get()));
    FL_RETURN_IF_FAILED(kTag, rootVisual_->AddVisual(contentVisual_.Get(),
                                                     FALSE, nullptr));

    // 2) Content surface: a virtual surface (resizable in place, no realloc on
    //    resize). Premultiplied alpha so Mica shows through where nothing is drawn.
    FL_RETURN_IF_FAILED(
        kTag, device_->CreateVirtualSurface(
                  pixelW_, pixelH_, DXGI_FORMAT_B8G8R8A8_UNORM,
                  DXGI_ALPHA_MODE_PREMULTIPLIED,
                  contentSurface_.ReleaseAndGetAddressOf()));

    // 3) Bind the surface to the content visual and draw the first real frame.
    //    Unlike a swap chain (which composites an empty white buffer if bound
    //    before the first draw), a DComp surface starts transparent, so binding
    //    first is safe. Draw one frame + Commit so the initial content shows.
    FL_RETURN_IF_FAILED(kTag, contentVisual_->SetContent(contentSurface_.Get()));
    contentBound_ = true;

    ID2D1DeviceContext* dc = nullptr;
    POINT off = {};
    FL_RETURN_IF_FAILED(kTag, BeginContentFrame(&dc, &off));
    FL_RETURN_IF_FAILED(kTag, EndContentFrame());
    FL_RETURN_IF_FAILED(kTag, Commit());

    TraceMsg(kTag, "initialized (DComp surface content)");
    return S_OK;
}

HRESULT DCompHost::BeginContentFrame(ID2D1DeviceContext** outDc,
                                     POINT* outNetOffset, bool fullClear,
                                     const RECT* updatePx) {
    if (!contentSurface_) return E_FAIL;
    POINT atlas = {};
    ComPtr<ID2D1DeviceContext> target;
    // BeginDraw returns a context bound to an atlas tile + the tile origin. With a
    // partial update rect, drawing is scoped to that region and its top-left maps
    // to the tile origin, so the NET translation is atlasOrigin - updateTopLeft.
    RECT ur = {};
    const RECT* urp = nullptr;
    LONG ux = 0, uy = 0;
    if (updatePx) {
        ur = *updatePx;
        urp = &ur;
        ux = ur.left;
        uy = ur.top;
    }
    HRESULT hr = contentSurface_->BeginDraw(
        urp, IID_PPV_ARGS(target.GetAddressOf()), &atlas);
    if (FAILED(hr)) {
        Trace(kTag, "content surface BeginDraw FAILED", hr);
        return hr;
    }
    const POINT net{atlas.x - ux, atlas.y - uy};
    if (outNetOffset) *outNetOffset = net;
    // Seed the transform with the net translation so a caller that draws in surface
    // space lands correctly. A caller with its own transform (DPI scale) composes
    // Scale(s) * Translation(net) using outNetOffset.
    target->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(net.x), static_cast<float>(net.y)));
    // Premultiplied-alpha target (Mica shows through) → ClearType is unavailable;
    // set grayscale AA explicitly to skip D2D's detection pass.
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    // Clear the updated region. On a partial frame the D2D context is clipped to
    // the update rect by BeginDraw, so Clear only affects that region; the rest of
    // the persistent surface keeps last frame's pixels.
    if (fullClear) {
        target->Clear(D2D1::ColorF(0, 0, 0, 0));
    }
    // Hold the per-frame context so the caller borrows it (does NOT own/release) —
    // same contract as the old shared-DC path. EndContentFrame releases it.
    frameDc_ = target;
    if (outDc) *outDc = frameDc_.Get();
    return S_OK;
}

HRESULT DCompHost::EndContentFrame(const RECT* /*dirtyRects*/, UINT /*count*/) {
    if (!contentSurface_) return E_FAIL;
    // Reset the transform we seeded and drop our ref to the per-frame context, then
    // EndDraw finalizes the tile. No Present — Commit() publishes it.
    if (frameDc_) frameDc_->SetTransform(D2D1::Matrix3x2F::Identity());
    frameDc_.Reset();
    HRESULT hr = contentSurface_->EndDraw();
    if (FAILED(hr))
        Trace(kTag, "content surface EndDraw FAILED", hr);
    return hr;
}

HRESULT DCompHost::Resize(UINT pixelW, UINT pixelH) {
    pixelW_ = pixelW ? pixelW : 1;
    pixelH_ = pixelH ? pixelH : 1;
    if (!contentSurface_) return E_FAIL;
    // Virtual surface resizes in place (no back-buffer realloc / rebind). The new
    // size is published on the next Commit, in the SAME batch as the content
    // redraw and every child visual's transform — so a resize never shows the
    // content surface and the child visuals at momentarily inconsistent sizes.
    HRESULT hr = contentSurface_->Resize(pixelW_, pixelH_);
    if (FAILED(hr))
        Trace(kTag, "content surface Resize FAILED", hr);
    return hr;
}

HRESULT DCompHost::AddChildVisual(IDCompositionVisual2* visual) {
    if (!visual || !rootVisual_) return E_INVALIDARG;
    // FALSE + NULL reference = render ABOVE all existing children, i.e. over the
    // content surface. (With a NULL reference the flag is inverted from its
    // name: TRUE would put it at the BOTTOM, hidden behind the content.)
    return rootVisual_->AddVisual(visual, FALSE, nullptr);
}

HRESULT DCompHost::RemoveChildVisual(IDCompositionVisual2* visual) {
    if (!visual || !rootVisual_) return E_INVALIDARG;
    return rootVisual_->RemoveVisual(visual);
}

HRESULT DCompHost::Commit() {
    HRESULT hr = device_->Commit();
    if (FAILED(hr))
        Trace(kTag, "Commit FAILED", hr);
    return hr;
}

void DCompHost::Reset() {
    // Release in reverse dependency order. The content surface and DComp visuals /
    // target / device are all derived from the (about-to-be-rebuilt) device.
    frameDc_.Reset();
    contentSurface_.Reset();
    contentVisual_.Reset();
    rootVisual_.Reset();
    target_.Reset();
    device_.Reset();
    contentBound_ = false;
}

} // namespace fluent
