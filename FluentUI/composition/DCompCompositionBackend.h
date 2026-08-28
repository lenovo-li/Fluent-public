// DCompCompositionBackend.h — the real DirectComposition implementation of
// ICompositionBackend (Phase 2).
//
// Wraps the window's existing DComp stack: visuals are IDCompositionVisual2/3,
// surfaces are IDCompositionSurface drawn with the shared D2D device context,
// sweeps are the IDCompositionAnimation from MakeOffsetSweep, and the root
// parenting / commit go through DCompHost. Controls use the interface and never
// name DComp types directly.
//
// Lifetime: the backend holds only borrowed pointers (DCompHost, D2DContext) and
// a commit callback. Those outlive every visual, and a device-loss rebuild
// happens in place (DCompHost::Reset + Initialize), so the backend itself is
// created once and never rebuilt; controls drop/recreate their own visuals
// across device loss via their OnDeviceLost/OnDeviceRestored hooks.
#pragma once

#include "ICompositionBackend.h"
#include "../fl_common.h"
#include <dcomp.h>
#include <d2d1_1.h>

namespace fluent {

class DCompHost;
class D2DContext;

// One IDCompositionVisual2 node + its optional backing surface. Created by
// DCompCompositionBackend; children are added/removed via the interface.
class DCompCompositionVisual : public ICompositionVisual {
public:
    // Takes the freshly created visual (and its IDCompositionVisual3 QI, if
    // available, for opacity) plus the device (to create surfaces) and the shared
    // D2D context (to draw them). All borrowed except the visual refs it holds.
    DCompCompositionVisual(ComPtr<IDCompositionVisual2> visual,
                           IDCompositionDevice2* device, D2DContext* d2d);
    ~DCompCompositionVisual() override;

    void SetOffset(float x, float y) override;
    void SetOpacity(float opacity) override;
    void SetClip(float left, float top, float right, float bottom) override;
    void ClearClip() override;
    void StartOffsetSweep(const SweepSpec& spec) override;
    void StartOffsetYTween(float fromPx, float toPx, double durationSec) override;
    void StartOpacityBlink(double halfPeriodSec) override;
    void StopAnimation(CompositionProperty property) override;
    bool DrawSurface(uint32_t pixelW, uint32_t pixelH,
                     const DrawCallback& draw) override;
    void AddChild(ICompositionVisual* child) override;
    void RemoveChild(ICompositionVisual* child) override;

    // The underlying visual, for the backend's root add/remove.
    IDCompositionVisual2* Raw() const { return visual_.Get(); }

private:
    ComPtr<IDCompositionVisual2> visual_;
    ComPtr<IDCompositionVisual3> visual3_;      // opacity animation (Win8.1+)
    ComPtr<IDCompositionSurface> surface_;
    IDCompositionDevice2* device_ = nullptr;    // borrowed (for CreateSurface/animation)
    D2DContext* d2d_ = nullptr;                 // borrowed (surface draw context)
    uint32_t surfaceW_ = 0, surfaceH_ = 0;      // current surface pixel size
};

// The factory + root, backed by DCompHost. RequestCommit forwards to a callback
// (the window schedules a frame whose tail Commit() publishes the changes).
class DCompCompositionBackend : public ICompositionBackend {
public:
    DCompCompositionBackend(DCompHost* host, D2DContext* d2d,
                            std::function<void()> requestCommit);

    std::unique_ptr<ICompositionVisual> CreateVisual() override;
    void AddToRoot(ICompositionVisual* visual) override;
    void RemoveFromRoot(ICompositionVisual* visual) override;
    void RequestCommit() override;

private:
    DCompHost* host_ = nullptr;   // borrowed; owns device + root visual
    D2DContext* d2d_ = nullptr;   // borrowed; surface draw context
    std::function<void()> requestCommit_;
};

} // namespace fluent
