// DCompHost.h — DirectComposition device/target/root visual + the window
// content SURFACE.
//
// The window content is a DirectComposition virtual surface (NOT a swap chain).
// This is deliberate: with a swap chain, the content update (Present1, on the
// DXGI queue) and the child visuals' transforms (Commit, on the composition tree)
// publish through two different paths that can land on different vsyncs — during
// a modal resize the background and an independently-composited child (e.g. a
// TreeView on its own scroll visual) then desync and shake. Drawing the content
// into a DComp surface means the content redraw AND every child visual's
// transform publish in the SAME Commit(), so they can never disagree.
//
// Follows the remaining DComp pitfalls in the global project documentation:
//   * the window must use WS_EX_NOREDIRECTIONBITMAP (caller's job),
//   * Device/Target/Visuals created once, never rebuilt on resize (the virtual
//     surface resizes in place),
//   * the DComp device is created from the D2D device so surface BeginDraw hands
//     back an ID2D1DeviceContext (not IDXGISurface),
//   * every critical call traces its HRESULT.
//
// The content surface is premultiplied alpha, cleared to transparent so the DWM
// Mica backdrop shows through where no control is drawn. Child visuals (an
// indeterminate ProgressBar's sweep, a TreeView's overscan scroll layer) are
// composited above the content and animate on the compositor thread.
#pragma once

#include "../fl_common.h"
#include "D2DContext.h"
#include <dcomp.h>

namespace fluent {

class DCompHost {
public:
    ~DCompHost();

    // hwnd must have been created with WS_EX_NOREDIRECTIONBITMAP.
    HRESULT Initialize(D2DContext* d2d, HWND hwnd, UINT pixelW, UINT pixelH);

    // Frame pacing handle. The content is now a DirectComposition surface, not a
    // swap chain, so there is no DXGI frame-latency waitable — always nullptr. The
    // message loop already treats nullptr as "render on demand, no pacing object"
    // (the waitable had been disabled anyway). Kept for source compatibility.
    HANDLE FrameLatencyWaitable() const { return nullptr; }

    // Begin a frame on the content DComp surface. Returns a D2D device context
    // bound to the surface (via IDCompositionSurface::BeginDraw) plus the NET
    // translation the caller must fold into its transform.
    //
    // `outNetOffset` (required): DComp hands back an atlas tile; when a partial
    //   update rect is supplied, drawing is also shifted so update-rect-top-left
    //   maps to the tile origin. This point is the combined translation
    //   (atlasOffset - updateOrigin) the caller composes as
    //   `Scale(dpi) * Translation(netOffset)` so DIP coordinates land correctly.
    //   The returned context already has `Translation(netOffset)` applied for a
    //   caller that draws in surface space and sets no transform of its own.
    // `fullClear`: true clears the whole updated region to transparent (Mica shows
    //   through). false leaves prior content — a DComp surface is PERSISTENT
    //   (unlike a flip swap chain), so pixels outside `updatePx` keep last frame's
    //   content and the caller clears only its dirty sub-region.
    // `updatePx` (optional): the dirty sub-rect in PHYSICAL PIXELS. When non-null
    //   (partial redraw), only that region is redrawn and the rest is preserved.
    //   Null = update the whole surface (full-frame redraw).
    HRESULT BeginContentFrame(ID2D1DeviceContext** outDc, POINT* outNetOffset,
                              bool fullClear = true, const RECT* updatePx = nullptr);
    // End the frame: IDCompositionSurface::EndDraw. There is no Present — the
    // change becomes visible on the next Commit(). `dirtyRects`/`count` are
    // accepted for source compatibility but unused (the update region was already
    // scoped in BeginContentFrame via `updatePx`).
    HRESULT EndContentFrame(const RECT* dirtyRects = nullptr, UINT count = 0);

    // Resize the content surface (IDCompositionVirtualSurface::Resize — no
    // reallocation, unlike recreating a fixed surface). The change is published on
    // the next Commit().
    HRESULT Resize(UINT pixelW, UINT pixelH);

    // Add / remove a child visual above the content: a control builds its own
    // composition sub-tree (e.g. a clipped viewport + a scrolled surface + a caret,
    // or an offset-animated segment for an indeterminate ProgressBar) and parents its
    // root here. Caller owns the visual and its whole sub-tree. Changes are not
    // visible until the next Commit().
    //
    // Controls do NOT call this directly — they go through ICompositionBackend, which
    // is injected via WindowServices so an embedded member (a TreeView's scroll model)
    // can reach the compositor without being a tree node. DCompCompositionBackend is
    // the only caller.
    HRESULT AddChildVisual(IDCompositionVisual2* visual);
    HRESULT RemoveChildVisual(IDCompositionVisual2* visual);

    // Commit the composition tree to the compositor.
    HRESULT Commit();

    // Release every device-bound object (content surface, DComp device / target /
    // visuals) for device-loss recovery (roadmap §17). After the shared D2DContext
    // is rebuilt in place, the host calls Initialize() again to recreate this
    // host's composition tree + content surface from the fresh device.
    void Reset();

    IDCompositionDevice2* Device() const { return device_.Get(); }
    UINT PixelW() const { return pixelW_; }
    UINT PixelH() const { return pixelH_; }

private:
    D2DContext* d2d_ = nullptr;
    UINT pixelW_ = 0, pixelH_ = 0;

    ComPtr<IDCompositionDevice2> device_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual2> rootVisual_;
    ComPtr<IDCompositionVisual2> contentVisual_;
    // The window content, drawn on a DirectComposition virtual surface composited
    // above Mica (premultiplied alpha). Replaces the old content swap chain so the
    // content redraw and every child visual's transform publish in the SAME
    // Commit() — no swap-chain-Present vs composition-Commit desync during resize.
    ComPtr<IDCompositionVirtualSurface> contentSurface_;
    // The per-frame D2D context returned by contentSurface_->BeginDraw, held so the
    // caller borrows a raw pointer (BeginContentFrame → EndContentFrame) without
    // owning it. Released in EndContentFrame. Null outside a frame.
    ComPtr<ID2D1DeviceContext> frameDc_;
    bool contentBound_ = false;  // SetContent(contentSurface_) already done
};

} // namespace fluent
