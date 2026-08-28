// LayoutTestHelpers.h — a concrete leaf Element for pure-logic layout tests.
//
// Element::Render is pure virtual, so tests cannot instantiate it directly.
// TestLeaf is a minimal no-op leaf whose desired size follows the default
// Element::Measure (explicit size when set, else the available space). It never
// touches D2D, so layout math can be tested with no device/window.
#pragma once

#include "../../FluentUI/core/FrameworkElement.h"

namespace fluent {

class TestLeaf : public FrameworkElement {
public:
    // A leaf with an explicit fixed size (the common case in layout tests).
    TestLeaf() = default;
    TestLeaf(float w, float h) {
        SetWidth(w);
        SetHeight(h);
    }
    void Render(const DrawingContext&) override {}
};

} // namespace fluent
