// Expander.cpp

#include "Expander.h"
#include "../styling/ThemeTokens.h"
#include "../styling/FocusVisual.h"
#include "../graphics/DrawingContext.h"
#include "../graphics/DWriteContext.h"
#include <algorithm>
#include <cmath>

namespace fluent {

namespace {
constexpr float kHeaderPadV = 12.0f;  // vertical padding above/below header text (DIP)
constexpr float kHeaderPadH = 12.0f;  // left padding before chevron (DIP)
constexpr float kChevronSize = 12.0f; // chevron glyph size (DIP)
constexpr float kChevronTextGap = 8.0f; // gap between chevron and header text (DIP)
constexpr float kRevealTau = 0.06f;   // reveal animation time constant (s)
constexpr float kRotationDeg = 90.0f; // chevron rotates 90° when expanding

// Focus ring spec for the header row. Same pattern as ComboBox: inset 2, stroke 1.5,
// radius inherited from the theme.
FocusRingSpec HeaderFocusRing(float cornerRadius) {
    FocusRingSpec spec;
    spec.cornerRadius = cornerRadius;
    return spec;
}
} // namespace

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------

void Expander::SetExpanded(bool exp, Transition transition) {
    if (expanded_ == exp) return;
    expanded_ = exp;
    SyncContentAttachment();

    if (transition == Transition::Instant) {
        // Snap: reveal lands on its target here, so WantsAnimationTick() is false on
        // the next line and the frame loop never ticks this element. This is the
        // behaviour the control shipped with and remains the default.
        reveal_.SetImmediate(expanded_ ? 1.0f : 0.0f);
    }
    // Transition::Animate deliberately does NOT touch reveal_. Leaving it where it is
    // makes WantsAnimationTick() true (the value differs from the target), the host
    // collects this element, and OnAnimationTick eases it over kRevealTau. That is the
    // same shape ToggleSwitch uses for its knob: no SetImmediate anywhere, the gap
    // between value and target IS the animation request.

    // Measure, not Render: the reveal height feeds this element's desired size, so the
    // parent has to re-run layout — on this frame for Instant, and on every frame of
    // the ease for Animate (OnAnimationTick invalidates again each tick).
    InvalidateDirty(DirtyFlags::Measure);
}

void Expander::SyncContentAttachment() {
    // Attach the content iff expanded AND this Expander is itself attached.
    if (!content_) return;
    bool shouldAttach = expanded_ && IsAttached();
    bool isAttached = content_->IsAttached();
    if (shouldAttach && !isAttached) {
        content_->AttachToContext(Context());
        static_cast<Visual*>(content_.get())->OnAncestorVisibilityChanged();
    } else if (!shouldAttach && isAttached) {
        // Detached immediately on collapse, including mid-animation, and that is
        // deliberate rather than an oversight in the animated path.
        //
        // A collapsing Expander with Transition::Animate therefore wipes an EMPTY
        // area: Render's content branch requires content_->IsAttached(), so the
        // shrinking band is blank instead of showing the content sliding away. The
        // alternative — keeping the subtree attached until reveal_ reaches 0 — would
        // mean a collapsed-looking Expander still has live children being measured,
        // hit-tested and ticked for the duration, and "collapsed means detached" is
        // load-bearing here (ExpanderTests pins it, and it is what stops every
        // previously-opened section staying live). Correctness of the tree wins over
        // fidelity of a 60 ms wipe.
        content_->DetachFromContext();
    }
}

void Expander::ToggleFromUser() {
    // The gesture's transition is a property of the control (SetUserToggleTransition),
    // not of this call: a click cannot pass an argument.
    SetExpanded(!expanded_, userToggle_);
    expandedChanged_.Raise(*this, expanded_);
}

bool Expander::IsContentAttached() const {
    return content_ && content_->IsAttached();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

float Expander::HeaderHeight() const {
    const ThemeSnapshot& th = Theme();
    return th.typography.bodySize + kHeaderPadV * 2.0f;
}

float Expander::ContentDesiredHeight() const {
    if (!content_) return 0.0f;
    return content_->Desired().h;
}

RectDip Expander::HeaderRect() const {
    return RectDip{bounds_.x, bounds_.y, bounds_.w, HeaderHeight()};
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void Expander::Measure(float availW, float availH) {
    const float hdrH = HeaderHeight();
    float desiredW = availW;
    float desiredH = hdrH;

    // Measure content only while expanding/expanded. A fully collapsed Expander
    // skips the measure so the content's Measure doesn't run and its desired_
    // stays stale. That is deliberate: the content is detached, so a measure would
    // see no context and produce a fallback size rather than the real one.
    //
    // The test is `expanded_ || reveal_ > 0` rather than reveal alone, and both halves
    // are needed:
    //   * `expanded_` covers the first frame of an ANIMATED expand, where reveal_ is
    //     still exactly 0.0 (Transition::Animate does not seed it). Gating on reveal
    //     alone measured nothing on that frame, so desiredH stayed at the header
    //     height and the ease had no content height to grow into.
    //   * `reveal_ > 0` covers an animated COLLAPSE, where expanded_ is already false
    //     but the band is still shrinking and its height still depends on the
    //     content's desired size.
    // Both cases require content_->IsAttached() to be meaningful; a collapse detaches
    // immediately (see SyncContentAttachment), so this measures a detached child
    // during the collapse ease and gets its last known desired size — which is what
    // the shrinking band should be measured against.
    if (content_ && (expanded_ || reveal_ > 0.001f)) {
        float contentAvailH = std::max(0.0f, availH - hdrH);
        content_->Measure(availW, contentAvailH);
        const SizeDip& childSize = content_->Desired();
        desiredW = std::max(desiredW, childSize.w);
        // During the ease, the content is measured but only partially visible: its
        // contribution to desired height is reveal * contentDesiredH. At reveal=1
        // that's the full content height; at reveal=0 it contributes nothing.
        desiredH += childSize.h * static_cast<float>(reveal_);
    }

    SetDesired({desiredW, desiredH});
}

void Expander::Arrange(const RectDip& finalRect) {
    bounds_ = finalRect;
    // Same condition as Measure, for the same reason: on the first frame of an
    // animated expand reveal_ is still 0.0, and skipping Arrange there would leave the
    // content with stale (or zero) bounds while Render is already drawing it inside
    // the reveal clip. Kept textually parallel to the Measure test so the two cannot
    // drift apart.
    if (!content_ || !(expanded_ || reveal_ > 0.001f)) return;

    const float hdrH = HeaderHeight();
    RectDip contentRect{
        finalRect.x, finalRect.y + hdrH,
        finalRect.w, std::max(0.0f, finalRect.h - hdrH)
    };
    content_->Arrange(contentRect);
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void Expander::Render(const DrawingContext& dc) {
    if (!Dwrite()) return;
    const ThemeSnapshot& th = Theme();
    const ColorTokens& pal = th.colors;

    const RectDip hdrRect = HeaderRect();
    const float chevX = bounds_.x + kHeaderPadH;
    const float chevY = hdrRect.y + hdrRect.h * 0.5f;
    const float textX = chevX + kChevronSize + kChevronTextGap;

    // --- Header background (hover state) ---
    if (headerHovered_) {
        dc.FillRoundedRect(
            D2D1::RoundedRect(
                D2D1::RectF(hdrRect.x, hdrRect.y, hdrRect.right(), hdrRect.bottom()),
                th.spacing.cornerRadiusSmall, th.spacing.cornerRadiusSmall),
            pal.controlFillHover);
    }

    // --- Chevron (rotates from 0° collapsed to 90° expanded) ---
    const float angle = static_cast<float>(reveal_) * kRotationDeg;
    const float rad = angle * 3.14159265f / 180.0f;
    const float halfSize = kChevronSize * 0.5f;
    // Chevron is a ">" shape: two line segments forming a right-angle arrow.
    // Rotate it around its center. At 0° it points right; at 90° it points down.
    auto rotatedPt = [chevX, chevY, rad](float dx, float dy) -> D2D1_POINT_2F {
        float cosA = std::cos(rad), sinA = std::sin(rad);
        return {chevX + dx * cosA - dy * sinA, chevY + dx * sinA + dy * cosA};
    };
    D2D1_POINT_2F p0 = rotatedPt(-halfSize * 0.5f, -halfSize);
    D2D1_POINT_2F p1 = rotatedPt(halfSize * 0.5f, 0.0f);
    D2D1_POINT_2F p2 = rotatedPt(-halfSize * 0.5f, halfSize);
    dc.DrawLine(p0, p1, pal.textPrimary, 1.5f);
    dc.DrawLine(p1, p2, pal.textPrimary, 1.5f);

    // --- Header text ---
    if (!header_.empty()) {
        IDWriteTextFormat* fmt = Dwrite()->Format(
            th.typography.bodySize, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            DWRITE_WORD_WRAPPING_NO_WRAP);
        if (fmt) {
            // VERTICAL EXTENT IS THE FULL HEADER ROW, NOT header-minus-padding.
            //
            // This used to be [hdrRect.y + kHeaderPadV, hdrRect.bottom() - kHeaderPadV],
            // which is algebraically exactly `bodySize` tall (HeaderHeight() is
            // bodySize + 2*kHeaderPadV). bodySize is the font's EM SIZE — 14 — but the
            // line box Segoe UI needs at 14 is 18.62 (ascent + descent + line gap all
            // live outside the em square). Combined with
            // D2D1_DRAW_TEXT_OPTIONS_CLIP, which makes the rect a HARD clip rather
            // than a hint, the 4.62 DIP shortfall sheared the descenders off every
            // g/y/p/q/j: "Settings" rendered as "Settinqs". Measured, not guessed —
            // see TextDescenderClipTests.ExpanderHeaderHeightExceedsEmSize.
            //
            // The padding was never needed here in the first place: the format uses
            // DWRITE_PARAGRAPH_ALIGNMENT_CENTER, so vertical position comes from
            // centring inside this rect, not from insetting it. Handing over the whole
            // header row gives the line box room (38 >= 18.62) and centres it in
            // exactly the same place, so nothing moves — the descenders simply stop
            // being clipped away.
            //
            // The HORIZONTAL inset stays: that one is doing real work, bounding a long
            // header so it truncates at the control edge instead of spilling out.
            D2D1_RECT_F textRect = D2D1::RectF(
                textX, hdrRect.y, bounds_.right() - kHeaderPadH, hdrRect.bottom());
            dc.DrawText(header_.c_str(), static_cast<UINT32>(header_.size()), fmt,
                        textRect, pal.textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    // --- Focus ring (header only) ---
    if (IsFocused()) {
        DrawFocusRing(dc, hdrRect, pal, HeaderFocusRing(th.spacing.cornerRadiusSmall));
    }

    // --- Content (clip to reveal height) ---
    if (content_ && content_->IsAttached() && reveal_ > 0.001f) {
        const float hdrH = HeaderHeight();
        const float clipH = ContentDesiredHeight() * static_cast<float>(reveal_);
        const float top = bounds_.y + hdrH;
        const float revealEdge = top + clipH;

        // The clip exists ONLY to implement the reveal wipe: content taller than the
        // currently revealed height must not spill past the animating edge. It was
        // written as exactly the content rect, and that sheared the focus ring off
        // every focused child sitting flush against any of the four edges.
        //
        // Arrange gives the content the FULL width (bounds_.x .. bounds_.right(),
        // Expander::Arrange), and single-child containers here do not honour a
        // child's Margin — so a focused Button inside is flush left, flush right, and
        // the last one is flush with the bottom. Its ring is drawn 3.75 DIP outside
        // its bounds and had nowhere to land. Measured on the Gallery's "Initially
        // expanded" card: clip x 50..450 / y 68..140 against a button at
        // x 50..450 / y 108..140 — three of four ring edges clipped away, which is
        // exactly the "only the top edge is drawn" artifact in the bug report.
        //
        // So the sides and the TOP are padded by the focus-ring allowance: nothing
        // about the reveal depends on them (the wipe is vertical, and the top edge is
        // the fixed boundary under the header, not the moving one).
        //
        // The BOTTOM is the moving edge and is deliberately NOT padded while the
        // animation runs — padding it would let content show up to 3.75 DIP below the
        // wipe, which is visible as content leaking out of a collapsing Expander.
        // Once the reveal is complete (fully open, nothing left to wipe) the bottom
        // is padded too, which is the state a keyboard user actually tabs into and
        // the one the screenshots show. WantsAnimationTick() is the same predicate
        // the frame loop uses, so "settled" here means exactly "no longer animating".
        const float pad = FocusRingPadDip(FocusRingSpec{});
        const float settledBottom =
            WantsAnimationTick() ? revealEdge : revealEdge + pad;

        ClipGuard clip = dc.PushClip(D2D1::RectF(
            bounds_.x - pad, top - pad, bounds_.right() + pad, settledBottom));
        content_->RenderWithOpacity(dc);
    }
}

// ---------------------------------------------------------------------------
// Hit-testing
// ---------------------------------------------------------------------------

UIElement* Expander::HitTestDeep(float dipX, float dipY) {
    if (!IsVisible() || !bounds_.contains(dipX, dipY)) return nullptr;
    // Route hits inside the content to the content subtree (only while expanded).
    if (content_ && content_->IsAttached() && reveal_ > 0.001f) {
        UIElement* hit = content_->HitTestDeep(dipX, dipY);
        if (hit) return hit;
    }
    // Hits in the header row, or anywhere when collapsed, land on this Expander.
    return this;
}

UIElement* Expander::HitTestDropTarget(float dipX, float dipY) {
    if (!IsVisible() || !bounds_.contains(dipX, dipY)) return nullptr;
    // Drop targets recurse the same way.
    if (content_ && content_->IsAttached() && reveal_ > 0.001f) {
        UIElement* hit = content_->HitTestDropTarget(dipX, dipY);
        if (hit) return hit;
    }
    return AcceptsDrop() ? this : nullptr;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Expander::OnPointerPressed(PointerEventArgs& e) {
    // Only clicks in the header toggle; clicks in the content route to the child.
    if (e.button == PointerButton::Left && HeaderRect().contains(e.position.x, e.position.y)) {
        ToggleFromUser();
        e.handled = true;
    }
}

void Expander::OnPointerMoved(PointerEventArgs& e) {
    bool wasHovered = headerHovered_;
    headerHovered_ = HeaderRect().contains(e.position.x, e.position.y);
    if (headerHovered_ != wasHovered) Invalidate();
}

void Expander::OnPointerLeft() {
    if (headerHovered_) {
        headerHovered_ = false;
        Invalidate();
    }
}

void Expander::OnKeyDownRouted(KeyEventArgs& e) {
    switch (e.vk) {
        case VK_SPACE:
        case VK_RETURN:
            ToggleFromUser();
            e.handled = true;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

bool Expander::WantsAnimationTick() const {
    // Epsilon must not be TIGHTER than the snap epsilon OnAnimationTick passes to
    // Approach, or the two disagree at the end of the ease: Approach would stop moving
    // the value (already within its snap band) while this still reports motion left,
    // and the element would sit in the active set forever being ticked to no effect —
    // an idle-cost leak, which is exactly the property this framework refuses to give
    // up. Both now use AnimatedValue's default 0.01 by omitting the argument, so they
    // cannot drift apart. If you tune one, tune both, and re-run
    // ExpanderTransitionTests.ReturnsToZeroCostIdleAfterAnimating.
    return reveal_.Animating(expanded_ ? 1.0f : 0.0f);
}

void Expander::OnAnimationTick(float dtSec) {
    // snapEps is AnimatedValue's default 0.01, matching every other animated control
    // here (Button tint, CheckBox check, RadioButton dot, Hyperlink hover). This used
    // to pass 0.001 — ten times tighter — and that tail was pure waste: measured, the
    // ease took 26 ticks (~432 ms at 60 Hz) instead of 12 (~200 ms), because the last
    // 0.9% of the reveal kept the element in the active set for another ~14 frames.
    //
    // Those frames are not free the way a Render-only tail would be. This tick raises
    // Measure (see below), so each one re-lays out the whole content subtree to move the
    // reveal by a sub-pixel amount nobody can see. Tightening an epsilon on a
    // Measure-level animation buys invisible precision at a real per-frame cost, and it
    // also delays the falling edge that returns the loop to its idle input wait.
    //
    // The threshold that matters is "smaller than a pixel of content height", and 0.01
    // of the reveal already is for any realistic content: 1% of even a 600 DIP panel is
    // 6 DIP of a height that is itself being interpolated, and the snap lands exactly on
    // the target rather than near it.
    reveal_.Approach(expanded_ ? 1.0f : 0.0f, dtSec, kRevealTau);

    // Measure, not Render: the reveal height feeds this Expander's desired size, so
    // the parent has to re-run layout on every frame of the ease. That makes this the
    // most expensive shape of animation in the framework (O(content subtree) per tick
    // rather than O(1)), which is why the tick count above is worth caring about.
    // Measured cost is still tiny — FluentUIBench "ExpanderReveal" reports ~0.003 ms
    // per tick+relayout with a 200-child content, about 0.02% of a 60 Hz frame — but
    // that is a reason to keep it cheap, not a licence to add frames.
    InvalidateDirty(DirtyFlags::Measure);
}

// ---------------------------------------------------------------------------
// Tree forwarding
// ---------------------------------------------------------------------------

void Expander::CollectFocusables(std::vector<UIElement*>& out) {
    if (!IsVisible()) return;
    if (IsFocusable()) out.push_back(this);
    if (content_ && content_->IsAttached()) content_->CollectFocusables(out);
}

void Expander::CollectDirtyBounds(std::vector<RectDip>& out) {
    if (IsVisible() && Any(Dirty())) out.push_back(VisualBounds());
    if (content_ && content_->IsAttached()) content_->CollectDirtyBounds(out);
}

void Expander::CollectAnimations(std::vector<UIElement*>& out) {
    if (WantsAnimationTick()) out.push_back(this);
    if (content_ && content_->IsAttached()) content_->CollectAnimations(out);
}

void Expander::OnVisibilityChanged(bool visible) {
    if (content_ && content_->IsAttached()) content_->OnVisibilityChanged(visible);
}

void Expander::OnAncestorVisibilityChanged() {
    Visual::OnAncestorVisibilityChanged();
    if (content_ && content_->IsAttached())
        static_cast<Visual*>(content_.get())->OnAncestorVisibilityChanged();
}

void Expander::OnThemeChanged() {
    Invalidate();
    if (content_) content_->OnThemeChanged();
}

void Expander::OnDpiChanged(float dpiScale) {
    Invalidate();
    if (content_) content_->OnDpiChanged(dpiScale);
}

void Expander::OnDeviceLost() {
    if (content_) content_->OnDeviceLost();
}

void Expander::OnDeviceRestored() {
    if (content_) content_->OnDeviceRestored();
}

bool Expander::AnyDirtyInSubtree(DirtyFlags flags) const {
    if (Has(Dirty(), flags)) return true;
    if (content_ && content_->IsAttached() && content_->AnyDirtyInSubtree(flags)) return true;
    return false;
}

void Expander::ClearDirtySubtree() {
    ClearDirty();
    if (content_) content_->ClearDirtySubtree();
}

} // namespace fluent
