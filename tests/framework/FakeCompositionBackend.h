// FakeCompositionBackend.h — a headless, in-memory ICompositionBackend for unit
// tests (Phase 2). No GPU, no window, no DirectComposition.
//
// It records what a control asks the compositor to do — visuals created, the
// root membership, per-visual offset / opacity / clip, the animation attached to
// each property (with the SweepSpec), child parenting, surface draw calls, and
// commit requests — so a test can assert on compositor *behaviour* without any
// pixels. DrawSurface invokes the caller's callback with a null D2D context, so a
// control's draw code must tolerate nullptr (the product code guards this).
//
// Header-only: included directly by the composition + ProgressBar tests. Lives in
// the test tree, never linked into the product.
#pragma once

#include "../../FluentUI/composition/ICompositionBackend.h"
#include <algorithm>
#include <optional>
#include <vector>

namespace fltest {

// Records the animation currently bound to a property.
struct FakeAnimation {
    fluent::CompositionProperty property{};
    fluent::SweepSpec sweep{};   // valid when isSweep
    bool isSweep = false;
    // Decelerate OffsetY tween (valid when isTween): the from/to/duration a
    // StartOffsetYTween recorded, so a test can assert the retarget seed + target.
    bool isTween = false;
    float fromPx = 0.0f, toPx = 0.0f;
    double durationSec = 0.0;
    // Infinite caret-blink square wave on opacity (valid when isBlink).
    bool isBlink = false;
    double halfPeriodSec = 0.0;
    int blinkStarts = 0;   // restart count (ResetBlink makes the caret solid again)
};

class FakeCompositionVisual : public fluent::ICompositionVisual {
public:
    // --- Recorded state (public for direct assertion) ---
    float offsetX = 0.0f, offsetY = 0.0f;
    float opacity = 1.0f;
    bool hasClip = false;
    float clipL = 0, clipT = 0, clipR = 0, clipB = 0;
    std::optional<FakeAnimation> offsetXAnim;   // set by StartOffsetSweep
    std::optional<FakeAnimation> offsetYAnim;   // set by StartOffsetYTween
    std::optional<FakeAnimation> opacityAnim;   // set by StartOpacityBlink
    int drawCount = 0;                          // DrawSurface calls
    uint32_t lastDrawW = 0, lastDrawH = 0;
    std::vector<fluent::ICompositionVisual*> children;

    void SetOffset(float x, float y) override {
        offsetX = x;
        offsetY = y;
        offsetXAnim.reset();  // a static offset replaces a running animation
        offsetYAnim.reset();
    }
    void SetOpacity(float o) override {
        opacity = o;
        opacityAnim.reset();  // a static opacity replaces a running animation
    }
    void SetClip(float l, float t, float r, float b) override {
        hasClip = true; clipL = l; clipT = t; clipR = r; clipB = b;
    }
    void ClearClip() override { hasClip = false; }

    void StartOffsetSweep(const fluent::SweepSpec& spec) override {
        FakeAnimation a;
        a.property = fluent::CompositionProperty::OffsetX;
        a.sweep = spec;
        a.isSweep = true;
        offsetXAnim = a;
    }
    void StartOffsetYTween(float fromPx, float toPx, double durationSec) override {
        FakeAnimation a;
        a.property = fluent::CompositionProperty::OffsetY;
        a.isTween = true;
        a.fromPx = fromPx;
        a.toPx = toPx;
        a.durationSec = durationSec;
        offsetYAnim = a;
        offsetY = toPx;  // fake "settled" value, for a test that reads the end state
    }
    void StartOpacityBlink(double halfPeriodSec) override {
        FakeAnimation a;
        a.property = fluent::CompositionProperty::Opacity;
        a.isBlink = true;
        a.halfPeriodSec = halfPeriodSec;
        // Keep counting restarts across calls — a test asserts that typing restarts
        // the blink phase (caret goes solid) rather than leaving it mid-cycle.
        a.blinkStarts = opacityAnim ? opacityAnim->blinkStarts + 1 : 1;
        opacityAnim = a;
        opacity = 1.0f;  // fake "solid" phase, for a test that reads the value
    }
    void StopAnimation(fluent::CompositionProperty property) override {
        if (property == fluent::CompositionProperty::OffsetX) offsetXAnim.reset();
        if (property == fluent::CompositionProperty::OffsetY) offsetYAnim.reset();
        if (property == fluent::CompositionProperty::Opacity) opacityAnim.reset();
    }

    bool DrawSurface(uint32_t w, uint32_t h, const DrawCallback& draw) override {
        ++drawCount;
        lastDrawW = w;
        lastDrawH = h;
        if (draw) draw(nullptr, 0.0f, 0.0f);  // headless: null DC, no rasterization
        return false;  // no real surface produced
    }

    void AddChild(fluent::ICompositionVisual* child) override {
        if (child) children.push_back(child);
    }
    void RemoveChild(fluent::ICompositionVisual* child) override {
        children.erase(std::remove(children.begin(), children.end(), child),
                       children.end());
    }

    bool IsAnimatingOffsetX() const { return offsetXAnim.has_value(); }
    bool IsAnimatingOffsetY() const { return offsetYAnim.has_value(); }
    bool IsBlinking() const { return opacityAnim.has_value() && opacityAnim->isBlink; }
};

class FakeCompositionBackend : public fluent::ICompositionBackend {
public:
    // --- Recorded state ---
    int createdVisuals = 0;
    int commitRequests = 0;
    // Non-owning pointers to every visual on the root, in add order.
    std::vector<fluent::ICompositionVisual*> rootVisuals;

    std::unique_ptr<fluent::ICompositionVisual> CreateVisual() override {
        ++createdVisuals;
        return std::make_unique<FakeCompositionVisual>();
    }
    void AddToRoot(fluent::ICompositionVisual* v) override {
        if (v) rootVisuals.push_back(v);
    }
    void RemoveFromRoot(fluent::ICompositionVisual* v) override {
        rootVisuals.erase(std::remove(rootVisuals.begin(), rootVisuals.end(), v),
                          rootVisuals.end());
    }
    void RequestCommit() override { ++commitRequests; }

    int RootCount() const { return static_cast<int>(rootVisuals.size()); }
};

}  // namespace fltest
