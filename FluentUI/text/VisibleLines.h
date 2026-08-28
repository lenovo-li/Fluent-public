// VisibleLines.h — which lines intersect a band of content space.
//
// The arithmetic at the heart of NoWrap virtualization, extracted as a pure
// function for the reason the repo extracts PlanRedraw and FramePacing: the draw
// loop that uses it needs a device context, a composition surface and a window, so
// a test that goes through the loop cannot reach this logic at all. Headless, the
// draw callback receives a null DC and returns immediately — meaning a bug that
// made the loop walk the entire document would be invisible to every test while
// the suite stayed green. That is precisely the failure mode this codebase has been
// bitten by before, so the judgment lives here instead, where it can be checked.
//
// The contract is what makes the cost O(visible) instead of O(document): given the
// band the surface covers, return only the line indices that can appear in it. A
// caller that respects the returned range does work proportional to the band; one
// that ignores it does work proportional to the buffer.
#pragma once

#include <cstddef>

namespace fluent {

// Half-open range of line indices [first, last) that intersect the band
// [bandTopDip, bandTopDip + bandHeightDip) of content space, given a constant
// line height and a total line count.
//
// Empty (first == last) when the band lies entirely past the last line, which
// happens legitimately: the overscan surface is taller than the document near the
// end of a short buffer.
struct LineSpan {
    size_t first = 0;
    size_t last = 0;   // exclusive
    bool Empty() const { return first >= last; }
    size_t Count() const { return Empty() ? 0 : last - first; }
};

// `bandTopDip` may be negative (an overscan surface can start above the document)
// and is clamped to 0. `lineHeightDip` must be > 0; a non-positive height would
// make the division meaningless, and rather than divide by zero this returns an
// empty span — the caller then draws nothing, which is the correct behaviour
// before DWrite has supplied real font metrics.
//
// The band's bottom edge is EXCLUSIVE, so a line whose top sits exactly on it is
// outside. That matters at the seam between two adjacent bands: with an inclusive
// bound the line on the boundary would be drawn by both, which is harmless for
// pixels but doubles the layout work for that row on every scroll.
inline LineSpan VisibleLineSpan(float bandTopDip, float bandHeightDip,
                               float lineHeightDip, size_t lineCount) {
    LineSpan span;
    if (lineHeightDip <= 0.0f || lineCount == 0 || bandHeightDip <= 0.0f) return span;

    const float top = bandTopDip > 0.0f ? bandTopDip : 0.0f;
    const float bottom = bandTopDip + bandHeightDip;
    if (bottom <= 0.0f) return span;    // band entirely above the document

    const size_t first = static_cast<size_t>(top / lineHeightDip);
    if (first >= lineCount) return span;  // band entirely below the last line

    // The first line whose TOP is at or past the band's bottom edge is the first
    // one not drawn. Computed as a count rather than by iterating so this stays
    // O(1) — iterating here would reintroduce the cost the whole design removes.
    size_t last = static_cast<size_t>(bottom / lineHeightDip) + 1;
    if (last > lineCount) last = lineCount;

    span.first = first;
    span.last = last;
    return span;
}

} // namespace fluent
