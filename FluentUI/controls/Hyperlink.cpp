// Hyperlink.cpp
#include "Hyperlink.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DWriteContext.h"
#include <dwrite.h>
#include <shellapi.h>
#include <algorithm>

namespace fluent {

namespace {
const char* kTag = "Hyperlink";
// The underline sits this far below the text baseline box, and is this thick.
constexpr float kUnderlineGap = 1.0f;
constexpr float kUnderlineThickness = 1.0f;
}  // namespace

void Hyperlink::SetText(std::wstring text) {
    if (SetProperty(text_, std::move(text), DirtyFlags::Measure)) layout_.Reset();
}

void Hyperlink::OnThemeChanged() {
    // A theme change can swap the font family, so the cached layout is stale.
    layout_.Reset();
    InvalidateDirty(DirtyFlags::Measure);
}

void Hyperlink::OnAttachedToTree() {
    // DWrite arrives with the context; the layout can only be built now.
    layout_.Reset();
    InvalidateDirty(DirtyFlags::Measure);
}

void Hyperlink::EnsureLayout() {
    if (text_.empty()) {
        layout_.Reset();
        return;
    }
    if (!Dwrite() || !Dwrite()->Valid()) {
        layout_.Reset();
        return;
    }

    const uint32_t gen = Theme().generation;
    // Font size stays part of the cache key. It is no longer a member of this class,
    // but EffectiveFontSize() can still change under us two ways: the user calling
    // Control::SetFontSize, or — when they never pinned one — a theme whose bodySize
    // differs. Dropping it from the comparison would keep serving a layout built at
    // the old size.
    const float fontSize = EffectiveFontSize();
    if (layout_ && layoutText_ == text_ && layoutFontSize_ == fontSize &&
        layoutThemeGen_ == gen)
        return;  // cache hit

    layout_.Reset();
    IDWriteTextFormat* fmt =
        Dwrite()->Format(fontSize, EffectiveFontWeight(DWRITE_FONT_WEIGHT_NORMAL),
                         DWRITE_TEXT_ALIGNMENT_LEADING,
                         DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (!fmt) return;

    // A very wide box: the link is one line and sizes to its glyphs, so the box
    // must not be the constraint.
    HRESULT hr = Dwrite()->Factory()->CreateTextLayout(
        text_.c_str(), static_cast<UINT32>(text_.size()), fmt, 1e6f, 1e6f,
        layout_.GetAddressOf());
    if (FAILED(hr)) {
        Trace(kTag, "CreateTextLayout failed", hr);
        layout_.Reset();
        return;
    }
    layoutText_ = text_;
    layoutFontSize_ = fontSize;
    layoutThemeGen_ = gen;
}

void Hyperlink::Measure(float availW, float availH) {
    UNREFERENCED_PARAMETER(availW);
    UNREFERENCED_PARAMETER(availH);
    EnsureLayout();

    // Content size from the layout when there is one; zero when there is not.
    //
    // NOT FrameworkElement::Measure as the fallback: its auto behavior is "take
    // the available space", which for a link is actively wrong — an empty link,
    // or one that cannot measure because DWrite is absent, would claim the whole
    // width of its slot and push everything beside it out of the way. A control
    // that sizes to its glyphs should claim nothing when it has no glyphs.
    float textW = 0.0f, textH = 0.0f;
    if (layout_) {
        DWRITE_TEXT_METRICS m{};
        if (SUCCEEDED(layout_->GetMetrics(&m))) {
            textW = m.width;
            textH = m.height + kUnderlineGap + kUnderlineThickness;
        }
    }

    // An explicit Width/Height always wins, so a fixed-size link still reserves
    // its slot even headless.
    SizeDip result;
    result.w = IsAuto(width_) ? textW : width_;
    result.h = IsAuto(height_) ? textH : height_;
    SetDesired(result);
}

void Hyperlink::Render(const DrawingContext& dc) {
    EnsureLayout();
    if (!layout_) return;

    const ColorTokens& pal = Theme().colors;

    // Rest = accent; hover/press brighten toward accentHover/accentPressed. The
    // fade is over the hover amount, so the color eases rather than snapping.
    // User Foreground overrides the semantic accent-based color.
    D2D1_COLOR_F color = EffectiveForeground();
    if (!HasForeground()) {
        // No user override, use semantic state-based color
        const D2D1_COLOR_F base =
            (state_ == VisualState::Pressed) ? pal.accentPressed : pal.accent;
        const D2D1_COLOR_F hot = pal.accentHover;
        const float t = hoverFade_;
        color = D2D1_COLOR_F{
            base.r + (hot.r - base.r) * t,
            base.g + (hot.g - base.g) * t,
            base.b + (hot.b - base.b) * t,
            base.a,
        };
    }

    dc.DrawTextLayout(D2D1::Point2F(bounds_.x, bounds_.y), layout_.Get(), color);

    // Underline spans the measured text width, not the full bounds — otherwise a
    // stretched link underlines empty space to its right.
    DWRITE_TEXT_METRICS m{};
    const float textW =
        SUCCEEDED(layout_->GetMetrics(&m)) ? m.width : bounds_.w;
    const float textH = SUCCEEDED(layout_->GetMetrics(&m)) ? m.height : bounds_.h;
    const float uy = bounds_.y + textH + kUnderlineGap;
    dc.FillRect(D2D1::RectF(bounds_.x, uy, bounds_.x + std::min(textW, bounds_.w),
                            uy + kUnderlineThickness),
                color);

    if (IsFocused()) {
        const float pad = 2.0f;
        dc.DrawRoundedRect(
            D2D1::RoundedRect(
                D2D1::RectF(bounds_.x - pad, bounds_.y - pad,
                            bounds_.x + std::min(textW, bounds_.w) + pad,
                            uy + kUnderlineThickness + pad),
                3.0f, 3.0f),
            D2D1::ColorF(pal.focusStroke.r, pal.focusStroke.g, pal.focusStroke.b,
                         0.8f),
            1.0f);
    }
}

float Hyperlink::VisualOverflowDip() const {
    // See the header for why this exists and why it is unconditional.
    //
    // The focus ring's contribution is a constant: the pad it is built outward
    // from, plus the outward half of the centered stroke, plus a pixel for
    // antialiasing. Same shape as FocusRingPadDip, but computed from THIS
    // control's own literals rather than a FocusRingSpec — Hyperlink strokes its
    // ring by hand (a custom alpha and corner radius) instead of calling
    // DrawFocusRing, so borrowing the shared spec's numbers here would silently
    // stop matching if either side changed.
    constexpr float kRingPad = 2.0f;      // Render's `pad`
    constexpr float kRingStroke = 1.0f;   // Render's stroke width
    const float ringOverflow = kRingPad + kRingStroke * 0.5f + 1.0f;

    // The underline's contribution is NOT a constant: it is drawn below the text,
    // and whether that lands inside bounds_ depends on how bounds_.h compares to
    // the measured glyph height. With Auto height they agree exactly and this is
    // zero; with an explicit Height shorter than the glyphs it is the shortfall.
    float underlineOverflow = 0.0f;
    if (layout_) {
        DWRITE_TEXT_METRICS m{};
        if (SUCCEEDED(layout_->GetMetrics(&m))) {
            const float underlineBottom =
                m.height + kUnderlineGap + kUnderlineThickness;
            underlineOverflow = std::max(0.0f, underlineBottom - bounds_.h);
        }
    }

    // One number feeds both the dirty-rect report and Panel::Render's cull, so the
    // max of the two is what keeps "what gets cleared" and "what gets redrawn"
    // from disagreeing — the drift that project documentation calls out as a ghosting bug class.
    return std::max(ringOverflow, underlineOverflow);
}

HCURSOR Hyperlink::Cursor() const {
    static HCURSOR hand = LoadCursor(nullptr, IDC_HAND);
    return hand;
}

bool Hyperlink::WantsAnimationTick() const {
    const float target = (state_ == VisualState::Hover ||
                          state_ == VisualState::Pressed)
                             ? 1.0f
                             : 0.0f;
    return hoverFade_.Animating(target);
}

void Hyperlink::OnAnimationTick(float dtSec) {
    const float target = (state_ == VisualState::Hover ||
                          state_ == VisualState::Pressed)
                             ? 1.0f
                             : 0.0f;
    hoverFade_.Approach(target, dtSec, Theme().motion.tintTau);
    Invalidate();
}

bool Hyperlink::RequiresCtrl() const {
    switch (activation_) {
        case HyperlinkActivation::Click:     return false;
        case HyperlinkActivation::CtrlClick: return true;
        case HyperlinkActivation::Default:   break;
    }
    // Fall back to the tree-wide behavior policy, then the library default.
    const BehaviorSettings* b = Context().behavior;
    return b ? b->hyperlinkRequireCtrl : BehaviorSettings{}.hyperlinkRequireCtrl;
}

void Hyperlink::Activate(bool ctrlHeld) {
    // Open the URI when the resolved policy accepts this gesture. Plain click
    // suffices under the Click policy; Ctrl is required under CtrlClick. When
    // the gesture doesn't match the policy (or there is no URI), raise Click
    // instead — the two paths are alternatives so a navigating handler does
    // not also fire when the shell already took the activation.
    const bool openUri = !uri_.empty() && (ctrlHeld || !RequiresCtrl());
    if (openUri) {
        lastOpenedUri_ = true;
        ShellExecuteW(Context().hwnd, L"open", uri_.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return;
    }
    lastOpenedUri_ = false;
    RoutedEventArgs args;
    args.source = this;
    args.originalSource = this;
    click_.Raise(*this, args);
}

void Hyperlink::OnClickRouted(PointerEventArgs& e) {
    // Read the modifier from the EVENT, not GetKeyState: the routed args carry
    // what was held when the click happened, and it is the only form a headless
    // test can set.
    Activate(HasModifier(e.modifiers, ModifierKeys::Ctrl));
    e.handled = true;
}

void Hyperlink::OnKeyDownRouted(KeyEventArgs& e) {
    if (e.vk != VK_SPACE && e.vk != VK_RETURN) return;
    Activate(HasModifier(e.modifiers, ModifierKeys::Ctrl));
    e.handled = true;
}

}  // namespace fluent
