// D2DContext.h — Direct3D11 + DXGI + Direct2D device stack, DPI aware.
//
// One D2D device context is shared by the whole UI. Each window's DirectComposition
// device is created from THIS D2D device (see DCompHost) — that is required, not
// stylistic: a DComp device built from the DXGI device hands back an IDXGISurface from
// surface BeginDraw instead of an ID2D1DeviceContext (E_NOINTERFACE). SetDpi() must be
// called whenever the target DPI changes so D2D scales DIPs to pixels.
#pragma once

#include "../fl_common.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>

namespace fluent {

class D2DContext {
public:
    HRESULT Initialize();

    ID3D11Device* D3DDevice() const { return d3dDevice_.Get(); }
    ID2D1Device* D2DDevice() const { return d2dDevice_.Get(); }
    ID2D1Factory1* D2DFactory() const { return d2dFactory_.Get(); }

    // The shared immediate device context used for all drawing.
    ID2D1DeviceContext* DC() const { return d2dContext_.Get(); }

    bool Valid() const { return d2dContext_ != nullptr; }

    // Release the whole device stack (device-loss recovery, roadmap §17). Called
    // by the host before re-Initialize()ing IN PLACE so borrowers that hold this
    // D2DContext* (PopupHost / TooltipService) keep a valid pointer across the
    // rebuild — only the COM objects inside are swapped, not the D2DContext.
    void Reset() {
        d2dContext_.Reset();
        d2dDevice_.Reset();
        d2dFactory_.Reset();
        dxgiDevice_.Reset();
        d3dImmediate_.Reset();
        d3dDevice_.Reset();
    }

    // Read the reason a device was removed, for the recovery trace (roadmap §17).
    // S_OK if the device is still valid or not yet created.
    HRESULT DeviceRemovedReason() const {
        return d3dDevice_ ? d3dDevice_->GetDeviceRemovedReason() : S_OK;
    }

private:
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dImmediate_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
};

} // namespace fluent
