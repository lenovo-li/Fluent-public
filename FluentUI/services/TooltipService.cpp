// TooltipService.cpp

#include "TooltipService.h"
#include "../graphics/D2DContext.h"
#include "../graphics/DWriteContext.h"
#include <algorithm>

namespace fluent {

namespace {
const char* kTag = "TooltipService";

constexpr float kFontSize = 12.0f;   // DIP
constexpr float kMaxWidth = 320.0f;  // DIP, wrap long tooltips
constexpr float kCursorGapDip = 20.0f;  // vertical gap below the cursor
} // namespace

HRESULT TooltipService::Create(HINSTANCE hInst, HWND hwndOwner, D2DContext* d2d,
                               DWriteContext* dwrite, const ThemeSnapshot* theme) {
    dwrite_ = dwrite;

    popup_ = std::make_unique<PopupHost>();
    HRESULT hr = popup_->Create(hInst, hwndOwner, d2d, dwrite);
    if (FAILED(hr)) {
        Trace(kTag, "Create: PopupHost::Create failed", hr);
        popup_.reset();
        return hr;
    }
    // A tooltip never takes input and is dismissed explicitly by the window.
    popup_->SetLightDismiss(false);
    // Theme the card + injected content with the parent's stable snapshot (§11).
    // Set before SetContent so it flows into the content's UIContext.
    popup_->SetTheme(theme);

    // DWrite is injected by PopupHost::SetContent via the tree UIContext now
    // (roadmap §6.2), so the TextBlock no longer needs a manual SetDWrite.
    content_ = std::make_unique<TextBlock>();
    content_->SetFontSize(kFontSize);
    content_->SetWrap(true);
    popup_->SetContent(content_.get());

    TraceMsg(kTag, "Create: tooltip popup ready");
    return S_OK;
}

void TooltipService::Show(const std::wstring& text, int cursorScreenX,
                          int cursorScreenY) {
    if (!popup_ || !content_ || text.empty()) return;

    content_->SetText(text);

    // Measure the text at the max width to get the natural card size. The popup
    // adds its own interior padding (kPadding) around the content, so we ask for
    // the measured size and PopupHost insets it.
    content_->Measure(kMaxWidth, 100000.0f);
    const SizeDip& d = content_->Desired();

    // PopupHost draws an 8dip padding inset around the content on all sides.
    // Add 2 DIP horizontal margin to avoid pixel-rounding truncation of the last
    // character (observed: long tooltips lose final word without this margin).
    constexpr float kPopupPad = 8.0f;
    float wDip = d.w + kPopupPad * 2.0f + 2.0f;
    float hDip = d.h + kPopupPad * 2.0f;

    // Anchor a zero-width rect just below the cursor so PopupHost places the card
    // beneath it (flipping above / clamping horizontally as needed). The gap is
    // in DIPs at the primary DPI; good enough since the cursor sets the monitor.
    UINT dpi = GetDpiForWindow(popup_->Hwnd());
    if (dpi == 0) dpi = 96;
    int gapPx = static_cast<int>(kCursorGapDip * dpi / 96.0f + 0.5f);
    RECT anchor;
    anchor.left = cursorScreenX;
    anchor.top = cursorScreenY;
    anchor.right = cursorScreenX;
    anchor.bottom = cursorScreenY + gapPx;

    if (FAILED(popup_->Open(anchor, wDip, hDip)))
        TraceMsg(kTag, "Show: PopupHost::Open failed");
}

void TooltipService::Hide() {
    if (popup_ && popup_->IsOpen()) popup_->Close();
}

} // namespace fluent
