// FocusVisual.h — the shared Fluent focus ring (roadmap §11, WP-05).
//
// Every focusable control drew its own accent outline just outside its bounds
// (Button: bounds inflated 2, corner kCorner+2; CheckBox: box inflated 3, corner
// kBoxCorner+3), all reading pal.accent. This folds that into one helper keyed on
// the ColorTokens.focusStroke token, so the ring color/shape lives in one place.
//
// ORDERING CONSTRAINT (real, see the memo): the ring is drawn OUTSIDE the
// content rect (inset expands it), so it must be emitted BEFORE any content clip
// is pushed — a control that clips its content to `bounds_` (e.g. TextBlock's
// ClipGuard on bounds_) would otherwise clip the ring away. Draw the ring first,
// then push the content clip.
//
// FocusRingRect is split out as a pure function so the geometry is unit-testable
// without a DrawingContext (FocusVisualTests).
#pragma once

#include "../graphics/DrawingContext.h"
#include "ThemeTokens.h"
#include "../fl_common.h"
#include <d2d1helper.h>

namespace fluent {

// How the ring sits relative to the content rect. Defaults match the historical
// Button ring (inset 2, stroke 1.5); a control passes its own cornerRadius (the
// content's rounding) and, if different, its inset.
struct FocusRingSpec {
    float inset = 2.0f;         // how far outside the content rect the ring sits
    float cornerRadius = 4.0f;  // the content rect's corner radius (ring adds inset)
    float strokeWidth = 1.5f;
};

// Pure geometry: the rounded rect the ring is stroked along. The rect is the
// content inflated by `inset` on all sides; the corner radius grows by the same
// inset so the ring stays concentric with the content's rounding.
inline D2D1_ROUNDED_RECT FocusRingRect(const RectDip& content,
                                       const FocusRingSpec& spec) {
    return D2D1::RoundedRect(
        D2D1::RectF(content.x - spec.inset, content.y - spec.inset,
                    content.right() + spec.inset, content.bottom() + spec.inset),
        spec.cornerRadius + spec.inset, spec.cornerRadius + spec.inset);
}

// How far outside the content rect the ring actually puts pixels: the inset, plus
// the half of the centered stroke that falls outward, plus a pixel of antialiasing.
// A focusable control MUST inflate its CollectDirtyBounds by this (WP-07 §S4) —
// otherwise a partial redraw clips the ring's outer edge and, once focus moves on,
// leaves the outer edge behind as residue. The redraw region is exactly this frame's
// dirty union, so anything a control paints outside its layout bounds has to be
// declared. Same defect Slider::CollectDirtyBounds exists to avoid.
inline float FocusRingPadDip(const FocusRingSpec& spec) {
    return spec.inset + spec.strokeWidth * 0.5f + 1.0f;
}

// Stroke the focus ring in the theme's focusStroke color. Call before pushing any
// content clip (see the ordering constraint above).
inline void DrawFocusRing(const DrawingContext& dc, const RectDip& content,
                          const ColorTokens& colors, const FocusRingSpec& spec) {
    dc.DrawRoundedRect(FocusRingRect(content, spec), colors.focusStroke,
                       spec.strokeWidth);
}

} // namespace fluent
