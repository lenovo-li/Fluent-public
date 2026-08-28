// FrameworkElement.h — the WPF-style layout-property layer (roadmap §5.3).
//
// Adds the common layout inputs (Width/Height, Margin, H/V alignment) and turns
// the abstract Measure/Arrange from UIElement into concrete default leaf behavior
// driven by those properties. Controls that size to content (TextBlock, Button…)
// override Measure; panels override both.
//
// Layer boundary: FrameworkElement owns "how big / where in my slot", not
// "what's inside me" (that's Control's Padding/Background) and not children
// (that's Panel). Every property setter routes through SetProperty so a change
// dirties exactly Measure (see roadmap §7.2).
#pragma once

#include "UIElement.h"
#include <algorithm>  // std::clamp — Min/Max size clamping in Measure
#include <cmath>      // std::isfinite — the default Measure rejects unbounded offers
#include <limits>     // std::numeric_limits<float>::infinity — MaxWidth/MaxHeight defaults
#include <optional>   // MinWidth/MaxWidth etc. — nullopt means "no constraint"

namespace fluent {

class FrameworkElement : public UIElement {
public:
    // Layout inputs (set by the app; read by panels during Measure/Arrange).
    // Each affects the element's (or its parent's) desired size, so a change
    // dirties Measure (which drops the measure cache via OnMeasureInvalidated).
    void SetMargin(const Thickness& m) { SetProperty(margin_, m, DirtyFlags::Measure); }
    const Thickness& Margin() const { return margin_; }
    void SetHAlign(HAlign a) { SetProperty(hAlign_, a, DirtyFlags::Measure); }
    void SetVAlign(VAlign a) { SetProperty(vAlign_, a, DirtyFlags::Measure); }
    HAlign GetHAlign() const { return hAlign_; }
    VAlign GetVAlign() const { return vAlign_; }
    // Explicit size in DIPs, or kAuto for "size to content / available space".
    void SetWidth(float w) { SetProperty(width_, w, DirtyFlags::Measure); }
    void SetHeight(float h) { SetProperty(height_, h, DirtyFlags::Measure); }
    float Width() const { return width_; }
    float Height() const { return height_; }

    // Min/Max size constraints (roadmap §4 gap A). std::nullopt = "no constraint"
    // (default: min = 0, max = +infinity). A change dirties Measure so the element
    // and its parent re-measure. The clamp runs at the end of Measure (see
    // ClampDesiredSize), so it applies to the base leaf layout; controls that
    // override Measure should call ClampDesiredSize() at the end of their override.
    // Setters no-op when unchanged (matches SetProperty contract) so a
    // redundant SetMinWidth(200) never schedules a frame.
    void SetMinWidth(float v) {
        if (minWidth_ == v) return;
        minWidth_ = v;
        InvalidateDirty(DirtyFlags::Measure);
    }
    void ClearMinWidth() {
        if (!minWidth_.has_value()) return;
        minWidth_ = std::nullopt;
        InvalidateDirty(DirtyFlags::Measure);
    }
    float MinWidth() const       { return minWidth_.value_or(0.0f); }
    const std::optional<float>& GetMinWidth() const { return minWidth_; }
    void SetMaxWidth(float v) {
        if (maxWidth_ == v) return;
        maxWidth_ = v;
        InvalidateDirty(DirtyFlags::Measure);
    }
    void ClearMaxWidth() {
        if (!maxWidth_.has_value()) return;
        maxWidth_ = std::nullopt;
        InvalidateDirty(DirtyFlags::Measure);
    }
    float MaxWidth() const       { return maxWidth_.value_or(std::numeric_limits<float>::infinity()); }
    const std::optional<float>& GetMaxWidth() const { return maxWidth_; }
    void SetMinHeight(float v) {
        if (minHeight_ == v) return;
        minHeight_ = v;
        InvalidateDirty(DirtyFlags::Measure);
    }
    void ClearMinHeight() {
        if (!minHeight_.has_value()) return;
        minHeight_ = std::nullopt;
        InvalidateDirty(DirtyFlags::Measure);
    }
    float MinHeight() const      { return minHeight_.value_or(0.0f); }
    const std::optional<float>& GetMinHeight() const { return minHeight_; }
    void SetMaxHeight(float v) {
        if (maxHeight_ == v) return;
        maxHeight_ = v;
        InvalidateDirty(DirtyFlags::Measure);
    }
    void ClearMaxHeight() {
        if (!maxHeight_.has_value()) return;
        maxHeight_ = std::nullopt;
        InvalidateDirty(DirtyFlags::Measure);
    }
    float MaxHeight() const      { return maxHeight_.value_or(std::numeric_limits<float>::infinity()); }
    const std::optional<float>& GetMaxHeight() const { return maxHeight_; }

    // Default leaf layout: an explicit size wins; otherwise the element fills the
    // space it is offered.
    //
    // AN INFINITE CONSTRAINT MUST NOT BECOME AN INFINITE DESIRED SIZE. "Fill what
    // I'm offered" is meaningless when the offer is unbounded, and an infinite
    // desired size is not merely too big — it poisons every arithmetic consumer
    // upstream. A StackPanel sums it (inf), a Grid's Auto track takes it as content
    // size, and `leftover = avail - used` becomes inf - inf = NaN, after which the
    // resolved track sizes are garbage.
    //
    // Zero is the correct fallback: an element with no explicit size and no content
    // logic of its own has no natural extent to report. Anything that does have a
    // natural size (TextBlock, CheckBox, Slider…) overrides Measure and never
    // reaches this line.
    void Measure(float availW, float availH) override {
        const bool finiteW = availW > 0.0f && std::isfinite(availW);
        const bool finiteH = availH > 0.0f && std::isfinite(availH);
        SizeDip sz;
        sz.w = IsAuto(width_) ? (finiteW ? availW : 0.0f) : width_;
        sz.h = IsAuto(height_) ? (finiteH ? availH : 0.0f) : height_;
        SetDesired(sz);
        // Clamp here as well as in MeasureCached's ApplySizeConstraints hook.
        // Not redundant: this base implementation is also reached by direct
        // Measure() calls that skip the cache — Image.cpp explicitly delegates
        // to FrameworkElement::Measure, and a leaf used as a window root is
        // measured directly by Window::Layout. Clamping is idempotent, so the
        // double application on the normal panel-child path is harmless.
        ClampDesiredSize();
    }
    // Arrange takes the rect as given. Min/Max are NOT applied here on purpose:
    // the caller has already resolved them. For a panel child that caller is
    // Panel::ArrangeChild, which needs Min/Max *before* it can compute the
    // alignment offset (a Center-aligned child clamped to a narrower width has
    // to be re-centred), so the clamp cannot be deferred to this method — doing
    // it in both places would mean the offset was computed from the unclamped
    // width. For a window root the rect is the client area, which no Min/Max is
    // allowed to override.
    void Arrange(const RectDip& finalRect) override {
        SetBounds(finalRect);
        // First layout is where a control's eased scalars adopt their initial state
        // outright instead of animating into it; see AnimationPrimed() below.
        if (!animPrimed_) {
            animPrimed_ = true;
            SnapAnimationsToSettledState();
        }
    }

    // --- First-paint animation suppression -------------------------------------
    //
    // A control must not animate INTO its initial state. Building a CheckBox already
    // checked, or a Slider already at 50, then easing from 0 shows the user a state the
    // application never asked for: the slider sweeps up from empty on the first frame it
    // is visible.
    //
    // The eased scalars all start at 0 while the state setters only move the TARGET, so
    // every animated control had this bug. The reason it only showed up "the first time a
    // page is opened" is that a hidden element is neither rendered nor ticked, so the
    // stray ease was spent whenever the page first became visible, and never again.
    //
    // The rule: a state change is only ANIMATED once the control has actually been
    // painted at least once. Before that there is no previous frame on screen, so there is
    // nothing to animate from and the value is snapped. Controls call
    // MarkAnimationPrimed() from Render() and check AnimationPrimed() in the setter that
    // moves an AnimatedValue.
    //
    // Keyed on FIRST ARRANGE, and deliberately not on first Render. An invisible child is
    // skipped by both Measure and Arrange (Panel.cpp checks IsVisible() in each), so a
    // pre-built hidden page does not prime, which is exactly the case that has to work.
    // Render is the wrong hook for two reasons: the host queries WantsAnimationTick()
    // BEFORE it renders, so frame one would still tick, and a headless test that never
    // paints would leave every control permanently unprimed and silently unable to animate.
    bool AnimationPrimed() const { return animPrimed_; }

    // Override to snap eased scalars to the values the control's current state implies.
    // Called exactly once, on first Arrange. Default: nothing to snap.
    virtual void SnapAnimationsToSettledState() {}

    // UIElement::ApplySizeConstraints override — enforces Min/Max on desired_.
    // Delegates to ClampDesiredSize so both the hook path (MeasureCached) and
    // the direct-call path (Window::Layout, Image::Measure delegation) share
    // the same logic.
    void ApplySizeConstraints() override { ClampDesiredSize(); }

protected:
    // Enforces MinWidth/MaxWidth/MinHeight/MaxHeight on desired_. Called by
    // both the base Measure() and the ApplySizeConstraints hook, so it covers
    // direct Measure() calls (root, Image) and the normal MeasureCached path.
    // Phase 3b: clamps whichever field the current thread owns. Reading the
    // pre-clamp value from that same field is what makes this idempotent, so the
    // double application on the normal panel-child path is harmless.
    void ClampDesiredSize() {
        const float minW = minWidth_.value_or(0.0f);
        const float maxW = maxWidth_.value_or(std::numeric_limits<float>::infinity());
        const float minH = minHeight_.value_or(0.0f);
        const float maxH = maxHeight_.value_or(std::numeric_limits<float>::infinity());
        const SizeDip& in = Desired();
        SetDesired({std::clamp(in.w, minW, std::max(minW, maxW)),
                    std::clamp(in.h, minH, std::max(minH, maxH))});
    }

    // Shared arrange logic: given a slot and a child element, compute the final
    // rect after applying margin, Min/Max constraints, and alignment. This is
    // the core of Panel::ArrangeChild, extracted so Border can use it too.
    //
    // Returns the absolute rect to pass to child->Arrange(). The caller is
    // responsible for calling Arrange(); this only computes the rect.
    static RectDip ComputeArrangeRect(const FrameworkElement* child,
                                       const RectDip& slot) {
        if (!child) return slot;
        const Thickness& m = child->Margin();
        // Slot minus margin is the space available to the child.
        RectDip avail = {slot.x + m.left, slot.y + m.top,
                         std::max(0.0f, slot.w - m.horizontal()),
                         std::max(0.0f, slot.h - m.vertical())};

        const SizeDip& d = child->Desired();
        RectDip out = avail;

        // Min/Max clamping helper: Min wins over Max on conflict.
        const auto clampAxis = [](float v, const std::optional<float>& lo,
                                  const std::optional<float>& hi) {
            const float minV = lo.value_or(0.0f);
            const float maxV = hi.value_or(std::numeric_limits<float>::infinity());
            return std::clamp(v, minV, std::max(minV, maxV));
        };

        // Horizontal: Stretch fills the slot; otherwise use desired width.
        // Clamp the width, then compute alignment offset from the CLAMPED width.
        {
            const bool stretch = child->GetHAlign() == HAlign::Stretch;
            float w = clampAxis(stretch ? avail.w : std::min(d.w, avail.w),
                                child->GetMinWidth(), child->GetMaxWidth());
            out.w = w;
            switch (child->GetHAlign()) {
                case HAlign::Left:   out.x = avail.x; break;
                case HAlign::Center: out.x = avail.x + (avail.w - w) * 0.5f; break;
                case HAlign::Right:  out.x = avail.x + (avail.w - w); break;
                default: break;  // Stretch keeps avail.x
            }
        }
        // Vertical: same logic.
        {
            const bool stretch = child->GetVAlign() == VAlign::Stretch;
            float h = clampAxis(stretch ? avail.h : std::min(d.h, avail.h),
                                child->GetMinHeight(), child->GetMaxHeight());
            out.h = h;
            switch (child->GetVAlign()) {
                case VAlign::Top:    out.y = avail.y; break;
                case VAlign::Center: out.y = avail.y + (avail.h - h) * 0.5f; break;
                case VAlign::Bottom: out.y = avail.y + (avail.h - h); break;
                default: break;  // Stretch keeps avail.y
            }
        }
        return out;
    }

    Thickness margin_;
    HAlign hAlign_ = HAlign::Stretch;
    VAlign vAlign_ = VAlign::Stretch;
    float width_ = kAuto;
    float height_ = kAuto;
    std::optional<float> minWidth_;
    std::optional<float> maxWidth_;
    std::optional<float> minHeight_;
    std::optional<float> maxHeight_;

    // False until the control has painted once; see AnimationPrimed() above.
    bool animPrimed_ = false;
};

} // namespace fluent
