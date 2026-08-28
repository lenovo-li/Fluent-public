// NativeWindowHost.cpp

#include "NativeWindowHost.h"
#include "../Application.h"
#include "DirtyRegion.h"  // WP-07 §S4: pure partial-redraw planner
#include "../styling/ThemeTokens.h"
#include "../diagnostics/PerformanceCounters.h"  // QpcNow for the animation clock
#include "../diagnostics/DebugHud.h"              // on-screen perf HUD formatting
#include <dwmapi.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <imm.h>
#include <wincodec.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "imm32.lib")

namespace fluent {

namespace {
const char* kTag = "NativeWindowHost";
const wchar_t* kClassName = L"FluentUI.NativeWindowHost";

// DWM backdrop / dark-mode / caption attributes now live in WindowAppearance
// (roadmap §12) — this window drives them through theme_.Appearance().

constexpr float kCaptionBtnWidthDip = 46.0f;
constexpr float kCaptionGlyphDip = 10.0f;
// WP-07 §S4: above this fraction of the window, a partial redraw is not worth the
// bookkeeping — just clear + repaint the whole frame. Below it, clip + partial
// present. 0.6 keeps the common small-change frames (hover, caret, one control)
// on the cheap path while large scrolls / theme flips take the full path.
constexpr float kPartialRedrawMaxCoverage = 0.6f;
constexpr UINT_PTR kBlinkTimerId = 1;
constexpr UINT_PTR kAnimTimerId = 2;
constexpr UINT_PTR kTooltipTimerId = 3;
constexpr UINT_PTR kSelfFrameTimerId = 4;
// Period for the LEGACY modal fallback timer (kAnimTimerId), used only when
// ModalFrameHeartbeat could not start — i.e. no waitable timer, pre-Windows 10 1803.
// 10 is USER_TIMER_MINIMUM: asking for less is silently clamped to it, so this is the
// best WM_TIMER can nominally do (~100fps). In practice it delivers far less, because
// WM_TIMER is a low-priority synthesized message whose period is rounded up to the
// system timer resolution (15.6ms default, 5ms measured here) — a 16ms request
// measured ~28.6ms, which is why a press-and-hold on a resize border used to sit at
// ~30-45fps on a 120Hz panel. That limit is intrinsic to WM_TIMER and is the whole
// reason ModalFrameHeartbeat exists; 10 merely stops this path being needlessly worse
// than the API allows.
constexpr UINT kAnimIntervalMs = 10;
constexpr UINT kTooltipDelayMs = 500;  // hover dwell before a tooltip appears
} // namespace

NativeWindowHost::~NativeWindowHost() {
    if (controlBgBrush_) DeleteObject(controlBgBrush_);
    if (hwnd_) DestroyWindow(hwnd_);
}

void NativeWindowHost::Close() {
    if (hwnd_ && !destroying_)
        DestroyWindow(hwnd_);
}

// Ask any active popup to close before the window and its elements are torn
// down (§5.5): while the elements are still alive we invoke the dismiss hook so
// the owning control runs its normal close path (hiding the popup window and
// resetting its Subscription). Idempotent — safe if no popup is open.
void NativeWindowHost::CloseActivePopups() {
    if (popupDismiss_) {
        popupDismiss_(PopupDismissReason::Deactivate, nullptr, 0, 0);
        popupDismiss_ = nullptr;
    }
    popupKey_ = nullptr;
}

HRESULT NativeWindowHost::Create(HINSTANCE hInst, const wchar_t* title,
                               float clientWDip, float clientHDip,
                               const WindowState* restore) {
    hInst_ = hInst;
    title_ = title ? title : L"";

    WNDCLASSEXW wc = {sizeof(wc)};
    // CS_DBLCLKS only. Still no CS_HREDRAW/CS_VREDRAW (project documentation #5: a DComp window
    // must not be invalidated wholesale on resize) — the two are unrelated flags and
    // adding double-click delivery does not reintroduce that problem.
    //
    // WITHOUT CS_DBLCLKS WINDOWS NEVER SENDS WM_LBUTTONDBLCLK. That is worth stating
    // here because the failure mode is silent and points at the wrong layer: the message
    // handler exists, the routing exists, the control's handler exists, and none of it
    // runs. Time gets spent debugging the click logic instead of the class registration.
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &NativeWindowHost::WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // ignore "already registered"

    // A dialog is created on its owner's monitor, so size it with that window's
    // actual DPI instead of the primary monitor's DPI.
    const HWND ownerHwnd = OwnerHwnd();
    HMONITOR mon = ownerHwnd && IsWindow(ownerHwnd)
        ? MonitorFromWindow(ownerHwnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dx = 96, dy = 96;
    if (ownerHwnd) {
        dx = GetDpiForWindow(ownerHwnd);
        dy = dx;
    } else {
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    }
    dpi_ = dx ? dx : 96;

    int w = static_cast<int>(clientWDip * dpi_ / 96.0f + 0.5f);
    int h = static_cast<int>(clientHDip * dpi_ / 96.0f + 0.5f);

    // Center on owner window if present (dialogs should appear in front of their
    // caller, not jump to the primary monitor center when the main window sits
    // elsewhere). Fall back to primary monitor center for top-level windows.
    int x = 0, y = 0;
    RECT ownerRect{};
    const bool haveOwner = ownerHwnd && IsWindow(ownerHwnd) &&
                           GetWindowRect(ownerHwnd, &ownerRect);
    if (haveOwner) {
        x = ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2;
        y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2;
        // Clamp to the owner's monitor work area so a dialog spawned from a window
        // near the screen edge doesn't spill off-screen.
        MONITORINFO omi = {sizeof(omi)};
        if (GetMonitorInfo(MonitorFromWindow(ownerHwnd, MONITOR_DEFAULTTONEAREST), &omi)) {
            x = std::clamp(x, static_cast<int>(omi.rcWork.left),
                           std::max<int>(omi.rcWork.left, omi.rcWork.right - w));
            y = std::clamp(y, static_cast<int>(omi.rcWork.top),
                           std::max<int>(omi.rcWork.top, omi.rcWork.bottom - h));
        }
    } else {
        // No owner: center on primary monitor (top-level window).
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(mon, &mi);
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    }

    RECT initialRect = {x, y, x + w, y + h};
    AdjustInitialWindowRect(initialRect, dpi_);
    // Override the centered rect with the caller-supplied saved placement.
    ApplyRestoreState(initialRect, dpi_, restore);
    w = std::max<int>(1, initialRect.right - initialRect.left);
    h = std::max<int>(1, initialRect.bottom - initialRect.top);

    // project documentation #4: WS_EX_NOREDIRECTIONBITMAP so content is fully DComp-owned.
    //
    // WS_OVERLAPPED (not WS_POPUP) + WS_THICKFRAME: the OVERLAPPED bit is what
    // makes Windows give the window a real sizing frame — including the
    // SM_CXPADDEDBORDER-width resize capture zone that extends a few px PAST
    // the visible edge. WS_POPUP windows get no such outside grip, which is
    // why the user had to hit the exact 8 px strip inside the window (and why
    // that strip then collided with the ScrollViewer's overlay scrollbar).
    // DwmExtendFrameIntoClientArea below already hides the visible frame, so
    // the look is identical; only the hit-test geometry changes.
    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | ExtraExStyle();
    DWORD style = WS_OVERLAPPED | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    hwnd_ = CreateWindowExW(
        exStyle, kClassName, title_.c_str(),
        style, initialRect.left, initialRect.top, w, h,
        OwnerHwnd(), nullptr, hInst, this);
    if (!hwnd_) {
        Trace(kTag, "CreateWindowExW FAILED", HRESULT_FROM_WIN32(GetLastError()));
        return E_FAIL;
    }
    SetPropW(hwnd_, L"FluentUI.AccessibleName", const_cast<wchar_t*>(title_.c_str()));

    // The window may have been placed (restored) on a monitor whose DPI differs
    // from the primary monitor's `dpi_` computed above. If we keep the primary
    // DPI, InitGraphics and the first Render use the wrong scale and every
    // control is drawn too big/small until a WM_DPICHANGED arrives (which may be
    // too late — the content surface is already sized). Re-read the authoritative
    // per-window DPI now so the initial frame is correct (project documentation pitfall #1).
    UINT winDpi = GetDpiForWindow(hwnd_);
    if (winDpi) dpi_ = winDpi;

    // The scheduler is the animation CLOCK (running_ flag + QPC dt); it no longer
    // drives an OS timer for normal animation — the main message loop pumps
    // animation every turn (PumpAnimations), paced by the frame waiter, so we can
    // run at the display refresh rate. A modal driver is needed ONLY while a system
    // modal loop (drag-resize) blocks our loop; that is armed/disarmed explicitly in
    // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE. So the arm/disarm edges here only need to
    // cover the case where an animation STARTS mid-drag, and they must not touch the
    // heartbeat's lifetime otherwise (WM_ENTERSIZEMOVE owns that).
    //
    // The heartbeat replaced a SetTimer(kAnimIntervalMs) fallback. That timer was
    // hard-capped at 16ms AND rounded up to the system tick, so a press-and-hold on
    // a resize border ran at 30-45fps on a 120Hz panel with the CPU 70% idle — the
    // measured symptom that motivated ModalFrameHeartbeat.
    frame_.Scheduler().SetCallbacks(
        [this] {
            if (inModalLoop_)
                modalHeartbeat_.Start(hwnd_, RefreshIntervalMs(RefreshHz()));
        },
        [this] { if (!inModalLoop_) modalHeartbeat_.Stop(); });

    // Client size in physical pixels (use GetClientRect; window is not shown yet
    // but WS_OVERLAPPEDWINDOW has a valid frame). Fall back to our computed size.
    RECT rc;
    GetClientRect(hwnd_, &rc);
    pixelW_ = std::max<UINT>(1, rc.right - rc.left);
    pixelH_ = std::max<UINT>(1, rc.bottom - rc.top);
    if (pixelW_ <= 1 || pixelH_ <= 1) { pixelW_ = w; pixelH_ = h; }

    FL_RETURN_IF_FAILED(kTag, InitGraphics(pixelW_, pixelH_));
    RecreateWindowIcon();

    // Build the service context now that the device / DWrite are up, so any
    // element registered in OnCreated is attached with a fully-wired context
    // (roadmap §6.2). Elements read services through Context() instead of the
    // old manual SetDWrite / Attach(WindowServices*) calls.
    context_.window = this;
    context_.dwrite = &graphics_.DWrite();
    context_.animations = &frame_.Anims();
    context_.hwnd = hwnd_;
    context_.dpiScale = DpiScale();
    context_.resourceCache = &graphics_.Cache();
    // Hand the tree the host's stable theme snapshot (roadmap §11, WP-05). The
    // pointer never changes for the window's lifetime — a theme change overwrites
    // the snapshot in place (ThemeManager::Set*) and bumps its generation, so the
    // parked pointer stays valid. The app sets the initial mode via SetDarkMode /
    // FollowSystemTheme before or after Create; the manager defaults to light.
    context_.theme = &theme_.Manager().Snapshot();

    // Wire the input routing (WP-03) via InputHost, which encapsulates the
    // InputManager, FocusManager, and ClickCounter.
    input_.Initialize(hwnd_, &context_, &elements_,
                      &NativeWindowHost::InvalidateThunk, this);
    // Run the caret-blink timer only while a blink-wanting element holds focus.
    focusChangedSub_ = input_.Focus().FocusChanged().Subscribe(this,
        [](void* owner, FocusManager&, FocusChangedArgs& e) {
            static_cast<NativeWindowHost*>(owner)->UpdateBlinkTimer(e.newFocus);
        });

    theme_.Appearance().SetHwnd(hwnd_);
    ApplyMica();
    ApplyDarkTitleBar();
    theme_.Appearance().SetCornerPreference(CornerPreferenceForRadius(cornerRadiusDip_));
    theme_.Appearance().SetShadowEnabled(shadowEnabled_);
    RecreateControlBrushes();

    // Window-level tooltip service (shares the window's D2D/DWrite devices).
    // A failure here is non-fatal: the window works, just without tooltips.
    tooltip_ = std::make_unique<TooltipService>();
    if (FAILED(tooltip_->Create(hInst_, hwnd_, &graphics_.D2D(), &graphics_.DWrite(),
                                &theme_.Manager().Snapshot()))) {
        TraceMsg(kTag, "Create: TooltipService unavailable");
        tooltip_.reset();
    }

    OnCreated();
    OnLayout();

    // Register OLE drag-drop (must happen after HWND exists, before ShowWindow).
    // Failure is non-fatal — the window works, just without drop support.
    if (SUCCEEDED(OleInitialize(nullptr))) {
        if (SUCCEEDED(RegisterDragDrop(hwnd_, this))) {
            dragDropRegistered_ = true;
            Trace(kTag, "RegisterDragDrop succeeded", S_OK);
        } else {
            TraceMsg(kTag, "RegisterDragDrop failed");
        }
    } else {
        TraceMsg(kTag, "OleInitialize failed — drop unavailable");
    }

    // project documentation #6: draw the first frame, DwmFlush, then ShowWindow. Synchronous
    // (RenderNow, not the coalesced Render) — the message loop is not running yet.
    //
    // SWP_FRAMECHANGED before ShowWindow: with WS_OVERLAPPED the window starts
    // life with a real caption frame, and WM_NCCALCSIZE(→0) only strips it when
    // the frame is next recalculated — which, without this call, is the first
    // user resize. Forcing a frame recalc now makes the very first shown frame
    // already caption-free, so the user never sees a transient double title bar
    // (system caption on top, our custom one below).
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);
    RenderNow(FrameReason::Paint);
    DwmFlush();
    ShowWindow(hwnd_, restoreMaximized_ ? SW_SHOWMAXIMIZED : InitialShowCommand());
    UpdateWindow(hwnd_);
    return S_OK;
}

HRESULT NativeWindowHost::InitGraphics(UINT pixelW, UINT pixelH) {
    // Phase 1: delegate to GraphicsHost
    FL_RETURN_IF_FAILED(kTag, graphics_.Initialize(hwnd_, pixelW, pixelH,
        [this] { RequestFrame(FrameReason::Paint); }));
    graphicsReady_ = true;
    return S_OK;
}

void NativeWindowHost::ApplyMica() {
    // Resolve the requested material against the live environment (OS build /
    // composition / transparency / high-contrast / RDP) and apply it with a solid
    // fallback (roadmap §12.2). The resolution also tells us whether high-contrast
    // is forcing a solid, opaque window — feed that into the theme so controls
    // paint legibly, and re-seed the dark title bar through the resolver.
    ResolvedBackdrop r = theme_.Appearance().ApplyBackdrop(requestedBackdrop_);
    if (theme_.Manager().Inputs().highContrast != r.suppressDarkTitleBar) {
        // High contrast is detected via the same resolver signal (it suppresses
        // the dark title bar); mirror it into the theme snapshot so controls can
        // switch to a high-contrast-safe path.
        theme_.Manager().SetHighContrast(r.suppressDarkTitleBar);
    }
    if (r.useSolidFallback)
        Trace(kTag, "backdrop: solid fallback in effect (painting base fill)", S_OK);
    // The base fill is part of every frame's pixels, so a resolution change must
    // repaint the whole window — a partial redraw would leave the old base behind
    // outside the dirty rect.
    fullDrawRequired_ = 2u;
    if (graphicsReady_) Render();
}

BaseFillPlan NativeWindowHost::BaseFill() const {
    return ResolveBaseFill(theme_.Appearance().LastResolved(),
                           theme_.Manager().Snapshot().colors.windowBackground,
                           backgroundOpacity_);
}

void NativeWindowHost::SetBackgroundOpacity(float opacity) {
    const float v = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    if (backgroundOpacity_ == v) return;
    backgroundOpacity_ = v;
    // Same reasoning as ApplyMica: the base fill covers the whole client area.
    fullDrawRequired_ = 2u;
    if (graphicsReady_) Render();
}

void NativeWindowHost::ApplyDarkTitleBar() {
    theme_.Appearance().SetDarkTitleBar(theme_.Manager().Snapshot().dark);
}

void NativeWindowHost::ApplyTheme() {
    ApplyDarkTitleBar();
    RecreateControlBrushes();
    // Bump the resource-cache epoch so any theme-dependent cached resource is
    // rebuilt on next use rather than reused stale (roadmap §13.3). For WP-04
    // inputs no cached layout actually depends on theme, so this is a cheap,
    // forward-looking backstop that becomes load-bearing with WP-05 typography.
    graphics_.Cache().BumpEpoch();
    for (UIElement* e : elements_)
        if (e) e->OnThemeChanged();
    // WP-07 §S4: a theme flip repaints every control + the caption chrome — force
    // a full clear so no partial redraw leaves stale-colored pixels behind.
    fullDrawRequired_ = 2u;
    Render();
    ThemeChangedArgs args;
    themeChanged_.Raise(*this, args);
}

HRESULT NativeWindowHost::Create(Application& application, const wchar_t* title,
                               float clientWDip, float clientHDip,
                               const WindowState* restore) {
    if (!application.IsUiThread() || IsOpen()) return E_INVALIDARG;
    // application_ should already be set by PrepareContent before this is called.
    // If it's not, the caller skipped PrepareContent (a lifecycle violation), but
    // set it here as a fallback so legacy code that never called PrepareContent
    // continues to work — though OwningApplication() won't be valid in OnInitialize
    // for them. New code should always funnel through Window::Show or
    // DialogWindow::Start, both of which call PrepareContent(&app) first.
    //
    // Guard against attaching to a DIFFERENT Application: if application_ is already
    // set and doesn't match the one we're being given, that's a real error.
    if (application_ && application_ != &application) return E_INVALIDARG;
    if (!application_) application_ = &application;
    HRESULT hr = Create(application.Instance(), title, clientWDip, clientHDip, restore);
    if (FAILED(hr)) { application_ = nullptr; return hr; }
    if (!application.AttachWindow(*this)) {
        application_ = nullptr;
        Close();
        return E_FAIL;
    }
    return S_OK;
}

void NativeWindowHost::SetCornerRadius(float radiusDip) {
    cornerRadiusDip_ = std::max(0.0f, radiusDip);
    if (hwnd_)
        theme_.Appearance().SetCornerPreference(CornerPreferenceForRadius(cornerRadiusDip_));
}

void NativeWindowHost::SetShadowEnabled(bool enabled) {
    shadowEnabled_ = enabled;
    if (hwnd_) theme_.Appearance().SetShadowEnabled(enabled);
}

void NativeWindowHost::RecreateControlBrushes() {
    if (controlBgBrush_) {
        DeleteObject(controlBgBrush_);
        controlBgBrush_ = nullptr;
    }
    const ColorTokens& pal = theme_.Manager().Snapshot().colors;
    COLORREF bg = RGB(static_cast<int>(pal.cardFill.r * 255.0f),
                      static_cast<int>(pal.cardFill.g * 255.0f),
                      static_cast<int>(pal.cardFill.b * 255.0f));
    controlBgBrush_ = CreateSolidBrush(bg);
}

void NativeWindowHost::AddElement(UIElement* e) {
    if (!e) return;
    elements_.push_back(e);
    // Attach the element (and its subtree) to the live service context so it gets
    // DWrite / device / popup registration without any manual wiring (§6.2/§6.3).
    // Guarded on graphicsReady_ so a stray pre-InitGraphics call is a no-op;
    // AddElement is normally called from OnCreated, after the context is built.
    if (graphicsReady_) e->AttachToContext(context_);
}

void NativeWindowHost::SetFocusElement(UIElement* e) {
    // Forward to the single focus authority; the caret-blink timer follows the
    // FocusChanged event (see UpdateBlinkTimer).
    input_.Focus().SetFocus(e);
}

void NativeWindowHost::UpdateBlinkTimer(UIElement* newFocus) {
    // Run a caret-blink timer only while a blink-wanting element is focused.
    bool wantBlink = newFocus && newFocus->WantsBlink();
    if (wantBlink && !blinkTimerOn_) {
        UINT period = GetCaretBlinkTime();
        if (period == 0 || period == INFINITE) period = 530;  // OS default
        SetTimer(hwnd_, kBlinkTimerId, period, nullptr);
        blinkTimerOn_ = true;
    } else if (!wantBlink && blinkTimerOn_) {
        KillTimer(hwnd_, kBlinkTimerId);
        blinkTimerOn_ = false;
    }
}

void NativeWindowHost::UpdateAnimationTimer() {
    // Rebuild the active-animation set from the element tree (a leaf adds itself
    // when it wants a tick; panels recurse). This runs at discrete triggers —
    // input events and program state changes — not every frame, so the timer
    // only ever ticks elements that are actually animating (roadmap §6.1). Arm
    // the clock iff something wants a tick; the scheduler owns the edge logic.
    frame_.Anims().Collect(elements_);
    frame_.Scheduler().SetWanted(!frame_.Anims().Empty());
}

// Coalesce a frame request (roadmap §14.3). The old input path called Render()
// which painted immediately; now Render() forwards here and the actual paint
// (RenderNow) happens once per message loop turn via RunDueFrame. Modal paths
// that run the *system* pump (drag-resize, WM_DPICHANGED) still call RenderNow
// directly, because our loop is not spinning during those.
void NativeWindowHost::RequestFrame(FrameReason reason) {
    frame_.Scheduler().RequestFrame(reason);
    if (!application_ && !UsesExternalFramePump() && hwnd_ && !destroying_)
        SetTimer(hwnd_, kSelfFrameTimerId, 1, nullptr);
}

// Service one coalesced frame if the loop has decided to render this turn. The
// scheduler snapshots+clears the pending request (BeginFrame) so a re-invalidate
// during painting is preserved for the next turn, then we paint.
void NativeWindowHost::RunDueFrame() {
    if (!frame_.Scheduler().NeedsFrame()) return;
    FrameReason serviced = frame_.Scheduler().BeginFrame();
    // Phase 1A: fold any coalesced WM_SIZE into this frame (one ResizeBuffers +
    // OnLayout at most) before painting. No-op when no resize is pending.
    ApplyPendingResize();
    RenderNow(serviced);
    frame_.Scheduler().EndFrame();
}

// THE animation clock (roadmap §6.1 / §14). Called once per message-loop turn.
// While the clock is armed (something is animating), advance the active set by
// the real QPC elapsed time and request the next frame so the loop keeps
// cycling — paced by the loop's FrameWaitTimeoutMs() wait, i.e. at the display's
// refresh rate. (That pacing used to come free from the swap chain's blocking
// Present1; Route 2 removed it, so the loop must supply the timeout itself.)
// Ticking here (not only in WM_TIMER) means a slider drag or any
// pointer-capture flood no longer starves the animation: our loop keeps running
// and pumping. Does NOT paint — it only advances state + schedules the frame;
// RunDueFrame does the actual paint later in the same turn.
bool NativeWindowHost::PumpAnimations() {
    if (!frame_.Scheduler().Running()) return false;
    const float dt = frame_.Scheduler().ComputeDt(QpcNow());
    // Time the tick and carry it to the next frame's stats. This runs OUTSIDE
    // RenderNow, so cpuFrameMs cannot see it — and it is where the compositor
    // surface refills live (a control's OnAnimationTick re-rasterizes its scroll
    // surface here, which for a long document is the most expensive UI-thread work
    // in the whole scroll path). Without this the overscan refill cost is invisible.
    const int64_t tickStart = QpcNow();
    const bool stillAnimating = frame_.Anims().Tick(dt);
    pendingAnimationMs_ = QpcToMs(QpcNow() - tickStart);
    if (stillAnimating) {
        RequestFrame(FrameReason::Animation);
    } else {
        // Falling edge: nothing left to animate. Disarm the clock so the loop
        // returns to the pure-idle input wait (SetWanted handles the edge).
        frame_.Scheduler().SetWanted(false);
    }
    return stillAnimating;
}

void NativeWindowHost::RenderNow(FrameReason reason) {
    if (!graphicsReady_) return;

    // Re-entrancy guard: laying out or painting can call SetOffset/Invalidate,
    // which routes back here through the root callback. Coalesce those into the
    // in-flight frame instead of nesting BeginContentFrame/EndContentFrame. This
    // is checked BEFORE resetting stats/timers so a reentrant call does not zero
    // the in-flight frame's metrics.
    if (rendering_) return;
    rendering_ = true;

    // Record metrics for this frame (roadmap §18.3). The reason/dirty counts are
    // sampled into lastFrameStats_ for a Debug HUD; direct RenderNow callers
    // (OnSize/OnDpiChanged/anim tick) pass their own reason so the HUD shows why
    // each frame happened. cpuFrameMs is timed manually (QPC) so it is finalised
    // BEFORE the diagnostics_.Frames().Push at the end (a ScopedTimer would fire too late).
    lastFrameStats_.Reset();
    lastFrameStats_.frameReason = static_cast<uint32_t>(reason);
    lastFrameStats_.activeAnimations = static_cast<uint32_t>(frame_.Anims().Count());
    // The animation tick that led to this frame ran before RenderNow (PumpAnimations),
    // so its cost is carried over rather than timed here. Consume it so the next frame
    // does not double-report a tick that did not happen.
    lastFrameStats_.animationMs = pendingAnimationMs_;
    pendingAnimationMs_ = 0.0;
    // Same carry-over for ApplyPendingResize, which likewise ran before this frame's
    // clock started (RunDueFrame / ServiceModalFrame apply the resize, then paint).
    lastFrameStats_.resizeMs = pendingResizeMs_;
    pendingResizeMs_ = 0.0;
    graphics_.Cache().ResetFrameStats();
    const int64_t frameStartQpc = QpcNow();

    // --- Dirty analysis (WP-07 §S1/§S4) -----------------------------------
    // Walk the tree once: does anything need a re-measure, and what are the
    // bounds of every element whose own dirty flags are set? The dirty-bounds
    // union drives the partial-redraw decision; its size is the accurate
    // dirtyElements count (every re-rendered element, not just measure roots).
    bool measureDirty = false;
    bool arrangeDirty = false;
    std::vector<RectDip> dirtyBounds;
    for (UIElement* e : elements_) {
        if (!e) continue;
        if (e->AnyDirtyInSubtree(DirtyFlags::Measure)) measureDirty = true;
        if (e->AnyDirtyInSubtree(DirtyFlags::Arrange)) arrangeDirty = true;
        e->CollectDirtyBounds(dirtyBounds);
    }
    lastFrameStats_.dirtyElements = static_cast<uint32_t>(dirtyBounds.size());

    // Arrange counts as much as Measure. This used to gate on measureDirty alone,
    // so an Arrange-ONLY invalidation never ran layout at all: arrangeDirty was
    // computed but only consulted further down to force a full repaint, which
    // redraws every element at the bounds it already had. Nothing had needed
    // Arrange-without-Measure before ScrollPanel started expressing its scroll
    // offset as child bounds — with the gate as it was, a wheel notch repainted
    // the scrollbar (drawn from the live offset) while the content stayed put,
    // and only a resize (which calls OnLayout unconditionally) appeared to fix it.
    if (measureDirty || arrangeDirty) {
        ScopedTimer layoutTimer(lastFrameStats_.layoutMs);
        OnLayout();
    }

    // Union the per-element dirty rects into one region (window DIPs).
    RectDip dirtyDip;
    for (const RectDip& r : dirtyBounds) dirtyDip = RectDip::Union(dirtyDip, r);

    const float wDip = ClientWidthDip();
    const float hDip = ClientHeightDip();
    const float s = DpiScale();

    // Caption hover/press is host chrome, invisible to the element dirty walk —
    // a change there forces a full redraw so the caption glyphs repaint.
    const bool captionChanged =
        (hotCaption_ != prevHotCaption_) || (pressedCaption_ != prevPressedCaption_);

    // --- Decide full vs partial redraw (WP-07 §S4) ------------------------
    // Delegated to the pure PlanRedraw helper (DirtyRegion.h) so the decision is
    // testable headless. Partial is unsafe on warm-up frames, on any layout change
    // (Measure/Arrange can move bounds anywhere), or when the caption chrome
    // changed — collapse those into forceFull.
    // HUD sits top-right and repaints from last frame's stats every frame; a
    // partial redraw could clip it out or leave stale digits, so force full while
    // it is visible.
    const bool forceFull =
        (fullDrawRequired_ > 0) || measureDirty || arrangeDirty || captionChanged ||
        hudVisible_;
    const RedrawPlan plan = PlanRedraw(dirtyDip, wDip, hDip, s,
                                       pixelW_, pixelH_, kPartialRedrawMaxCoverage,
                                       forceFull);
    const bool partial = plan.partial;
    const RectDip redrawDip = plan.redrawDip;
    const RECT dirtyPx = plan.dirtyPx;

    ID2D1DeviceContext* dc = nullptr;
    POINT netOffset = {};
    // The content is now a DComp surface (not a swap chain): pass the dirty rect in
    // pixels on a partial frame so only that region is redrawn (the surface is
    // persistent). netOffset folds the surface's atlas tile origin (minus the
    // update-rect origin) into our transform below.
    HRESULT beginHr = graphics_.Comp().BeginContentFrame(
        &dc, &netOffset, /*fullClear=*/!partial, partial ? &dirtyPx : nullptr);
    if (FAILED(beginHr) || !dc) {
        rendering_ = false;
        // A lost device surfaces here (D2DERR_RECREATE_TARGET / device removed):
        // rebuild the stack and repaint. A transient failure just skips this frame.
        if (deviceLost_.NotifyFrameResult(ClassifyDeviceHResult(beginHr)))
            RecoverDevice();
        return;
    }

    // Paint in DIPs: DPI scale, composed onto the surface's net atlas translation
    // so DIP (0,0) lands at the surface tile origin. Row-vector order: the scale is
    // applied first, then the pixel-space translation — Scale(s) * Translation(net).
    dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                     D2D1::Matrix3x2F::Translation(static_cast<float>(netOffset.x),
                                                   static_cast<float>(netOffset.y)));

    // One reusable solid brush per frame, shared by every element via the
    // DrawingContext (roadmap §5.3.1 / §13.2): controls draw through typed methods
    // that set this one brush's color, instead of each creating its own per frame.
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf()))) {
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        graphics_.Comp().EndContentFrame();
        rendering_ = false;
        return;
    }

    // On a partial frame, scope everything to the dirty rect: clear only that
    // sub-region to transparent (Mica shows through) then repaint the elements
    // that intersect it. The DrawingContext carries the dirty region as its
    // ClipHint so Panel::Render culls children outside it. On a full frame the
    // clip hint is unbounded (nullptr) so every element is visited.
    {
        uint32_t drawOps = 0;
        ClipGuard partialClip;
        const RectDip* hint = nullptr;
        if (partial) {
            partialClip = ClipGuard(dc, D2D1::RectF(redrawDip.x, redrawDip.y,
                                                    redrawDip.right(), redrawDip.bottom()));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));  // transparent, dirty region only
            hint = &redrawDip;
        }

        // Window BASE FILL (roadmap §12.2). The content surface is premultiplied
        // alpha over the DWM material, so a cleared frame is see-through by
        // design — correct only while a material actually exists. Without one
        // (pre-Win11, composition off, RDP, high contrast, or BackdropKind::None)
        // there is no material AND no redirection bitmap, so the desktop shows
        // through the whole window and every material-authored translucent
        // control fill loses the opaque base it was designed against. Paint that
        // base here, over exactly the region being redrawn.
        const BaseFillPlan base = BaseFill();
        if (base.fill) {
            const RectDip fillArea = partial ? redrawDip : RectDip{0, 0, wDip, hDip};
            brush->SetColor(base.color);
            dc->FillRectangle(D2D1::RectF(fillArea.x, fillArea.y, fillArea.right(),
                                          fillArea.bottom()),
                              brush.Get());
        }

        DrawingContext rc{dc, brush.Get(), s, &drawOps, hint};

        // Time the element traversal + D2D draw (fills renderMs) so the HUD can
        // separate "build the frame" cost from present cost — key for diagnosing
        // resize frame drops (is layout, render, or present the bottleneck?).
        ScopedTimer renderTimer(lastFrameStats_.renderMs);
        OnRender(dc);
        for (UIElement* e : elements_)
            if (e) e->RenderWithOpacity(rc);
        DrawWindowIcon(dc);
        DrawStandardTitle(dc);
        DrawCaptionButtons(dc, s);
        if (hudVisible_) DrawHud(dc);
        lastFrameStats_.drawOps = drawOps;
    }  // partialClip pops here (before SetTransform / present)

    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    // Time EndContentFrame (D2D EndDraw + Present1) + DComp Commit (fills
    // presentMs).
    HRESULT endHr;
    {
        ScopedTimer presentTimer(lastFrameStats_.presentMs);
        endHr = partial ? graphics_.Comp().EndContentFrame(&dirtyPx, 1)
                        : graphics_.Comp().EndContentFrame();
        graphics_.Comp().Commit();
    }
    lastFrameStats_.dirtyRects = partial ? 1u : 0u;

    // Frame serviced: clear the tree's accumulated dirty flags.
    for (UIElement* e : elements_)
        if (e) e->ClearDirtySubtree();

    prevHotCaption_ = hotCaption_;
    prevPressedCaption_ = pressedCaption_;
    if (fullDrawRequired_ > 0) --fullDrawRequired_;

    // Sample resource-cache diagnostics for this frame (roadmap §18.3).
    const CacheStats& cs = graphics_.Cache().Stats();
    lastFrameStats_.textLayoutsNew = cs.misses;
    lastFrameStats_.textLayoutCacheHits = cs.hits;

    // Finalise the CPU frame time and feed the P95/P99 ring (WP-07 §S2).
    const int64_t frameEndQpc = QpcNow();
    lastFrameStats_.cpuFrameMs = QpcToMs(frameEndQpc - frameStartQpc);
    diagnostics_.Frames().Push(static_cast<float>(lastFrameStats_.cpuFrameMs));

    // Snapshot the now-complete stats for the HUD to read next frame (the HUD is
    // drawn mid-frame, before these fields are filled, so it must read the prior
    // completed frame — see prevFrameStats_).
    prevFrameStats_ = lastFrameStats_;

    // Session diagnostic capture (in-memory, zero-alloc, no I/O; never requests a
    // frame). Uses the interval since the previous painted frame as the real
    // cadence; the phase is derived from the current modal/animation state.
    //
    // THE IDLE GUARD IS THE SAME PREDICATE THE INTERVAL RING USES, deliberately.
    // This call site used to carry its own copy of the rejected `gap <= 500.0`
    // threshold, which FrameIntervalIsCadenceSample exists to replace (see the
    // reasoning at FramePacing.h and at the interval-ring push below): a threshold
    // cannot distinguish "the window idled 3s" from "this frame COST 3s", and it
    // resolves the ambiguity the wrong way on exactly the workload the log is read
    // for. The fix reached the ring but not this line, and the comment here still
    // claimed the two matched — so a genuinely 3-second frame was recorded as
    // intervalMs = 0 and vanished from the jank list and the phase average. The log
    // is written on close to prove timing regressions after the fact, which made the
    // *worst* hitches the ones guaranteed to be absent from it.
    //
    // Sharing the predicate also removes the second, redundant filter: RecordFrame
    // already excludes SessionPhase::Idle from the jank list for the same reason, so
    // an idle gap that reaches it is dropped there rather than being zeroed here.
    if (sessionLogEnabled_) {
        float intervalMs = 0.0f;
        if (FrameIntervalIsCadenceSample(lastFrameQpc_ != 0, loopWasContinuous_))
            intervalMs = static_cast<float>(QpcToMs(frameEndQpc - lastFrameQpc_));
        diagnostics_.Log().RecordFrame(lastFrameStats_, intervalMs,
                                CurrentSessionPhase(reason), SessionClockSec());
    }

    // Feed the frame-INTERVAL ring (real cadence for FPS/jank on the HUD): the gap
    // since the previous painted frame. Skip the first frame (no prior timestamp),
    // and skip a gap that spans an IDLE period — the loop blocking on input is not a
    // cadence sample and would read as one enormous stutter when the HUD opens.
    //
    // WHY THIS TESTS STATE AND NOT THE GAP'S SIZE. This guard used to be
    // `intervalMs <= 500.0`: treat anything big as an idle resume. That cannot
    // distinguish the two cases it has to distinguish —
    //
    //     the window slept 3s waiting for input   -> not a sample, discard
    //     this frame genuinely COST 3s            -> the most important sample there is
    //
    // — and it silently chose wrong on exactly the workload where the numbers matter.
    // Dragging the edge of a window holding a large document, every real interval
    // exceeded 500ms and was discarded as "idle", leaving the ring holding only the
    // cheap frames in between. The HUD then reported four-digit FPS against a window
    // that was visibly redrawing about once per second: the measurement excluded the
    // entire cost it existed to measure.
    //
    // The honest question is whether the loop was CONTINUOUSLY BUSY across the gap,
    // which is a property of the scheduler state at the end of the previous frame, not
    // of the gap's magnitude. `loopWasContinuous_` records it there (see below), so a
    // 3-second frame is now kept and a 3-second idle is still dropped — no threshold,
    // and no load at which the two get confused.
    if (FrameIntervalIsCadenceSample(lastFrameQpc_ != 0, loopWasContinuous_)) {
        const double intervalMs = QpcToMs(frameEndQpc - lastFrameQpc_);
        diagnostics_.Intervals().Push(static_cast<float>(intervalMs));
    }
    lastFrameQpc_ = frameEndQpc;
    // PACING REFERENCE: the START of this frame, deliberately NOT frameEndQpc.
    //
    // The deadline for the next frame is "one refresh after this one began". Pacing
    // from the END instead adds this frame's own cost to every cycle: the loop woke
    // at start = prevEnd + interval - margin, spent cpuFrameMs painting, and set the
    // next reference to prevEnd + interval - margin + cpuFrameMs. The frame cost is
    // then OUTSIDE the interval rather than inside it, so the cadence runs slow by
    // exactly cpuFrameMs every frame — a permanent, load-dependent shortfall rather
    // than a transient one. On a 120Hz panel with the HUD open (cpuFrameMs ~0.8ms)
    // that measured 9.1ms intervals = 110fps instead of 120, reproducible every run.
    //
    // Anchoring to the start makes the frame's own cost fit INSIDE the interval,
    // which is what "one frame per refresh" means. It also degrades correctly: if a
    // frame overruns the interval the next deadline is already past, FrameIsDue is
    // true immediately, and the loop paints back-to-back until it catches up instead
    // of compounding the delay.
    //
    // lastFrameQpc_ (end-based) is kept, but ONLY for diagnostics: the interval ring
    // and the HUD measure end-to-end, which has the same mean in steady state and is
    // the number a user comparing against a frame budget expects. Every PACING
    // accessor reads lastFrameStartQpc_ instead — see FrameWaitTimeoutMs and
    // FrameWaitRemaining, which deliberately share this one reference point so the
    // coarse and precise paths can never disagree about whether a frame is due.
    lastFrameStartQpc_ = frameStartQpc;

    // Keep the HUD live: request the next frame so the numbers update even when
    // nothing else is dirty. This makes the window continuously repaint while the
    // HUD is on (that is the point — you are watching the frame rate), paced by
    // the frame-latency waitable. Toggling the HUD off returns to idle.
    if (hudVisible_) RequestFrame(FrameReason::Paint);

    // Will the loop go straight into another frame, or block waiting for input? This
    // is what decides whether the NEXT frame's interval is a cadence sample (see the
    // interval-ring guard above). Sampled here, after the HUD's self-request, so that
    // request counts: with the HUD open the loop genuinely never idles. A pending
    // frame, a live animation or an unapplied resize each mean the next frame follows
    // directly and the gap to it is real cadence rather than a wait for input.
    loopWasContinuous_ = frame_.Scheduler().NeedsFrame() || IsAnimating() || resizePending_;

    // Rebuild the animation set at the frame boundary, not only on input events.
    // A PROGRAMMATIC state change (e.g. TabControl::SetSelectedIndex from code, or
    // a control starting an animation from a timer) can begin an animation with no
    // pointer/key ever reaching the window, and UpdateAnimationTimer otherwise
    // runs only on input — so that animation is never collected, never ticked,
    // and any state gated on it (the tab indicator position, the content fade)
    // freezes at its start value. The frame boundary is the reliable sync point:
    // every state change schedules a frame, so re-collecting here guarantees the
    // set reflects reality. Collect is a tree walk of WantsAnimationTick, cheap
    // relative to the paint that just ran, and SetWanted disarms the clock when
    // nothing wants a tick, so idle stays zero-cost.
    UpdateAnimationTimer();

    rendering_ = false;

    // Classify the present result (roadmap §17): a lost device triggers a rebuild
    // on the way out of the frame (not mid-paint). Done after rendering_ is
    // cleared so RecoverDevice's follow-up repaint is not suppressed as reentrant.
    if (deviceLost_.NotifyFrameResult(ClassifyDeviceHResult(endHr)))
        RecoverDevice();
}

void NativeWindowHost::SetHudVisible(bool on) {
    if (hudVisible_ == on) return;
    hudVisible_ = on;
    // Turning it on: kick a frame so it appears immediately (and RenderNow then
    // keeps re-requesting). Turning it off: one more frame to erase it, after
    // which nothing re-requests and the window returns to idle.
    RequestFrame(FrameReason::Paint);
}

// --- Session diagnostic log ------------------------------------------------

double NativeWindowHost::SessionClockSec() const {
    if (!sessionLogEnabled_ || sessionStartQpc_ == 0) return 0.0;
    return QpcToMs(QpcNow() - sessionStartQpc_) / 1000.0;
}

SessionPhase NativeWindowHost::CurrentSessionPhase(FrameReason reason) const {
    // Modal system loop: distinguish an active resize from a position-only move.
    if (inModalLoop_)
        return resizePending_ ? SessionPhase::Resizing : SessionPhase::Moving;
    if (HasReason(reason, FrameReason::Resize)) return SessionPhase::Resizing;
    if (HasReason(reason, FrameReason::Dpi)) return SessionPhase::Dpi;
    if (frame_.Anims().Count() > 0 || HasReason(reason, FrameReason::Animation))
        return SessionPhase::Animating;
    return SessionPhase::Idle;
}

int NativeWindowHost::RefreshHz() {
    if (refreshHz_ != 0) return refreshHz_;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
        refreshHz_ = static_cast<int>(dm.dmDisplayFrequency);
    return refreshHz_;
}

unsigned NativeWindowHost::FrameWaitTimeoutMs() const {
    // No previous frame (first paint): render immediately, nothing to pace against.
    if (lastFrameStartQpc_ == 0) return 0u;
    // Same reference point as FrameWaitRemaining — the previous frame's START. Two
    // pacing accessors that disagreed about "when did the last frame happen" would
    // be a silent source of drift: the coarse one would think a frame was overdue
    // while the precise one still wanted to wait, or vice versa. There is one answer
    // to that question and both read it.
    const double elapsed = QpcToMs(QpcNow() - lastFrameStartQpc_);
    // RefreshHz() caches, so this is a plain read on all but the first call. The
    // const_cast keeps the accessor const for callers (it is a memo, not state).
    const int hz = const_cast<NativeWindowHost*>(this)->RefreshHz();
    return FrameWaitMs(elapsed, hz);
}

double NativeWindowHost::FrameWaitRemaining() const {
    // First paint: due immediately.
    if (lastFrameStartQpc_ == 0) return 0.0;
    // Measured from the previous frame's START, not its end — see the comment at
    // lastFrameStartQpc_'s assignment in RenderNow. Using the end would push each
    // frame's own paint cost outside the refresh interval, making the cadence run
    // permanently slow by cpuFrameMs (the measured 110-vs-120fps shortfall).
    const double elapsed = QpcToMs(QpcNow() - lastFrameStartQpc_);
    const int hz = const_cast<NativeWindowHost*>(this)->RefreshHz();
    return FrameWaitRemainingMs(elapsed, hz);
}

void NativeWindowHost::EnableSessionLog(bool on) {
    if (sessionLogEnabled_ == on) return;
    sessionLogEnabled_ = on;
    if (on) {
        sessionStartQpc_ = QpcNow();
        const int refreshHz = RefreshHz();   // cached; also used for frame pacing
        diagnostics_.Log().Start(static_cast<int>(DpiScale() * 100.0f + 0.5f),
                          static_cast<int>(pixelW_), static_cast<int>(pixelH_),
                          ClientWidthDip(), ClientHeightDip(), refreshHz);
        diagnostics_.Log().RecordEvent("session", "launch", SessionClockSec());
    }
}

void NativeWindowHost::LogSessionEvent(const char* category,
                                     const std::string& detail) {
    if (sessionLogEnabled_) diagnostics_.Log().RecordEvent(category, detail, SessionClockSec());
}

// Draw the on-screen performance HUD (roadmap §18.3). Text is built by the pure
// FormatHudLines (diagnostics/DebugHud.h) from last frame's stats + the interval
// ring percentiles; here we just lay out a translucent panel + the lines in the
// top-right corner. DIP space (caller left the transform at the DPI scale).
void NativeWindowHost::DrawHud(ID2D1DeviceContext* dc) {
    if (!graphics_.DWrite().Valid()) return;

    HudPercentiles iv;
    iv.p50Ms = diagnostics_.Intervals().Percentile(50);
    iv.p95Ms = diagnostics_.Intervals().Percentile(95);
    iv.p99Ms = diagnostics_.Intervals().Percentile(99);
    iv.maxMs = diagnostics_.Intervals().Max();
    // CPU-cost percentiles from frameRing_ (already fed every frame, previously never
    // displayed). These are what survive a bimodal distribution: a burst of trivial
    // frames can drag the interval P50 — and therefore the FPS reading — down to a
    // fraction of a millisecond, but it cannot pull P99/Max off a 900ms frame.
    HudCpuPercentiles cpu;
    cpu.p50Ms = diagnostics_.Frames().Percentile(50);
    cpu.p99Ms = diagnostics_.Frames().Percentile(99);
    cpu.maxMs = diagnostics_.Frames().Max();
    const std::string ascii = FormatHudLines(prevFrameStats_, iv, cpu);
    const std::wstring text(ascii.begin(), ascii.end());  // ASCII-only, safe widen

    IDWriteTextFormat* fmt =
        graphics_.DWrite().Format(12.0f, DWRITE_FONT_WEIGHT_NORMAL,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (!fmt) return;

    // Panel: top-right, fixed size, a few DIP in from the frame. Sized for the seven
    // lines FormatHudLines emits (the CPU-percentile line was added to make a bimodal
    // frame-time distribution visible, and the async-layout line tracks worker behavior
    // during resize drags) at the widest — the tick/rsz/draw line.
    const float panelW = 250.0f, panelH = 105.0f, margin = 8.0f;
    const float right = ClientWidthDip() - margin;
    const float top = margin + TitleBarHeightDip();  // below the caption band
    RectDip panel{right - panelW, top, panelW, panelH};

    ComPtr<ID2D1SolidColorBrush> bg;
    if (SUCCEEDED(dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.55f), bg.GetAddressOf()))) {
        D2D1_ROUNDED_RECT rr{{panel.x, panel.y, panel.right(), panel.bottom()}, 4.0f, 4.0f};
        dc->FillRoundedRectangle(rr, bg.Get());
    }
    ComPtr<ID2D1SolidColorBrush> fg;
    if (SUCCEEDED(dc->CreateSolidColorBrush(D2D1::ColorF(0.6f, 1.0f, 0.6f, 1.0f), fg.GetAddressOf()))) {
        D2D1_RECT_F tr{panel.x + 6.0f, panel.y + 4.0f, panel.right() - 6.0f, panel.bottom() - 4.0f};
        dc->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, tr, fg.Get());
    }
}

bool NativeWindowHost::RecoverDevice() {
    if (!deviceLost_.BeginRebuild()) return deviceLost_.IsHealthy();

    HRESULT reason = graphics_.D2D().DeviceRemovedReason();
    Trace(kTag, "device lost — rebuilding stack", reason);

    // Tell the tree the device is gone (controls release any device-bound
    // resource; today none do, but panels propagate for future controls).
    for (UIElement* e : elements_)
        if (e) e->OnDeviceLost();

    // Drop cached GPU/factory resources and any transient overlays — a popup /
    // tooltip recreates its own composition target from the fresh device on next open.
    graphics_.Cache().Clear();
    CloseActivePopups();
    if (tooltip_) tooltip_->Hide();

    // Release the old stack and rebuild IN PLACE so borrowers (PopupHost /
    // TooltipService holding d2d_ pointers) stay valid.
    graphicsReady_ = false;
    titleIconBitmap_.Reset();
    graphics_.Reset();

    HRESULT hr = graphics_.Rebuild(hwnd_, pixelW_, pixelH_);
    if (FAILED(hr)) {
        Trace(kTag, "device rebuild FAILED", hr);
        deviceLost_.EndRebuild(false);  // stay Lost; retry on a later frame
        return false;
    }

    // Re-point the context at the fresh cache.
    context_.resourceCache = &graphics_.Cache();
    graphicsReady_ = true;
    RecreateWindowIcon();

    // Notify the tree the device is back. A full repaint follows below (RenderNow
    // repaints the whole tree unconditionally), so no explicit re-dirty is needed.
    for (UIElement* e : elements_)
        if (e) e->OnDeviceRestored();

    deviceLost_.EndRebuild(true);
    Trace(kTag, "device rebuilt", S_OK);
    // WP-07 §S4: fresh content surface — force a full clear for the next 2 frames.
    fullDrawRequired_ = 2u;
    RenderNow(FrameReason::Paint);
    return true;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK NativeWindowHost::WndProc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
    NativeWindowHost* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<NativeWindowHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<NativeWindowHost*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT NativeWindowHost::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT appResult = 0;
    if (OnAppMessage(msg, wp, lp, appResult))
        return appResult;

    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp == TRUE) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                // Reserve NC bands on left/right/bottom only. Top cannot have an NC inset
                // without triggering the system title bar (DWM draws its own caption when
                // the top edge is non-client), producing a double-title-bar artifact.
                //
                // The DComp content surface follows the client origin (verified by probe:
                // 40px left+top NC inset shifted content 40px right+down), so asymmetric
                // insets pose no coordinate-system issue — the surface adjusts automatically.
                const int insetPx = ResizeGripOuterPx(dpi_);
                params->rgrc[0].left += insetPx;
                // params->rgrc[0].top — unchanged; top edge remains client rect boundary
                params->rgrc[0].right -= insetPx;
                params->rgrc[0].bottom -= insetPx;
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            return HitTestNca(pt);
        }

        case WM_GETMINMAXINFO: {
            // Clamp how small the user can drag the window. Without this the system
            // floor (~SM_CXMIN) applies and the layout collapses. Recomputed from DIPs
            // on every message so the limit tracks the current monitor's DPI — the
            // window may have been dragged to a different one since it was set.
            // (The very first WM_GETMINMAXINFO precedes WM_NCCREATE, so GWLP_USERDATA
            // is still null and WndProc sends it to DefWindowProc — this handler only
            // ever runs once the instance is wired up.)
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            POINT minTrack = mmi->ptMinTrackSize;
            if (MinTrackSizePx(minClientWDip_, minClientHDip_, dpi_, minTrack))
                mmi->ptMinTrackSize = minTrack;
            return 0;
        }

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                OnSize(LOWORD(lp), HIWORD(lp));
                // Phase 1A: WM_SIZE arrives densely inside the system modal resize
                // loop where our main loop (and the low-priority WM_TIMER heartbeat)
                // is starved. Service a frame right here so the coalesced resize is
                // applied and animation advances smoothly while dragging the edge.
                if (inModalLoop_) ServiceModalFrame(/*pace*/ true);
            }
            return 0;

        case WM_MOVING:
            // Window being dragged (position only, no size change): the modal loop
            // owns the pump, so drive animation from these frequent messages too so
            // a running animation (e.g. an indeterminate ProgressBar) keeps moving
            // while the window is dragged. No resize is pending, so this paints only
            // when something is actually animating.
            if (inModalLoop_) ServiceModalFrame(/*pace*/ true);
            break;  // let DefWindowProc handle the actual move

        case WM_MOVE:
            // Dismiss active popup when the parent window moves.
            if (popupDismiss_) {
                if (popupDismiss_(PopupDismissReason::Move, nullptr, 0, 0))
                    popupDismiss_ = nullptr;
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            // User started resizing/moving the window — dismiss popup immediately.
            if (popupDismiss_) {
                if (popupDismiss_(PopupDismissReason::Resize, nullptr, 0, 0))
                    popupDismiss_ = nullptr;
            }
            // Entering a SYSTEM modal move/resize loop: our message loop (and thus
            // PumpAnimations / RunDueFrame) stops running. Arm the heartbeat timer
            // UNCONDITIONALLY (not just when animating) — with Phase 1A, WM_SIZE no
            // longer paints synchronously, so the heartbeat is now what applies the
            // coalesced resize and repaints the window during the drag. It also
            // keeps any active animation ticking. Disarmed on WM_EXITSIZEMOVE.
            inModalLoop_ = true;
            context_.inModalResize = true;
            // UIContext is stored BY VALUE on every element (UIElement::context_), so
            // mutating the host's copy alone leaves the tree reading a stale flag —
            // exactly the same hazard UpdateContextDpi exists for. Push it down now, so
            // the first RefreshComposition of the drag already sees it and skips its
            // BeginDraw. Cheap: one bool write per element, no layout, no invalidation.
            for (UIElement* e : elements_)
                if (e) e->UpdateContextModalResize(true);
            // Forget the previous drag's opportunity spacing: it described that
            // drag's mouse rate and message mix, and starting a new drag with a
            // stale (possibly much wider) estimate would let the first frames
            // overshoot the panel before the smoothing pulls it back.
            ResetModalGapEstimate();
            // Precise tick from a worker thread posting WM_FLUENT_MODAL_TICK. This
            // is the primary driver now, and the reason a held-but-motionless resize
            // border no longer drops to ~30fps: WM_TIMER could not deliver better
            // than ~28.6ms here (low priority + rounded to the system timer
            // resolution), while this paces at the actual refresh interval.
            modalHeartbeat_.Start(hwnd_, RefreshIntervalMs(RefreshHz()));
            // Keep the OS timer armed as a FALLBACK only when the heartbeat could
            // not start (no waitable timer on this OS). Arming both would double the
            // tick rate for no benefit — ServiceModalFrame would just skip the
            // extra, but it is wasted wakeups.
            if (!modalHeartbeat_.IsRunning())
                SetTimer(hwnd_, kAnimTimerId, kAnimIntervalMs, nullptr);
            if (sessionLogEnabled_)
                diagnostics_.Log().RecordEvent("modal", "enter size/move loop",
                                        SessionClockSec());
            break;

        case WM_EXITSIZEMOVE:
            // Modal loop done — the main loop resumes driving PumpAnimations /
            // RunDueFrame. Apply any resize the heartbeat had not yet folded in and
            // paint the final exact size synchronously (crisp last frame), then drop
            // the heartbeat timer.
            inModalLoop_ = false;
            context_.inModalResize = false;
            // Clear the flag on the tree BEFORE the convergence layout below. Order is
            // load-bearing: that layout is what rasterizes the final pixels, and it can
            // only do so if every host has already seen the falling edge (which is also
            // where ScrollContentHost marks its surfaces stale — see SetModalResize).
            for (UIElement* e : elements_)
                if (e) e->UpdateContextModalResize(false);
            // Stop the tick BEFORE the final paint: Stop() joins the worker, so no
            // further WM_FLUENT_MODAL_TICK can be posted after this point and the
            // synchronous final frame below is the last one of the drag.
            modalHeartbeat_.Stop();
            // Convergence layout: run a precise layout at the final size if a WM_SIZE
            // is still unconsumed.
            if (resizePending_) {
                frame_.Scheduler().BeginFrame();
                ApplyPendingResize();  // inModalLoop_ is false now → full OnLayout
                RenderNow(FrameReason::Resize);
                frame_.Scheduler().EndFrame();
            } else {
                // No pending size, but we still ran a tree update in the drag —
                // ScrollContentHost may have marked surfaces stale on the falling edge
                // (SetModalResize), so paint once to converge to the exact final pixels.
                // NOTE: UpdateContextModalResize(false) auto-invalidates Arrange on every
                // element, so OnLayout will trigger OnBoundsChanged → RefreshComposition.
                frame_.Scheduler().BeginFrame();
                OnLayout();
                RenderNow(FrameReason::Resize);
                frame_.Scheduler().EndFrame();
            }
            KillTimer(hwnd_, kAnimTimerId);
            if (sessionLogEnabled_) {
                char buf[96];
                _snprintf_s(buf, sizeof(buf), _TRUNCATE, "exit; final %ux%upx",
                            pixelW_, pixelH_);
                diagnostics_.Log().RecordEvent("modal", buf, SessionClockSec());
            }
            break;

        case WM_ACTIVATE:
            // Dismiss popup when the window loses activation to another app (not our
            // own popup). WA_INACTIVE with lp=popup HWND means popup is showing.
            if (LOWORD(wp) == WA_INACTIVE) {
                // Release any pointer capture with a canceling release so a control
                // pressed when focus left doesn't stay stuck pressed (§9.3).
                input_.Input().OnWindowDeactivated();
                // Break the click streak. Otherwise clicking away and quickly back to
                // the same spot resumes the old streak, so one click in the new
                // activation reads as a double- or triple-click.
                input_.Clicks().Reset();
                if (popupDismiss_) {
                    HWND other = reinterpret_cast<HWND>(lp);
                    // Pass the other HWND so the callback can check if it's the popup.
                    if (popupDismiss_(PopupDismissReason::Deactivate, other, 0, 0))
                        popupDismiss_ = nullptr;
                }
            }
            break;

        case WM_CAPTURECHANGED:
            // Another window or a caption button stole capture. Clear our internal
            // captured_ state so the next press doesn't incorrectly route to the old
            // element. Don't call ::ReleaseCapture — we don't hold it anymore.
            input_.Input().OnCaptureStolen();
            break;

        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wp);
            OnDpiChanged(newDpi, reinterpret_cast<const RECT*>(lp));
            return 0;
        }

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            float s = 96.0f / dpi_;
            // Caption-button hover is a window-level concern (not an element):
            // track it here and suppress element hover while over a caption button.
            int caption = CaptionButtonAt(px * s, py * s);
            if (caption != hotCaption_) { hotCaption_ = caption; Render(); }
            if (caption) {
                input_.Input().PointerLeftWindow();  // clear element hover under the button
            } else {
                input_.Input().PointerMoved(ToDip(px, py), CurrentModifiers());
                ArmTooltip(input_.Input().Hot());
            }
            // A move may change hover (fade/expand a scrollbar) OR, while a control
            // has captured the pointer (dragging a Slider), retarget an animation on
            // a DIFFERENT element — e.g. the Slider's ValueChanged drives a bound
            // ProgressBar's determinate ease. So the active-animation set must be
            // re-collected on drag-moves too; skipping it (an earlier optimization)
            // left the ProgressBar un-ticked until drag end. Collect walks only the
            // registered roots and is cheap relative to the per-move layout/paint.
            UpdateAnimationTimer();
            return 0;
        }
        case WM_MOUSELEAVE:
            input_.Input().PointerLeftWindow();
            if (hotCaption_) { hotCaption_ = 0; Render(); }
            HideTooltip();  // pointer left the window entirely
            UpdateAnimationTimer();  // leaving may start a scrollbar fade-out
            return 0;
        // Both press messages take the SAME path — the only difference is the click
        // count, which the counter derives. Windows sends DOWN, DBLCLK, DOWN, DBLCLK…
        // for a rapid series, so a handler that keyed off the message type alone would
        // see every third click as a fresh single press; counting time+position instead
        // is what makes triple-click possible at all (Win32 has no triple-click message).
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            HideTooltip();  // any click dismisses a visible tooltip
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            float s = 96.0f / dpi_;
            // A caption button press is window chrome, not an element event.
            int caption = CaptionButtonAt(px * s, py * s);
            if (caption) {
                // Chrome interaction breaks the streak: a double-click that lands half
                // on the caption and half in the text must not select a word.
                input_.Clicks().Reset();
                pressedCaption_ = caption;
                SetCapture(hwnd_);
                Render();
                return 0;
            }
            // GetMessageTime, not GetTickCount: it is when the message was POSTED, so a
            // slow frame between two clicks cannot stretch the measured interval and
            // break a genuine double-click. System thresholds because the user can change
            // double-click speed and pointer tolerance (see ClickCounter.h).
            const int clickCount = input_.Clicks().Register(
                static_cast<float>(px), static_cast<float>(py),
                static_cast<uint32_t>(GetMessageTime()), GetDoubleClickTime(),
                static_cast<float>(GetSystemMetrics(SM_CXDOUBLECLK)));
            // Traced because "double-click does nothing" and "double-click was never
            // delivered" look identical on screen. Seeing clickCount here separates
            // a missing CS_DBLCLKS / routing problem from a control-logic problem,
            // which is otherwise a long detour through the wrong layer.
            //
            // FL_TRACEF, not a local buffer + TraceMsg: this runs on EVERY mouse press,
            // and with a debugger attached OutputDebugStringA blocks until the debugger
            // drains it. The macro removes the formatting too, so a Release build costs
            // literally nothing here.
            FL_TRACEF("Input", "press clickCount=%d at (%d,%d)", clickCount, px, py);
            // Parent-window light-dismiss for no-activate popups first (the popup
            // decides whether to close), then route the press into the tree.
            LightDismissAt(px, py);
            input_.Input().PointerPressed(ToDip(px, py), PointerButton::Left, CurrentModifiers(),
                                  clickCount);
            UpdateAnimationTimer();
            return 0;
        }
        case WM_RBUTTONDOWN: {
            HideTooltip();
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            // Close any already-open popup first (same light-dismiss as a left
            // click), then open the context menu — otherwise a menu opened by an
            // earlier click stays visible under the new one.
            LightDismissAt(px, py);
            float s = 96.0f / dpi_;
            POINT screen = {px, py};
            ClientToScreen(hwnd_, &screen);
            // Per-control context menu (WP-03 follow-up): the element under the
            // cursor (or the nearest ancestor that owns one) opens its own menu.
            // Only if nothing in the chain has a menu do we fall back to the
            // subclass hook (a window-level background menu).
            if (UIElement* owner = input_.Input().ContextMenuOwnerAt(Point{px * s, py * s})) {
                // Give the element a chance to refresh item state and adjust its own
                // selection first — MenuItem::enabled is captured by SetItems, so a menu
                // with live conditions has to rebuild here or it shows stale state.
                owner->OnContextMenuOpening(px * s, py * s);
                owner->ContextMenu()->ShowAt(screen.x, screen.y);
            } else {
                OnClientRightClick(px * s, py * s, screen.x, screen.y);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
            float s = 96.0f / dpi_;
            if (pressedCaption_) {
                int pressed = pressedCaption_;
                pressedCaption_ = 0;
                ReleaseCapture();
                Render();
                if (CaptionButtonAt(px * s, py * s) == pressed) {
                    switch (pressed) {
                        case HTMINBUTTON:
                            SendMessageW(hwnd_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                            break;
                        case HTMAXBUTTON:
                            SendMessageW(hwnd_, WM_SYSCOMMAND,
                                         IsZoomed(hwnd_) ? SC_RESTORE : SC_MAXIMIZE, 0);
                            break;
                        case HTCLOSE:
                            SendMessageW(hwnd_, WM_SYSCOMMAND, SC_CLOSE, 0);
                            break;
                    }
                }
                return 0;
            }
            // A press not on any element still reaches the subclass hook when the
            // release lands on empty space (nothing was captured).
            bool hadCapture = input_.Input().Captured() != nullptr;
            input_.Input().PointerReleased(ToDip(px, py), PointerButton::Left, CurrentModifiers());
            // The release handler may have destroyed the window (an app Close
            // button): don't touch the torn-down window past this point.
            if (destroying_) return 0;
            if (!hadCapture && !input_.Input().HitTest(ToDip(px, py)))
                OnClientClick(px * s, py * s);
            if (destroying_) return 0;  // OnClientClick could also close the window
            UpdateAnimationTimer();  // drag-end starts the idle fade countdown
            return 0;
        }
        case WM_MOUSEWHEEL: {
            HideTooltip();  // scrolling dismisses a visible tooltip
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            input_.Input().PointerWheel(ToDip(pt.x, pt.y), GET_WHEEL_DELTA_WPARAM(wp),
                                CurrentModifiers());
            OnMouseWheel(pt.x * (96.0f / dpi_), pt.y * (96.0f / dpi_),
                         GET_WHEEL_DELTA_WPARAM(wp));
            UpdateAnimationTimer();  // a wheel may have kicked off a smooth scroll
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UINT vk = static_cast<UINT>(wp);
            // F12 toggles the on-screen performance HUD (roadmap §18.3). A global
            // diagnostics shortcut handled before element routing, so it works
            // regardless of focus.
            if (vk == VK_F12) { ToggleHud(); return 0; }
            // An active no-activate popup session (menu/combo) consumes keys first.
            // A context menu opened by right-click has no focused element, so it
            // cannot rely on the focused-element route inside InputManager.
            if (popupKey_ && popupKey_(vk)) { UpdateAnimationTimer(); return 0; }
            if (input_.Input().KeyDown(vk, CurrentModifiers())) {
                // A key may have kicked off an animation (Space/Enter toggling a
                // CheckBox/ToggleSwitch, arrow keys nudging a Slider): re-collect
                // the active set and arm the clock, same as the mouse paths.
                UpdateAnimationTimer();
                return 0;
            }
            break;  // fall through to DefWindowProc for unhandled keys
        }

        case WM_CHAR:
            input_.Input().TextInput(static_cast<wchar_t>(wp));
            return 0;

        case WM_SETCURSOR:
            // Let the element under the pointer choose its cursor (e.g. I-beam
            // for a text box). Only for the client area hit code.
            if (LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                if (UIElement* e = input_.Input().HitTest(ToDip(pt.x, pt.y))) {
                    if (HCURSOR c = e->Cursor()) {
                        SetCursor(c);
                        return TRUE;
                    }
                }
            }
            break;  // default arrow / resize cursors

        case WM_FLUENT_MODAL_TICK:
            // High-resolution modal heartbeat (ModalFrameHeartbeat.h). Posted from a
            // waitable-timer thread at the display refresh rate while a SYSTEM modal
            // move/resize loop owns the pump. This exists because the WM_TIMER
            // heartbeat below cannot go faster than ~16ms (USER_TIMER_MINIMUM is
            // 10ms and the tick is rounded up to the system timer period), which
            // capped a held-but-motionless resize border at ~30-45fps even though
            // the frame itself costs under 3ms.
            //
            // Stale ticks are expected and harmless: Stop() cannot retract messages
            // already in the queue, so a few may arrive after the modal loop ended.
            // ServiceModalFrame is safe then (it paints only if there is pending
            // work), but gate on inModalLoop_ anyway so a post-drag tick cannot
            // paint outside the state it was meant for.
            //
            // Release the heartbeat's single in-flight slot FIRST, before painting.
            // The worker suppresses a new tick while one is unconsumed (so a long
            // frame cannot queue a backlog), and "consumed" means dispatched here —
            // not "finished painting". Decrementing after the paint would idle the
            // heartbeat for the whole duration of a slow frame, which is exactly when
            // a motionless drag needs the next tick to already be on its way.
            modalHeartbeat_.OnTickConsumed();
            //
            // pace=false for the same reason as the WM_TIMER heartbeat: this tick IS
            // the pacing (the waitable timer already fires at the refresh interval),
            // so applying the skip-if-not-due test on top would double-pace and drop
            // every other frame.
            if (inModalLoop_) ServiceModalFrame(/*pace*/ false);
            return 0;

        case WM_TIMER:
            if (wp == kBlinkTimerId && input_.Focus().Focused() && input_.Focus().Focused()->WantsBlink()) {
                input_.Focus().Focused()->OnBlink();
                Render();
            } else if (wp == kTooltipTimerId) {
                // Hover delay elapsed: show the tooltip for the tracked element.
                KillTimer(hwnd_, kTooltipTimerId);
                tooltipTimerOn_ = false;
                if (tooltip_ && tooltipHot_ && tooltipHot_->HasTooltip()) {
                    POINT pt;
                    GetCursorPos(&pt);
                    tooltip_->Show(tooltipHot_->Tooltip(), pt.x, pt.y);
                }
            } else if (wp == kAnimTimerId) {
                // FALLBACK animation tick, only load-bearing inside a SYSTEM modal
                // loop (drag-resize / menu), where our message loop is not running
                // and PumpAnimations() is therefore not being called each turn.
                // Normally the main loop drives PumpAnimations() and this timer is
                // redundant (it may still fire, but PumpAnimations already advanced
                // the shared QPC clock, so ComputeDt here returns a near-zero dt and
                // the extra Tick is a no-op-sized step — no double-advance).
                //
                // Paint SYNCHRONOUSLY (RenderNow): in the modal loop a coalesced
                // RequestFrame would sit unserviced until the drag ends, freezing
                // the animation. PumpAnimations advances state + requests a frame;
                // BeginFrame/EndFrame consume it and paint right here.
                //
                // THIS IS NOW A FALLBACK ONLY. WM_ENTERSIZEMOVE arms it solely when
                // ModalFrameHeartbeat failed to start (no waitable timer, pre-1803);
                // otherwise the precise WM_FLUENT_MODAL_TICK path drives the drag and
                // this timer is never armed. It remains because it is the only
                // mechanism available on those older systems, and a motionless drag
                // must still repaint there — just at WM_TIMER's ~28ms cadence rather
                // than the panel's refresh.
                //
                // pace=false: this timer is already self-limiting at kAnimIntervalMs
                // (and then some, given WM_TIMER's rounding), and it is the ONLY
                // driver while the mouse holds still — pacing it would risk skipping
                // to the next period and halving an already-slow rate.
                ServiceModalFrame(/*pace*/ false);
            } else if (wp == kSelfFrameTimerId) {
                // Secondary windows have no dedicated application-loop entry.
                // Drive only their pending work here, then remove the timer as
                // soon as they become idle. A continuing animation re-arms at a
                // roughly display-rate interval; ordinary invalidation gets one
                // prompt frame and stops.
                // NOTE ON PACING: this path is capped by WM_TIMER, whose minimum
                // period is ~10ms (USER_TIMER_MINIMUM) regardless of what we ask
                // for — so a window driven from here cannot exceed ~100fps and on a
                // 120Hz panel will pace at roughly every other refresh. That is
                // accepted, not overlooked: this timer only runs for a secondary
                // window with NO Application (UsesExternalFramePump() == false),
                // which is the legacy path. Any window attached to an Application
                // is paced by Application::PumpOneTurn through FrameWaiter, which
                // uses a high-resolution waitable timer and does reach full refresh.
                // Fixing this path properly means giving it its own message loop,
                // which is precisely what Application exists to avoid duplicating.
                KillTimer(hwnd_, kSelfFrameTimerId);
                PumpAnimations();
                RunDueFrame();
                if (IsAnimating() || NeedsFrame())
                    SetTimer(hwnd_, kSelfFrameTimerId,
                             std::max<UINT>(1u, FrameWaitTimeoutMs()), nullptr);
            }
            return 0;

        case WM_IME_STARTCOMPOSITION:
            if (UIElement* f = input_.Focus().Focused()) {
                f->OnImeStartComposition(hwnd_);
                UpdateImePosition();
                return 0;  // we draw the composition string ourselves
            }
            break;
        case WM_IME_COMPOSITION:
            if (UIElement* f = input_.Focus().Focused()) {
                f->OnImeComposition(hwnd_, lp);
                UpdateImePosition();
                Render();
                return 0;
            }
            break;
        case WM_IME_ENDCOMPOSITION:
            if (UIElement* f = input_.Focus().Focused()) {
                f->OnImeEndComposition(hwnd_);
                Render();
                return 0;
            }
            break;

        case WM_DEVICECHANGE:
            OnDeviceChange(wp, lp);
            return TRUE;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const ColorTokens& pal = theme_.Manager().Snapshot().colors;
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, RGB(static_cast<int>(pal.textPrimary.r * 255.0f),
                                  static_cast<int>(pal.textPrimary.g * 255.0f),
                                  static_cast<int>(pal.textPrimary.b * 255.0f)));
            SetBkColor(hdc, RGB(static_cast<int>(pal.cardFill.r * 255.0f),
                                static_cast<int>(pal.cardFill.g * 255.0f),
                                static_cast<int>(pal.cardFill.b * 255.0f)));
            return reinterpret_cast<LRESULT>(controlBgBrush_ ? controlBgBrush_ : GetStockObject(NULL_BRUSH));
        }

        case WM_ERASEBKGND:
            return 1;  // DComp owns the content; no GDI erase

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_DESTROY:
            // Mark teardown so an input message that synchronously triggered this
            // (e.g. a Close button's handler) skips its trailing member access
            // after unwinding back out of the nested WM_DESTROY.
            destroying_ = true;
            KillTimer(hwnd_, kSelfFrameTimerId);
            // Revoke the OLE drop registration while hwnd_ is still valid. OLE holds
            // a BORROWED pointer to this object (AddRef/Release are no-ops — see the
            // header), so this must happen before the window object can go away or
            // the OS would be left with a dangling IDropTarget.
            if (dragDropRegistered_) {
                RevokeDragDrop(hwnd_);
                dragDropRegistered_ = false;
                OleUninitialize();
            }
            dragOverElement_ = nullptr;
            // Stop the modal heartbeat BEFORE the HWND is cleared. Its worker posts
            // to hwnd_, so a tick in flight past this point would target a destroyed
            // window. Stop() joins the worker, so once it returns no further post can
            // happen. Normally WM_EXITSIZEMOVE already stopped it, but a window can be
            // destroyed from inside a drag (an owner closing it), so this is the
            // backstop rather than the usual path.
            modalHeartbeat_.Stop();
            // Close any active popup while the owning elements are still alive and
            // the HWND is valid, so a control never tears down mid-dismiss
            // (roadmap §5.5, single deterministic shutdown path).
            CloseActivePopups();
            OnDestroying();
            // Detach every registered root while it is still alive and the window
            // members (anim registry, device) are valid (roadmap §6.3). This
            // releases context-scoped subscriptions and drops animations now, and
            // clears each element's context so a later element destructor (if it
            // outlives the window) never dereferences the by-then-dead registry.
            for (UIElement* e : elements_) {
                if (!e) continue;
                // Clear the element from the input/focus state before it detaches
                // so no manager keeps a dangling hot/captured/focused pointer.
                input_.Input().OnElementDetached(e);
                if (e->IsAttached()) e->DetachFromContext();
            }
            hwnd_ = nullptr;
            OnDestroyed();
            if (application_) application_->DetachWindow(*this);
            else if (ShouldPostQuitOnDestroy()) PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT NativeWindowHost::HitTestNca(POINT ptScreen) {
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    // WHERE THE BAND ACTUALLY IS. WM_NCCALCSIZE shrinks only the CLIENT rect; the WINDOW
    // rect is untouched. So the reserved non-client band lies INSIDE the window rect,
    // between the window edge and the client edge:
    //
    //     window rect   [rc.left,          rc.right)
    //     client rect   [rc.left + inset,  rc.right - inset)
    //     right NC band [rc.right - inset, rc.right)          <-- inside rc
    //
    // The band must therefore be claimed on the INSIDE of each edge. An earlier revision
    // tested [rc.right - 1, rc.right + outerPx) — i.e. 1px of the real band plus a stretch
    // of pure outside — which left inset-1 px of genuine non-client area falling through
    // to HTCLIENT at the bottom of this function, and leaned on an outside band that a
    // frameless window never receives WM_NCHITTEST for. That is why the sides felt so much
    // thinner than the bottom despite identical arithmetic: only ~1px of each was live.
    //
    // `tolerancePx` extends each band a little past the window edge. It costs nothing when
    // those coordinates are not delivered, and helps on the configurations where they are.
    const int insetPx = ResizeGripOuterPx(dpi_);
    constexpr int tolerancePx = 4;

    bool left = false, right = false, top = false, bottom = false;

    // Left/right/bottom: the full reserved band, inside the window rect, plus tolerance.
    // Top: no NC band exists (a top inset makes DWM draw its own caption over ours — the
    // double-title-bar artifact), so the top grip has to live inside the CLIENT area and
    // compete with the title bar. It is kept at insetPx for consistency of feel.
    if (ptScreen.x >= rc.left - tolerancePx && ptScreen.x < rc.left + insetPx)
        left = true;
    if (ptScreen.x >= rc.right - insetPx && ptScreen.x < rc.right + tolerancePx)
        right = true;
    if (ptScreen.y >= rc.top && ptScreen.y < rc.top + insetPx)
        top = true;
    if (ptScreen.y >= rc.bottom - insetPx && ptScreen.y < rc.bottom + tolerancePx)
        bottom = true;

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    // Title bar drag region (top strip, excluding any element hit areas).
    int titleBarPx = static_cast<int>(TitleBarHeightDip() * DpiScale() + 0.5f);
    if (ptScreen.y >= rc.top && ptScreen.y < rc.top + titleBarPx) {
        // If an interactive element sits here, let it receive the click.
        float dipX = (ptScreen.x - rc.left) * 96.0f / dpi_;
        float dipY = (ptScreen.y - rc.top) * 96.0f / dpi_;
        if (CaptionButtonAt(dipX, dipY)) return HTCLIENT;
        if (input_.Input().HitTest(Point{dipX, dipY})) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

void NativeWindowHost::OnSize(UINT pixelW, UINT pixelH) {
    if (!graphicsReady_) return;
    // Phase 1A (resize coalescing): a drag fires WM_SIZE at ~per-pixel frequency;
    // doing a full ResizeBuffers + OnLayout + synchronous RenderNow on each one
    // repaints every intermediate size and starves everything else (the animation
    // fallback timer, in particular). Instead, just record the latest size and
    // request a Resize frame. ApplyPendingResize() does the real work once per
    // painted frame — from RunDueFrame() (normal loop) or, inside the system modal
    // resize loop where our loop is not spinning, from the kAnimTimerId heartbeat
    // (see WM_TIMER) and WM_EXITSIZEMOVE. Result: at most one ResizeBuffers + one
    // OnLayout + one paint per frame regardless of WM_SIZE burst rate.
    pendingPixelW_ = std::max<UINT>(1, pixelW);
    pendingPixelH_ = std::max<UINT>(1, pixelH);
    resizePending_ = true;
    RequestFrame(FrameReason::Resize);
}

void NativeWindowHost::ApplyPendingResize() {
    if (!resizePending_ || !graphicsReady_) return;

    const int64_t applyStart = QpcNow();

    // Time this and carry it to the next frame's stats. It runs BEFORE RenderNow
    // starts its own clock, so without this the whole cost of a resize — the surface
    // resize plus the OnLayout it forces, which for a text control means re-wrapping
    // at the new width — is absent from every number on the HUD. See
    // FrameStats::resizeMs.
    ScopedTimer resizeTimer(pendingResizeMs_);
    resizePending_ = false;
    // Coalesce: only the most recent size matters. Skip the swap-chain work if the
    // committed size already matches (e.g. a maximize that lands on the same rect).
    const bool sizeChanged = (pendingPixelW_ != pixelW_) || (pendingPixelH_ != pixelH_);
    pixelW_ = pendingPixelW_;
    pixelH_ = pendingPixelH_;

    const int64_t beforeCompResize = QpcNow();
    if (sizeChanged)
        graphics_.Comp().Resize(pixelW_, pixelH_);  // project documentation #7: resize in place, no rebuild
    const int64_t afterCompResize = QpcNow();

    // WP-07 §S4: a resized content surface has undefined pixels in the newly exposed
    // area — force a full clear for the next 2 frames so no partial redraw trusts them.
    fullDrawRequired_ = 2u;

    // Layout runs every resize frame. The GPU-transform alternatives (uniform root
    // scale, or 1:1 anchoring with a blank strip) were implemented, measured, and
    // deleted: both were fast but looked wrong — scale blurred and squashed the
    // content, anchoring exposed white edges on the right/bottom. What made the
    // per-frame layout affordable instead is UIContext::inModalResize, which skips
    // the DComp surface rasterization inside ScrollContentHost during the drag and
    // converges on WM_EXITSIZEMOVE. That kept the geometry exact at every frame and
    // took layout from 7-13ms to 2-3ms, so there is nothing left to trade away here.
    const int64_t beforeLayout = QpcNow();
    OnLayout();
    const int64_t afterLayout = QpcNow();

    // 细分打印：graphics_.Comp().Resize 和 OnLayout 分别的耗时
    const double compResizeMs = QpcToMs(afterCompResize - beforeCompResize);
    const double layoutMs = QpcToMs(afterLayout - beforeLayout);
    const double totalMs = QpcToMs(afterLayout - applyStart);

    if (!resizeTraceLabel_.empty()) {
        // measure/arrange come from Window::OnLayout via FrameStats — read them right
        // after the OnLayout above so they describe THIS resize, not a later frame.
        // Printed on the same line as the total so no manual correlation between two
        // trace streams is needed: one line says how long, which half, and how many
        // elements were actually re-measured.
        const FrameStats& fs = lastFrameStats_;
        FL_TRACEF("Resize",
                  "Page=[%ls] size=%ux%u  total=%.3fms (surface=%.3f + layout=%.3f "
                  "[measure=%.3f arrange=%.3f]) measured=%d hits=%d",
                  resizeTraceLabel_.c_str(), pixelW_, pixelH_,
                  totalMs, compResizeMs, layoutMs, fs.measureMs, fs.arrangeMs,
                  LayoutCostProbe::GetCount(LayoutCountKey::MeasureCalls),
                  LayoutCostProbe::GetCount(LayoutCountKey::MeasureCacheHits));
        // Per-control breakdown for whichever suspects are instrumented. Silent when
        // nothing recorded, so an uninstrumented page costs one loop over 6 ints.
        LayoutCostProbe::Report("    ");
    }
}

double NativeWindowHost::SampleModalGap() {
    const int64_t now = QpcNow();
    if (modalLastOpportunityQpc_ != 0)
        modalGapMs_ = UpdateOpportunityGap(
            modalGapMs_, QpcToMs(now - modalLastOpportunityQpc_));
    modalLastOpportunityQpc_ = now;
    return modalGapMs_;
}

void NativeWindowHost::ServiceModalFrame(bool pace) {
    if (!graphicsReady_) return;
    // Measure the opportunity spacing BEFORE any early return, and for EVERY caller
    // (heartbeat included, paced or not). The estimate must describe how often we are
    // being given the chance to paint, which is a property of the message stream, not
    // of the frames we chose to accept. Sampling only where the decision is made
    // would feed it the intervals between PAINTS instead — a circular estimate that
    // converges on the refresh interval and stops adapting to the mouse rate.
    const double gapMs = SampleModalGap();
    // Advance animation on the real QPC clock, then paint once — SYNCHRONOUSLY,
    // because inside a modal loop a coalesced RequestFrame sits unserviced until
    // the drag ends (RunDueFrame is not being called). Paint only when there is
    // something to show: a pending resize, a live animation, or an already-pending
    // frame. A pure window *move* with nothing animating must not repaint at all.
    const bool anim = PumpAnimations();
    if (!resizePending_ && !anim && !frame_.Scheduler().NeedsFrame()) return;

    // Pace to the display, like the main loop does. This path CANNOT wait (it runs
    // synchronously inside the system's modal pump, and blocking here would add
    // latency to the drag), so instead skip a frame that is not due yet. The pending
    // request is deliberately left unconsumed, so the next message — or the heartbeat,
    // or WM_EXITSIZEMOVE's final synchronous paint — renders it once it is due.
    // Nothing is dropped; it is only deferred to the refresh boundary.
    //
    // Only for the MOUSE-driven callers: WM_SIZE / WM_MOVING arrive at the mouse's
    // report rate, unrelated to vsync, so a drag otherwise paints once per mouse event
    // (a measured session showed the Moving phase at 132fps on a 120Hz panel; a
    // 500/1000Hz mouse wastes proportionally more). The heartbeat must NOT be paced:
    // it is already self-limiting at kAnimIntervalMs, and Win32 timers are coarse
    // (~15.6ms) and fire early, so on a 60Hz panel a heartbeat landing at 15ms would
    // see 1.67ms remaining, skip, and wait a whole further period — halving a
    // motionless drag to ~32fps. It is the only driver when the mouse holds still.
    //
    // WHY THIS IS NOT FrameIsDue(FrameWaitRemaining()). That predicate is right for
    // the main loop, which can WAIT until the deadline and then paint exactly on it.
    // Here the opportunities are DISCRETE — one per mouse message — and we cannot
    // wait, so a frame that is "not due yet" is not deferred by the fraction of a
    // millisecond that remains: it is deferred by a whole mouse-report interval.
    // Applying the main loop's threshold here aliases badly against the mouse rate.
    // Simulated over a second of dragging on a 120Hz panel with a 125Hz mouse:
    //
    //   jitter          0.0ms  0.5ms  1.0ms  2.0ms
    //   old (>= 7.33ms)   125    120     93     85
    //   main-loop rule    125     78     78     78    <- worse, threshold even tighter
    //   nearest-opp       125    125    125    124
    //
    // FrameIsNearestOpportunity instead picks whichever of "paint now, early by `remaining`"
    // and "paint at the next opportunity, late by `gap - remaining`" lands closer to
    // the deadline. It is self-tuning: a 1000Hz mouse gets a tight threshold (so we
    // do not paint faster than the panel), a 125Hz mouse a loose one (so jitter never
    // costs a whole frame). Passing gap == 0 degrades to "always paint", which is
    // exactly the unpaced heartbeat behaviour, so the pace==false path is unchanged.
    if (pace &&
        !FrameIsNearestOpportunity(FrameWaitRemaining(), gapMs))
        return;

    FrameReason r = frame_.Scheduler().BeginFrame();
    ApplyPendingResize();
    RenderNow(AnyReason(r) ? r : FrameReason::Animation);
    frame_.Scheduler().EndFrame();
}

void NativeWindowHost::OnDpiChanged(UINT newDpi, const RECT* suggested) {
    const UINT oldDpi = dpi_;
    dpi_ = newDpi ? newDpi : 96;
    if (suggested) {
        FL_TRACEF(kTag, "DPI: %u -> %u scale=%.3f suggested=(%ld,%ld,%ldx%ld)",
                  oldDpi, dpi_, DpiScale(), suggested->left, suggested->top,
                  suggested->right - suggested->left,
                  suggested->bottom - suggested->top);
    } else {
        FL_TRACEF(kTag, "DPI: %u -> %u scale=%.3f suggested=(none)",
                  oldDpi, dpi_, DpiScale());
    }
    // A DPI change usually means the window moved to another monitor, which may run at
    // a different refresh rate — re-query it so frame pacing follows the new panel.
    InvalidateRefreshHz();
    // Keep the injected context's DPI current so anything reading Context() after
    // a monitor change sees the new scale (roadmap §6.2). Existing attached
    // elements cache the context by value; they still get OnDpiChanged below for
    // the per-DPI resource refresh.
    context_.dpiScale = DpiScale();
    // Bump the resource-cache epoch on a DPI change too (roadmap §13.3): DIP-space
    // layouts are DPI-independent so nothing rebuilds today, but a future
    // rasterized/realized resource keyed by DPI would — the backstop is free.
    graphics_.Cache().BumpEpoch();
    // WP-07 §S4: a DPI change repositions/resizes and rebuilds layout — force a
    // full clear for the next 2 frames (same reason as OnSize).
    fullDrawRequired_ = 2u;
    if (suggested) {
        // project documentation: honor the suggested rect.
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // Every element stores UIContext by value. Refresh those copies BEFORE the
    // notification: composited controls read Context().dpiScale while rebuilding
    // their pixel-space visuals in OnDpiChanged.
    for (UIElement* e : elements_) {
        if (!e) continue;
        e->UpdateContextDpi(DpiScale());
        e->OnDpiChanged(DpiScale());
    }
    // WM_DPICHANGED runs in a system modal path; paint synchronously and consume
    // any coalesced request so the on-demand loop doesn't repaint a stale frame.
    // Phase 1A: the SetWindowPos above fired a synchronous WM_SIZE that only set
    // resizePending_ (deferred). Commit it here — ApplyPendingResize does the
    // surface Resize + OnLayout — so the content surface matches the new size/DPI before
    // we paint. If no size actually changed, force the layout the DPI change needs.
    frame_.Scheduler().BeginFrame();
    if (resizePending_) ApplyPendingResize();
    else OnLayout();
    RenderNow(FrameReason::Dpi);
    frame_.Scheduler().EndFrame();
}

RectDip NativeWindowHost::CaptionButtonRect(int hit) const {
    float w = static_cast<float>(pixelW_) / DpiScale();
    float h = TitleBarHeightDip();
    float x = w;
    switch (hit) {
        case HTCLOSE:    x -= kCaptionBtnWidthDip; break;
        case HTMAXBUTTON: x -= kCaptionBtnWidthDip * 2.0f; break;
        case HTMINBUTTON: x -= kCaptionBtnWidthDip * 3.0f; break;
        default: return {};
    }
    return {x, 0.0f, kCaptionBtnWidthDip, h};
}

int NativeWindowHost::CaptionButtonAt(float dipX, float dipY) const {
    if (CaptionButtonRect(HTCLOSE).contains(dipX, dipY)) return HTCLOSE;
    if (CaptionButtonRect(HTMAXBUTTON).contains(dipX, dipY)) return HTMAXBUTTON;
    if (CaptionButtonRect(HTMINBUTTON).contains(dipX, dipY)) return HTMINBUTTON;
    return 0;
}

void NativeWindowHost::DrawCaptionButtons(ID2D1DeviceContext* dc, float) {
    const ColorTokens& pal = theme_.Manager().Snapshot().colors;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(pal.textPrimary, brush.GetAddressOf()))) return;

    auto fillButton = [&](int hit) {
        RectDip r = CaptionButtonRect(hit);
        if (r.w <= 0) return;
        if (pressedCaption_ == hit) {
            brush->SetColor(hit == HTCLOSE
                ? D2D1::ColorF(0.76f, 0.12f, 0.12f, 0.85f)
                : pal.controlFillPressed);
            dc->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush.Get());
        } else if (hotCaption_ == hit) {
            brush->SetColor(hit == HTCLOSE
                ? D2D1::ColorF(0.91f, 0.15f, 0.15f, 0.95f)
                : pal.controlFillHover);
            dc->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush.Get());
        }
    };

    fillButton(HTMINBUTTON);
    fillButton(HTMAXBUTTON);
    fillButton(HTCLOSE);

    auto glyphColor = [&](int hit) -> D2D1_COLOR_F {
        if (hit == HTCLOSE && (hotCaption_ == hit || pressedCaption_ == hit))
            return D2D1::ColorF(1, 1, 1, 1);
        return pal.textPrimary;
    };

    auto center = [](const RectDip& r) {
        return D2D1::Point2F(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    };

    // Minimize: horizontal line.
    RectDip minR = CaptionButtonRect(HTMINBUTTON);
    D2D1_POINT_2F c = center(minR);
    brush->SetColor(glyphColor(HTMINBUTTON));
    dc->DrawLine(D2D1::Point2F(c.x - kCaptionGlyphDip * 0.5f, c.y + 4.0f),
                 D2D1::Point2F(c.x + kCaptionGlyphDip * 0.5f, c.y + 4.0f),
                 brush.Get(), 1.25f);

    // Maximize/restore: square outline.
    RectDip maxR = CaptionButtonRect(HTMAXBUTTON);
    c = center(maxR);
    brush->SetColor(glyphColor(HTMAXBUTTON));
    bool zoomed = IsZoomed(hwnd_) != FALSE;
    if (zoomed) {
        dc->DrawRectangle(D2D1::RectF(c.x - 3.0f, c.y - 5.0f, c.x + 5.0f, c.y + 3.0f), brush.Get(), 1.0f);
        dc->DrawRectangle(D2D1::RectF(c.x - 5.0f, c.y - 3.0f, c.x + 3.0f, c.y + 5.0f), brush.Get(), 1.0f);
    } else {
        dc->DrawRectangle(D2D1::RectF(c.x - 5.0f, c.y - 5.0f, c.x + 5.0f, c.y + 5.0f), brush.Get(), 1.0f);
    }

    // Close: X.
    RectDip closeR = CaptionButtonRect(HTCLOSE);
    c = center(closeR);
    brush->SetColor(glyphColor(HTCLOSE));
    dc->DrawLine(D2D1::Point2F(c.x - 5.0f, c.y - 5.0f),
                 D2D1::Point2F(c.x + 5.0f, c.y + 5.0f), brush.Get(), 1.25f);
    dc->DrawLine(D2D1::Point2F(c.x + 5.0f, c.y - 5.0f),
                 D2D1::Point2F(c.x - 5.0f, c.y + 5.0f), brush.Get(), 1.25f);
}

void NativeWindowHost::SetIconResource(UINT resourceId) {
    if (iconResourceId_ == resourceId) return;
    iconResourceId_ = resourceId;
    FL_TRACEF(kTag, "icon resource set id=%u hwnd=%p", resourceId, hwnd_);
    titleIconBitmap_.Reset();
    if (hwnd_) {
        RecreateWindowIcon();
        Render();
    }
}

void NativeWindowHost::RecreateWindowIcon() {
    titleIconBitmap_.Reset();
    if (!hwnd_ || !hInst_) {
        FL_TRACEF(kTag, "icon skipped id=%u hwnd=%p instance=%p",
                  iconResourceId_, hwnd_, hInst_);
        return;
    }

    HICON icon = nullptr;
    if (iconResourceId_) {
        HRSRC resource = FindResourceW(hInst_, MAKEINTRESOURCEW(iconResourceId_), RT_GROUP_ICON);
        FL_TRACEF(kTag, "icon resource id=%u found=%d", iconResourceId_, resource ? 1 : 0);
        SetLastError(ERROR_SUCCESS);
        icon = static_cast<HICON>(LoadImageW(
            hInst_, MAKEINTRESOURCEW(iconResourceId_), IMAGE_ICON, 0, 0,
            LR_DEFAULTSIZE | LR_SHARED));
        FL_TRACEF(kTag, "LoadImageW icon=%p error=%lu", icon, GetLastError());
    }
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    if (!icon || !graphics_.D2D().DC()) {
        TraceMsg(kTag, "icon bitmap skipped: icon or D2D context unavailable");
        return;
    }

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    Trace(kTag, "icon WIC factory", hr);
    if (SUCCEEDED(hr)) {
        hr = factory->CreateBitmapFromHICON(icon, bitmap.GetAddressOf());
        Trace(kTag, "icon CreateBitmapFromHICON", hr);
    }
    if (SUCCEEDED(hr)) {
        UINT width = 0, height = 0;
        bitmap->GetSize(&width, &height);
        FL_TRACEF(kTag, "icon WIC size=%ux%u", width, height);
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        Trace(kTag, "icon CreateFormatConverter", hr);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeMedianCut);
        Trace(kTag, "icon converter Initialize(PBGRA)", hr);
    }
    if (SUCCEEDED(hr)) {
        hr = graphics_.D2D().DC()->CreateBitmapFromWicBitmap(converter.Get(), nullptr,
                                                  titleIconBitmap_.GetAddressOf());
        Trace(kTag, "icon CreateBitmapFromWicBitmap", hr);
    }
    FL_TRACEF(kTag, "icon ready=%d bitmap=%p", titleIconBitmap_ ? 1 : 0,
              titleIconBitmap_.Get());
}

void NativeWindowHost::DrawWindowIcon(ID2D1DeviceContext* dc) {
    if (!titleIconBitmap_ || !dc) return;
    constexpr float size = 16.0f;
    const float top = (TitleBarHeightDip() - size) * 0.5f;
    dc->DrawBitmap(titleIconBitmap_.Get(), D2D1::RectF(10.0f, top, 10.0f + size, top + size),
                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void NativeWindowHost::DrawStandardTitle(ID2D1DeviceContext* dc) {
    if (!UsesStandardTitleBar() || !dc) return;
    const wchar_t* text = TitleText();
    if (!text || !*text) return;

    const auto& theme = theme_.Manager().Snapshot();
    IDWriteTextFormat* format = graphics_.DWrite().Format(
        theme.typography.subtitleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
        DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!format) return;

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(theme.colors.textPrimary, brush.GetAddressOf()))) return;
    const float left = titleIconBitmap_ ? 34.0f : 12.0f;
    const float right = std::max(left, ClientWidthDip() - kCaptionBtnWidthDip * 3.0f);
    dc->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format,
                  D2D1::RectF(left, 0.0f, right, TitleBarHeightDip()), brush.Get());
}

void NativeWindowHost::UpdateNativeTitle(const std::wstring& title) {
    title_ = title;
    if (!hwnd_) return;
    SetWindowTextW(hwnd_, title_.c_str());
    SetPropW(hwnd_, L"FluentUI.AccessibleName", const_cast<wchar_t*>(title_.c_str()));
    Render();
}

// ---------------------------------------------------------------------------
// Input translation: window -> InputManager
// ---------------------------------------------------------------------------

Point NativeWindowHost::ToDip(int px, int py) const {
    float s = 96.0f / dpi_;
    return Point{px * s, py * s};
}

ModifierKeys NativeWindowHost::CurrentModifiers() {
    ModifierKeys m = ModifierKeys::None;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ModifierKeys::Ctrl;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= ModifierKeys::Shift;
    if (GetKeyState(VK_MENU) & 0x8000) m |= ModifierKeys::Alt;
    return m;
}

void NativeWindowHost::InvalidateThunk(void* ctx) {
    static_cast<NativeWindowHost*>(ctx)->Render();
}

void NativeWindowHost::ArmTooltip(UIElement* hovered) {
    // The hovered element hasn't changed: keep whatever state we're in (either a
    // pending timer counting down or a tooltip already shown). Don't restart.
    if (hovered == tooltipHot_) return;

    tooltipHot_ = hovered;

    // Moving to a different element hides any visible tooltip and cancels a
    // pending one. We re-arm only if the new element actually has tooltip text.
    if (tooltip_ && tooltip_->IsVisible()) tooltip_->Hide();
    if (tooltipTimerOn_) {
        KillTimer(hwnd_, kTooltipTimerId);
        tooltipTimerOn_ = false;
    }

    if (hovered && hovered->HasTooltip() && tooltip_) {
        SetTimer(hwnd_, kTooltipTimerId, kTooltipDelayMs, nullptr);
        tooltipTimerOn_ = true;
    }
}

void NativeWindowHost::HideTooltip() {
    if (tooltipTimerOn_) {
        KillTimer(hwnd_, kTooltipTimerId);
        tooltipTimerOn_ = false;
    }
    if (tooltip_ && tooltip_->IsVisible()) tooltip_->Hide();
    tooltipHot_ = nullptr;
}

void NativeWindowHost::LightDismissAt(int px, int py) {
    if (!popupDismiss_) return;
    POINT pt = {px, py};
    ClientToScreen(hwnd_, &pt);
    if (popupDismiss_(PopupDismissReason::Click, nullptr, pt.x, pt.y))
        popupDismiss_ = nullptr;
}

void NativeWindowHost::UpdateImePosition() {
    UIElement* f = input_.Focus().Focused();
    if (!f) return;
    RectDip caret;
    if (!f->CaretRectDip(caret)) return;

    // Convert the caret (window DIPs) to client pixels and ask the IME to put
    // its composition + candidate window at the caret's bottom-left.
    float s = DpiScale();
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;

    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = static_cast<LONG>(caret.x * s + 0.5f);
    cf.ptCurrentPos.y = static_cast<LONG>(caret.y * s + 0.5f);
    ImmSetCompositionWindow(himc, &cf);

    // Also place the candidate list right below the caret.
    CANDIDATEFORM candf = {};
    candf.dwIndex = 0;
    candf.dwStyle = CFS_CANDIDATEPOS;
    candf.ptCurrentPos.x = cf.ptCurrentPos.x;
    candf.ptCurrentPos.y = static_cast<LONG>(caret.bottom() * s + 0.5f);
    ImmSetCandidateWindow(himc, &candf);

    ImmReleaseContext(hwnd_, himc);
}

// ---------------------------------------------------------------------------
// Window state capture / restore (plain data; persistence is the caller's job)
// ---------------------------------------------------------------------------

void NativeWindowHost::ApplyRestoreState(RECT& rect, UINT dpi,
                                       const WindowState* restore) {
    if (!restore || !restore->valid) return;

    // `dpi` is the PRIMARY monitor's DPI (resolved before any window exists). But
    // the saved rect may live on a DIFFERENT monitor — rescaling by the primary
    // DPI then wrongly grows/shrinks the frame (the size looks right on the
    // primary but is doubled/halved on the actual target monitor). Use the DPI of
    // the monitor the saved rect lands on as the rescale target (project documentation #1).
    UNREFERENCED_PARAMETER(dpi);
    RECT savedRect = {restore->x, restore->y,
                      restore->x + restore->width, restore->y + restore->height};
    HMONITOR mon = MonitorFromRect(&savedRect, MONITOR_DEFAULTTONULL);
    if (!mon) {  // saved rect off-screen (monitor unplugged): keep the fallback.
        TraceMsg(kTag, "saved window rect off-screen, using centered default");
        return;
    }
    UINT targetDpi = 96, ydpi = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &targetDpi, &ydpi)) || !targetDpi)
        targetDpi = 96;

    UINT savedDpi = restore->dpi ? restore->dpi : 96;
    int w = (targetDpi != savedDpi) ? MulDiv(restore->width, targetDpi, savedDpi)
                                    : restore->width;
    int h = (targetDpi != savedDpi) ? MulDiv(restore->height, targetDpi, savedDpi)
                                    : restore->height;

    POINT minSize{0, 0};
    if (MinTrackSizePx(minClientWDip_, minClientHDip_, targetDpi, minSize)) {
        w = std::max(w, static_cast<int>(minSize.x));
        h = std::max(h, static_cast<int>(minSize.y));
    }

    WindowState candidate = *restore;
    candidate.width = w;
    candidate.height = h;

    // Validate the placement is still on a connected display; if the monitor is
    // gone, keep the centered fallback rect that was passed in.
    if (!candidate.OnMonitor()) {
        TraceMsg(kTag, "saved window rect off-screen, using centered default");
        return;
    }

    // Clamp into the target monitor's work area so the caption bar stays visible
    // and grabbable. A window dragged mostly off-screen (only an edge showing)
    // otherwise leaves no reachable title bar to drag — and it would restore to
    // that same stuck spot on the next launch. If the window is larger than the
    // work area, align its top-left so at least the caption is on-screen.
    int rx = restore->x, ry = restore->y;
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfo(mon, &mi)) {
        const RECT& wa = mi.rcWork;
        if (rx + w > wa.right)  rx = wa.right - w;
        if (rx < wa.left)       rx = wa.left;
        if (ry + h > wa.bottom) ry = wa.bottom - h;
        if (ry < wa.top)        ry = wa.top;
    }

    rect = {rx, ry, rx + w, ry + h};
    restoreMaximized_ = restore->maximized;
}

void NativeWindowHost::SetMinClientSizeDip(float wDip, float hDip) {
    minClientWDip_ = wDip > 0.0f ? wDip : 0.0f;
    minClientHDip_ = hDip > 0.0f ? hDip : 0.0f;
    if (!hwnd_) return;  // applied by WM_GETMINMAXINFO once the window exists

    // A new limit larger than the current size would otherwise not bite until the user
    // next dragged an edge, leaving the window sitting below its own minimum. Grow it
    // now. Skipped while maximized/minimized: the placement is not the user's restored
    // size, and WM_GETMINMAXINFO will enforce the limit when it is restored.
    if (IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
    POINT minPx = {0, 0};
    if (!MinTrackSizePx(minClientWDip_, minClientHDip_, dpi_, minPx)) return;
    RECT rc;
    if (!GetWindowRect(hwnd_, &rc)) return;
    const LONG w = rc.right - rc.left, h = rc.bottom - rc.top;
    const LONG newW = w < minPx.x ? minPx.x : w;
    const LONG newH = h < minPx.y ? minPx.y : h;
    if (newW == w && newH == h) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, newW, newH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

WindowState NativeWindowHost::CaptureWindowState() const {
    WindowState s;
    if (!hwnd_) return s;

    // GetWindowPlacement gives the normal (restored) rect plus the maximized
    // flag, so we persist the un-maximized size even when closing maximized.
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (!GetWindowPlacement(hwnd_, &wp)) return s;

    const RECT& n = wp.rcNormalPosition;
    s.x = n.left;
    s.y = n.top;
    s.width = std::max<int>(1, n.right - n.left);
    s.height = std::max<int>(1, n.bottom - n.top);
    s.maximized = (wp.showCmd == SW_SHOWMAXIMIZED) || (IsZoomed(hwnd_) != FALSE);
    s.dpi = dpi_;
    s.valid = true;
    return s;
}

// ────────────────────────────────────────────────────────────────────────────
// IDropTarget implementation — routes OS drag-drop to element handlers
// ────────────────────────────────────────────────────────────────────────────

STDMETHODIMP NativeWindowHost::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

Point NativeWindowHost::DropPointToClientDip(POINTL pt) const {
    POINT client = {pt.x, pt.y};
    ScreenToClient(hwnd_, &client);
    const float scale = DpiScale();
    return {client.x / scale, client.y / scale};
}

UIElement* NativeWindowHost::FindDropTarget(Point dip) const {
    // Reverse-walk so the top-most (last-registered) root gets first hit, matching
    // the paint order the same way HitTestDeep does.
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it) {
        if (!*it) continue;
        if (UIElement* target = (*it)->HitTestDropTarget(dip.x, dip.y))
            return target;
    }
    return nullptr;
}

// The proposed effect for a drag, given what the source allows. Copy is
// preferred (the common file-drop case); Move and Link are accepted when Copy
// is not on offer. A source that allows nothing yields None.
static DragDropEffect PreferredEffect(DWORD allowed) {
    if (allowed & DROPEFFECT_COPY) return DragDropEffect::Copy;
    if (allowed & DROPEFFECT_MOVE) return DragDropEffect::Move;
    if (allowed & DROPEFFECT_LINK) return DragDropEffect::Link;
    return DragDropEffect::None;
}

STDMETHODIMP NativeWindowHost::DragEnter(IDataObject* data, DWORD /*keyState*/,
                                        POINTL pt, DWORD* effect) {
    if (!effect) return E_POINTER;

    // Cache the data object for the whole gesture: DragOver does NOT receive it,
    // and a target that wants to decide its effect from the payload (accept files
    // but not text) needs it on every move, not only on enter and drop.
    input_.DragData() = data;

    const Point dip = DropPointToClientDip(pt);
    UIElement* target = FindDropTarget(dip);
    dragOverElement_ = target;

    if (target && target->DropTarget()) {
        DragEventArgs args{dip, PreferredEffect(*effect), data};
        *effect = static_cast<DWORD>(target->DropTarget()->OnDragEnter(args));
    } else {
        *effect = DROPEFFECT_NONE;
    }
    return S_OK;
}

STDMETHODIMP NativeWindowHost::DragOver(DWORD /*keyState*/, POINTL pt, DWORD* effect) {
    if (!effect) return E_POINTER;

    const Point dip = DropPointToClientDip(pt);
    UIElement* target = FindDropTarget(dip);

    // Crossing from one target to another must look like a leave + an enter to the
    // two elements involved — the OS only reports coordinates, so the transition is
    // ours to synthesize. Without this, an element keeps whatever hover affordance
    // it drew when the pointer moves to a sibling.
    if (target != dragOverElement_) {
        if (dragOverElement_ && dragOverElement_->DropTarget())
            dragOverElement_->DropTarget()->OnDragLeave();
        dragOverElement_ = target;
        if (target && target->DropTarget()) {
            DragEventArgs args{dip, PreferredEffect(*effect), input_.DragData().Get()};
            *effect = static_cast<DWORD>(target->DropTarget()->OnDragEnter(args));
            return S_OK;   // enter already reported the effect for this position
        }
    }

    if (target && target->DropTarget()) {
        DragEventArgs args{dip, PreferredEffect(*effect), input_.DragData().Get()};
        *effect = static_cast<DWORD>(target->DropTarget()->OnDragOver(args));
    } else {
        *effect = DROPEFFECT_NONE;
    }
    return S_OK;
}

STDMETHODIMP NativeWindowHost::DragLeave() {
    if (dragOverElement_ && dragOverElement_->DropTarget())
        dragOverElement_->DropTarget()->OnDragLeave();
    dragOverElement_ = nullptr;
    input_.DragData().Reset();
    return S_OK;
}

STDMETHODIMP NativeWindowHost::Drop(IDataObject* data, DWORD /*keyState*/, POINTL pt,
                                   DWORD* effect) {
    if (!effect) return E_POINTER;

    const Point dip = DropPointToClientDip(pt);
    UIElement* target = FindDropTarget(dip);

    if (target && target->DropTarget()) {
        DragEventArgs args{dip, PreferredEffect(*effect), data};
        target->DropTarget()->OnDrop(args);
        *effect = static_cast<DWORD>(args.effect);
        // A drop mutates content (text inserted, files loaded), and nothing on this
        // path went through a property setter — schedule the frame explicitly.
        RequestFrame(FrameReason::Paint);
    } else {
        *effect = DROPEFFECT_NONE;
    }

    // No DragLeave arrives after a Drop; clear the gesture state here.
    dragOverElement_ = nullptr;
    input_.DragData().Reset();
    return S_OK;
}



} // namespace fluent
