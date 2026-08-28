// PopupHost.cpp

#include "PopupHost.h"
#include "../styling/ThemeManager.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace fluent {

namespace {
const char* kTag = "PopupHost";
const wchar_t* kClassName = L"FluentUI.PopupHost";

constexpr float kPadding = 8.0f;       // DIP, content inset from card edge

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
} // namespace

PopupHost::~PopupHost() {
    // Do NOT touch content_ here: it is owned by the caller, and a common
    // ownership pattern (e.g. MenuFlyout's per-level struct) holds the content
    // and the PopupHost side by side, so the content may already be destroyed by
    // the time this runs (its member is declared first). Dereferencing it then is
    // a use-after-free. Detach is unnecessary anyway: the content's own destructor
    // self-heals (drops from the animation set, releases subscriptions), and if it
    // is reused elsewhere AttachToContext cleanly detaches the stale context first.
    if (hwnd_) DestroyWindow(hwnd_);
}

HRESULT PopupHost::Create(HINSTANCE hInstParent, HWND hwndOwner,
                          D2DContext* d2d, DWriteContext* dwrite) {
    hInst_ = hInstParent;
    hwndOwner_ = hwndOwner;
    d2d_ = d2d;
    dwrite_ = dwrite;

    if (!d2d || !dwrite || !d2d->Valid() || !dwrite->Valid()) {
        TraceMsg(kTag, "Create: invalid D2D/DWrite context");
        return E_INVALIDARG;
    }

    // Register the window class (ignore "already registered" error).
    // NOTE: no CS_DROPSHADOW here. It was present historically, but DWM does
    // not honor it for layered/redirected content and the window also carries
    // WS_EX_NOREDIRECTIONBITMAP (DComp owns the pixels), so the style was a
    // no-op. Shadows, if wanted, must be drawn into the DComp surface itself.
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = 0;  // no CS_HREDRAW/VREDRAW (DComp pitfall #5)
    wc.lpfnWndProc = &PopupHost::WndProc;
    wc.hInstance = hInst_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Create a hidden popup window. We'll position and show it in Open().
    // WS_POPUP: no parent, borderless. WS_EX_NOACTIVATE: don't steal focus.
    // WS_EX_TOOLWINDOW: keep it out of the taskbar and alt-tab.
    // WS_EX_NOREDIRECTIONBITMAP: DComp owns content (DComp pitfall #4).
    DWORD style = WS_POPUP;
    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    hwnd_ = CreateWindowExW(exStyle, kClassName, L"", style,
                            0, 0, 1, 1,  // dummy size, repositioned in Open
                            hwndOwner_, nullptr, hInst_, this);
    if (!hwnd_) {
        Trace(kTag, "CreateWindowExW FAILED", HRESULT_FROM_WIN32(GetLastError()));
        return E_FAIL;
    }

    // Request rounded corners (Win11+).
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    TraceMsg(kTag, "Create: window created (hidden)");
    return S_OK;
}

HRESULT PopupHost::InitGraphics(UINT pixelW, UINT pixelH) {
    FL_RETURN_IF_FAILED(kTag, comp_.Initialize(d2d_, hwnd_, pixelW, pixelH));
    graphicsReady_ = true;
    TraceMsg(kTag, "InitGraphics: DCompHost initialized");
    return S_OK;
}

HRESULT PopupHost::Open(const RECT& anchorScreenRect, float wDip, float hDip) {
    if (!hwnd_) return E_FAIL;

    // Only a Closed popup may open. Reject re-open while Opening/Open/Closing so
    // a stray Open() during the close side effects can't resurrect the window.
    if (!CanOpen(state_)) {
        TraceMsg(kTag, "Open: ignored (not in Closed state)");
        return S_FALSE;
    }
    state_ = PopupState::Opening;

    // Determine the target monitor from the anchor rect, and its DPI.
    HMONITOR mon = MonitorFromRect(&anchorScreenRect, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    dpi_ = dx ? dx : 96;

    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(mon, &mi);

    // Convert DIP size to physical pixels.
    int w = static_cast<int>(wDip * dpi_ / 96.0f + 0.5f);
    int h = static_cast<int>(hDip * dpi_ / 96.0f + 0.5f);

    // Default position: below the anchor, left-aligned.
    int x = anchorScreenRect.left;
    int y = anchorScreenRect.bottom;

    // Flip above the anchor if we'd overflow the bottom of the work area.
    if (y + h > mi.rcWork.bottom && anchorScreenRect.top - h >= mi.rcWork.top)
        y = anchorScreenRect.top - h;

    // Clamp horizontally into the work area.
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (w > mi.rcWork.right - mi.rcWork.left) w = mi.rcWork.right - mi.rcWork.left;

    FL_TRACEF(kTag, "Open: anchor=(%d,%d,%dx%d) dpi=%u pos=(%d,%d) size=(%dx%d)",
              anchorScreenRect.left, anchorScreenRect.top,
              anchorScreenRect.right - anchorScreenRect.left,
              anchorScreenRect.bottom - anchorScreenRect.top,
              dpi_, x, y, w, h);

    // Resize and reposition the window (but don't show yet).
    SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_NOREDRAW);

    // Client area size in physical pixels.
    RECT rc;
    GetClientRect(hwnd_, &rc);
    pixelW_ = std::max<UINT>(1, rc.right - rc.left);
    pixelH_ = std::max<UINT>(1, rc.bottom - rc.top);

    // Initialize or resize the DComp composition target.
    if (!graphicsReady_) {
        FL_RETURN_IF_FAILED(kTag, InitGraphics(pixelW_, pixelH_));
    } else {
        FL_RETURN_IF_FAILED(kTag, comp_.Resize(pixelW_, pixelH_));
    }

    // Layout content to the card's interior (inset by padding).
    if (content_) {
        float s = DpiScale();
        float contentW = std::max(0.0f, pixelW_ / s - kPadding * 2.0f);
        float contentH = std::max(0.0f, pixelH_ / s - kPadding * 2.0f);
        RectDip area = {kPadding, kPadding, contentW, contentH};
        content_->SetInvalidateCallback(&PopupHost::InvalidateThunk, this);
        content_->Measure(contentW, contentH);
        content_->Arrange(area);
    }

    // Mark open before Render(): the first frame is drawn while the HWND is still
    // hidden, then DwmFlush waits for DWM before ShowWindow (DComp pitfall #6).
    // Opening -> Open: Render() checks IsOpen(), so settle the state first.
    state_ = PopupState::Open;

    // DComp pitfall #6: draw the first frame, DwmFlush, then show.
    Render();
    DwmFlush();

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    // DO NOT SetCapture on a WS_EX_NOACTIVATE window — the system immediately
    // revokes it (WM_CAPTURECHANGED), causing the popup to close instantly.
    // Instead, the parent window implements light-dismiss by checking clicks
    // outside the popup via ContainsScreenPoint().
    TraceMsg(kTag, "Open: popup shown (no capture, parent handles light-dismiss)");
    return S_OK;
}

void PopupHost::Close() {
    // Idempotent: only a fully-Open popup begins closing (Closed/Closing/Opening
    // are no-ops). See CanClose() in PopupState.h.
    if (!CanClose(state_)) return;

    // Enter Closing for the duration of the side effects: a reentrant Close() is
    // a no-op (state != Open) and a reentrant Open() is rejected (CanOpen is
    // false in Closing). IsOpen() also reads false throughout onClose_.
    state_ = PopupState::Closing;
    ShowWindow(hwnd_, SW_HIDE);
    if (onClose_) onClose_();
    state_ = PopupState::Closed;

    TraceMsg(kTag, "Close: popup hidden");
}

bool PopupHost::ContainsScreenPoint(int screenX, int screenY) const {
    if (!IsOpen() || !hwnd_) return false;
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    POINT pt = {screenX, screenY};
    return PtInRect(&rc, pt) != FALSE;
}

void PopupHost::SetContent(UIElement* content) {
    if (content_ == content) return;
    // Detach the previous content from this popup's context (releases its
    // subscriptions / drops animations) before swapping in the new one.
    if (content_ && content_->IsAttached()) content_->DetachFromContext();
    content_ = content;
    // Inject the popup's service context so tree-attached content (e.g. a tooltip
    // TextBlock) reads DWrite through Context() instead of a manual SetDWrite
    // (roadmap §6.2). The popup shares the parent's device-level DWrite; it has no
    // animation registry of its own (popup content is redrawn on invalidate), so
    // that stays null. window is null too — a popup is not a WindowServices host.
    roots_.clear();
    if (content_) {
        UIContext ctx;
        ctx.dwrite = dwrite_;
        ctx.hwnd = hwnd_;
        ctx.dpiScale = DpiScale();
        ctx.input = &input_;  // content grabs capture through the popup's router
        ctx.resourceCache = resourceCache_;  // share the parent's caches (§13.3)
        ctx.theme = theme_;                  // share the parent's stable snapshot (§11)
        content_->AttachToContext(ctx);
        roots_.push_back(content_);
    }
    // Point the router at the (single) content root and repaint on any input.
    input_.SetRoots(&roots_);
    input_.SetHwnd(hwnd_);
    input_.SetInvalidate(&PopupHost::InvalidateThunk, this);
}

bool PopupHost::ForwardKey(UINT vk) {
    if (!content_ || !IsOpen()) return false;
    // The popup content is a single element (list view); route the key straight
    // to it. Preview tunnels then the main bubble both hit just this element.
    KeyEventArgs args;
    args.vk = vk;
    args.source = content_;
    args.originalSource = content_;
    content_->OnPreviewKeyDown(args);
    if (!args.handled) content_->OnKeyDownRouted(args);
    return args.handled;
}

void PopupHost::Render() {
    if (!graphicsReady_ || !IsOpen()) return;

    ID2D1DeviceContext* dc = nullptr;
    POINT netOffset = {};
    if (FAILED(comp_.BeginContentFrame(&dc, &netOffset)) || !dc) return;

    float s = DpiScale();
    // Popups always full-redraw; fold the surface's net atlas translation into the
    // DPI transform so DIP (0,0) maps to the surface tile origin.
    dc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) *
                     D2D1::Matrix3x2F::Translation(static_cast<float>(netOffset.x),
                                                   static_cast<float>(netOffset.y)));

    // Resolve the theme snapshot: the parent's stable one, or a default light
    // snapshot if none was set (a popup shown before SetTheme).
    static const ThemeSnapshot kDefault = BuildSnapshot(ThemeInputs{}, 0);
    const ThemeSnapshot& th = theme_ ? *theme_ : kDefault;
    const ColorTokens& pal = th.colors;
    float w = pixelW_ / s;
    float h = pixelH_ / s;

    // Draw a rounded card: card fill + subtle border. The card is the full client
    // area; corners outside the rounded rect stay transparent (DComp
    // ALPHA_MODE_PREMULTIPLIED) so the rounding reads cleanly against whatever is
    // behind the window. One brush is created per frame and shared with the
    // content element (roadmap §5.3.1).
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(pal.cardFill, brush.GetAddressOf()))) {
        comp_.EndContentFrame();
        comp_.Commit();
        return;
    }
    // A popup is its own HWND with WS_EX_NOREDIRECTIONBITMAP and NO system
    // backdrop — there is no Mica behind it, on ANY Windows version. cardFill is
    // authored translucent (0.70 light / 0.65 dark) FOR a Mica base, so painting
    // it raw made the menu see-through onto the parent window. Flatten it over
    // windowBackground first: that yields the solid color the translucency was
    // designed to look like, and only then does cardOpacity_ (1.0 by default)
    // scale it — so a menu is opaque unless the app explicitly asks otherwise.
    const D2D1_COLOR_F bg =
        WithAlphaScale(FlattenOver(pal.cardFill, pal.windowBackground), cardOpacity_);
    brush->SetColor(bg);
    const float corner = th.spacing.cornerRadiusNormal;
    D2D1_ROUNDED_RECT card = D2D1::RoundedRect(
        D2D1::RectF(0, 0, w, h), corner, corner);
    dc->FillRoundedRectangle(card, brush.Get());

    // The border follows the card's opacity: a fading card with a solid outline
    // would read as a floating frame.
    brush->SetColor(WithAlphaScale(pal.controlStrokeDefault, cardOpacity_));
    dc->DrawRoundedRectangle(card, brush.Get(), 1.0f);

    // Render the content element (already arranged in Open), sharing the brush.
    // The popup's content opacity multiplies with the element's own Opacity()
    // (RenderWithOpacity), matching how the main window composes them.
    if (content_) {
        DrawingContext rc{dc, brush.Get(), s, nullptr, nullptr, contentOpacity_};
        content_->RenderWithOpacity(rc);
    }

    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    comp_.EndContentFrame();
    comp_.Commit();
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK PopupHost::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PopupHost* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<PopupHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<PopupHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PopupHost::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED)
                OnSize(LOWORD(lp), HIWORD(lp));
            return 0;

        case WM_MOUSEMOVE:
            RouteMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_LBUTTONDOWN:
            RouteMouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_LBUTTONUP:
            RouteMouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            RouteMouseWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // DComp owns the content

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_DESTROY:
            state_ = PopupState::Closed;
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void PopupHost::OnSize(UINT pixelW, UINT pixelH) {
    if (!graphicsReady_) return;
    pixelW_ = std::max<UINT>(1, pixelW);
    pixelH_ = std::max<UINT>(1, pixelH);
    comp_.Resize(pixelW_, pixelH_);
    Render();
}

// ---------------------------------------------------------------------------
// Mouse routing (physical pixels -> DIPs, then route to content)
// ---------------------------------------------------------------------------

void PopupHost::RouteMouseMove(int px, int py) {
    float s = 96.0f / dpi_;
    input_.PointerMoved(Point{px * s, py * s}, ModifierKeys::None);
}

void PopupHost::RouteMouseDown(int px, int py) {
    float s = 96.0f / dpi_;
    float dipX = px * s, dipY = py * s;

    // Light-dismiss: click outside the client rect (or on non-interactive content)
    // closes. When lightDismiss_ is off (a menu whose whole chain is managed by an
    // external session), never self-close — the owner decides when to close.
    RECT rc;
    GetClientRect(hwnd_, &rc);
    POINT pt = {px, py};
    if (!PtInRect(&rc, pt)) {
        if (lightDismiss_) {
            TraceMsg(kTag, "RouteMouseDown: outside client rect, closing");
            Close();
        }
        return;
    }
    if (!input_.HitTest(Point{dipX, dipY})) {
        if (lightDismiss_) Close();
        return;
    }
    input_.PointerPressed(Point{dipX, dipY}, PointerButton::Left, ModifierKeys::None);
}

void PopupHost::RouteMouseUp(int px, int py) {
    float s = 96.0f / dpi_;
    input_.PointerReleased(Point{px * s, py * s}, PointerButton::Left, ModifierKeys::None);
}

void PopupHost::RouteMouseWheel(int px, int py, int delta) {
    float s = 96.0f / dpi_;
    input_.PointerWheel(Point{px * s, py * s}, delta, ModifierKeys::None);
}

} // namespace fluent
