// GraphicsHost.h — D3D/D2D/DComp device lifetime and surface management.
//
// Extracted from NativeWindowHost as part of the Host refactoring (Phase 1).
// Owns the graphics device stack: D2DContext, DWriteContext, DCompHost, the
// composition backend, resource cache, and device-lost recovery. The window
// no longer directly holds these — it composes GraphicsHost and delegates.
#pragma once

#include "../fl_common.h"
#include "../graphics/D2DContext.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/DCompHost.h"
#include "../composition/DCompCompositionBackend.h"
#include "../graphics/ResourceCache.h"
#include <functional>
#include <memory>

namespace fluent {

class GraphicsHost {
public:
    GraphicsHost() = default;
    ~GraphicsHost() = default;

    // Non-copyable (holds unique device resources)
    GraphicsHost(const GraphicsHost&) = delete;
    GraphicsHost& operator=(const GraphicsHost&) = delete;

    // Initialize the D3D/D2D/DComp device stack for the given HWND and surface size.
    // Also creates the composition backend with the given commit callback.
    // Must be called before any rendering operations.
    HRESULT Initialize(HWND hwnd, UINT pixelW, UINT pixelH,
                       std::function<void()> requestCommit);

    // Reset devices (called before recovery or shutdown). Does NOT recreate.
    void Reset();

    // Rebuild the device stack after device lost. Returns S_OK on success.
    // The composition backend is NOT recreated (it holds borrowed pointers).
    // The resource cache must be cleared by the caller before calling this.
    HRESULT Rebuild(HWND hwnd, UINT pixelW, UINT pixelH);

    // Accessors for subsystems (const and non-const).
    D2DContext& D2D() { return d2d_; }
    const D2DContext& D2D() const { return d2d_; }

    DWriteContext& DWrite() { return dwrite_; }
    const DWriteContext& DWrite() const { return dwrite_; }

    DCompHost& Comp() { return comp_; }
    const DCompHost& Comp() const { return comp_; }

    DCompCompositionBackend* CompBackend() { return compBackend_.get(); }
    const DCompCompositionBackend* CompBackend() const { return compBackend_.get(); }

    ResourceCache& Cache() { return resourceCache_; }
    const ResourceCache& Cache() const { return resourceCache_; }

    // Check if devices are initialized and ready for rendering.
    bool IsReady() const { return d2d_.Valid(); }

private:
    D2DContext d2d_;
    DWriteContext dwrite_;
    DCompHost comp_;
    std::unique_ptr<DCompCompositionBackend> compBackend_;
    ResourceCache resourceCache_;
};

} // namespace fluent
