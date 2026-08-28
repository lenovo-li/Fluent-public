// Layout.h — shared layout value types (DIP units).
//
// Geometry stays in absolute window DIPs throughout the library; panels compute
// and set each child's absolute bounds during Arrange. These types describe how
// an element wants to be sized and positioned within the space a panel offers.
#pragma once

#include <limits>

namespace fluent {

// A margin / padding box in DIPs.
struct Thickness {
    float left = 0, top = 0, right = 0, bottom = 0;

    Thickness() = default;
    explicit Thickness(float all) : left(all), top(all), right(all), bottom(all) {}
    Thickness(float h, float v) : left(h), top(v), right(h), bottom(v) {}
    Thickness(float l, float t, float r, float b)
        : left(l), top(t), right(r), bottom(b) {}

    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }

    bool operator==(const Thickness& o) const {
        return left == o.left && top == o.top && right == o.right &&
               bottom == o.bottom;
    }
    bool operator!=(const Thickness& o) const { return !(*this == o); }
};

// A size in DIPs.
struct SizeDip {
    float w = 0, h = 0;
};

// Cross/main-axis placement when the element is smaller than the slot it is
// arranged into. Stretch makes the element fill the slot on that axis.
enum class HAlign { Left, Center, Right, Stretch };
enum class VAlign { Top, Center, Bottom, Stretch };

// Sentinel for "no explicit size; size to content / available space".
constexpr float kAuto = std::numeric_limits<float>::quiet_NaN();
inline bool IsAuto(float v) { return v != v; }  // NaN != NaN

// A Grid track length. Auto sizes to the largest child in the track; Pixel is a
// fixed DIP size; Star shares leftover space in proportion to `value` (weight).
enum class GridUnit { Auto, Pixel, Star };

struct GridLength {
    GridUnit unit = GridUnit::Star;
    float value = 1.0f;  // Pixel: DIP size; Star: weight; Auto: ignored.

    GridLength() = default;
    GridLength(GridUnit u, float v) : unit(u), value(v) {}

    static GridLength Auto() { return {GridUnit::Auto, 0.0f}; }
    static GridLength Pixels(float p) { return {GridUnit::Pixel, p}; }
    static GridLength Star(float w = 1.0f) { return {GridUnit::Star, w}; }

    bool isAuto() const { return unit == GridUnit::Auto; }
    bool isPixel() const { return unit == GridUnit::Pixel; }
    bool isStar() const { return unit == GridUnit::Star; }
};

} // namespace fluent
