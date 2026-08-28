// Panel.h — container element that owns and lays out child elements.
//
// A Panel owns its children (unique_ptr) and recurses for render, hit-testing,
// focus collection, and theme/DPI notifications. Coordinates stay absolute
// (window DIPs): the panel computes each child's absolute bounds in Arrange.
//
// Subclasses implement MeasureOverride/ArrangeOverride to define a layout
// strategy (see StackPanel). The two-pass protocol mirrors WPF:
//   Measure(avail)  -> fills each child's desired size, then ours
//   Arrange(rect)   -> positions each child within rect
#pragma once

#include "../core/FrameworkElement.h"
#include <algorithm>  // std::min — InsertAt clamps the index to [0, size()]
#include <memory>
#include <vector>

namespace fluent {

class Panel : public FrameworkElement {
public:
    // Take ownership of a child and return a borrowed pointer for the caller to
    // configure. Children render/route in registration order. If this panel is
    // already attached to a live tree, the new child is attached immediately so
    // late additions get the context without the caller wiring anything.
    template <typename T>
    T* Add(std::unique_ptr<T> child) {
        T* raw = child.get();
        if (raw) {
            raw->SetParent(this);
            if (IsAttached()) {
                raw->AttachToContext(Context());
                static_cast<Visual*>(raw)->OnAncestorVisibilityChanged();
            }
        }
        children_.push_back(std::move(child));
        return raw;
    }

    // Convenience: construct + add in one call.
    template <typename T, typename... Args>
    T* Emplace(Args&&... args) {
        return Add(std::make_unique<T>(std::forward<Args>(args)...));
    }

    void Clear() {
        for (auto& c : children_) {
            if (!c) continue;
            if (c->IsAttached()) c->DetachFromContext();
            c->SetParent(nullptr);
        }
        children_.clear();
    }
    size_t ChildCount() const { return children_.size(); }

    // Read-only access to a child by index, for code that has to WALK the tree rather
    // than build it: an inspector, a debug overlay, a test that asserts structure.
    //
    // Read-only on purpose. It hands out a raw pointer, never the unique_ptr, so a caller
    // cannot mutate the child list directly — that would break Measure/Arrange caching.
    // All list mutations go through Panel's public API (Add/Clear/RemoveAt/InsertAt),
    // which invalidates Measure to keep layout caching correct. Returns nullptr for an
    // out-of-range index instead of asserting, because a walker iterating a tree that is
    // being mutated is a normal situation for a debugging tool, not a programming error.
    FrameworkElement* ChildAt(size_t i) const {
        return i < children_.size() ? children_[i].get() : nullptr;
    }

    // Remove the child at the given index. Out-of-range index is a no-op rather than
    // an error, matching ChildAt's bounds-checking philosophy (debugging tools may
    // iterate a mutating tree). The removed element is detached and its parent pointer
    // cleared before the unique_ptr destructs, so subscriptions cancel cleanly.
    void RemoveAt(size_t index);

    // Insert a child at the given index. Index is clamped to [0, ChildCount()], so
    // passing size() appends to the end and passing a value beyond size() also appends.
    // Returns a borrowed pointer for the caller to configure, matching Add's contract.
    // If this panel is already attached to a live tree, the new child is attached
    // immediately so late insertions get the context without the caller wiring anything.
    template <typename T>
    T* InsertAt(size_t index, std::unique_ptr<T> child) {
        T* raw = child.get();
        if (!raw) return nullptr;

        // Clamp to [0, size()] — beyond the end becomes append.
        const size_t pos = std::min(index, children_.size());

        // 1. Insert into vector at the target position.
        children_.insert(children_.begin() + pos, std::move(child));

        // 2. Establish parent relationship.
        raw->SetParent(this);

        // 3. If the panel is attached, attach the new child immediately (matching Add's
        //    late-addition behavior — see Panel.h:30-33).
        if (IsAttached()) {
            raw->AttachToContext(Context());
            static_cast<Visual*>(raw)->OnAncestorVisibilityChanged();
        }

        // 4. Mark that the panel's desired size may have changed.
        InvalidateDirty(DirtyFlags::Measure);

        return raw;
    }

    // Run a full layout pass over `area` (absolute DIPs). The app calls this
    // from the window's OnLayout on the root panel.
    void UpdateLayout(const RectDip& area);

    // Element overrides: recurse into children.
    void Render(const DrawingContext& dc) override;
    UIElement* HitTestDeep(float dipX, float dipY) override;
    UIElement* HitTestDropTarget(float dipX, float dipY) override;
    void CollectFocusables(std::vector<UIElement*>& out) override;
    void CollectDirtyBounds(std::vector<RectDip>& out) override;
    // Both visibility hooks recurse into children. This is what carries a
    // SetVisible(false) on a container down to a composited descendant (TextArea /
    // TreeView), whose DComp visual is parented on the COMPOSITOR ROOT rather than
    // under this panel — so it keeps painting no matter what the D2D render path
    // does. Children read IsEffectivelyVisible() to learn their real state.
    void OnVisibilityChanged(bool visible) override;
    void OnAncestorVisibilityChanged() override;
    void OnThemeChanged() override;
    void OnDpiChanged(float dpiScale) override;
    void OnDeviceLost() override;
    void OnDeviceRestored() override;
    void CollectAnimations(std::vector<UIElement*>& out) override;
    bool AnyDirtyInSubtree(DirtyFlags flags) const override;
    void ClearDirtySubtree() override;
    // A panel must re-measure when its own Measure bit is set OR any descendant
    // needs it: a child's changed desired size feeds the panel's MeasureOverride.
    // Without this, MeasureCached would reuse the panel's stale size after a
    // deeply-nested control changed (roadmap §6.2).
    bool NeedsRemeasure() const override { return AnyDirtyInSubtree(DirtyFlags::Measure); }

    void Measure(float availW, float availH) override;
    void Arrange(const RectDip& finalRect) override;

protected:
    // Propagate the tree context into / out of the owned children (roadmap §6.3).
    // The base UIElement lifecycle calls these: on attach after this panel's own
    // OnAttachedToTree (self first, then subtree), on detach before it (subtree
    // first, then self), so ordering matches WPF's mount/unmount.
    void AttachChildren(const UIContext& ctx) override;
    void DetachChildren() override;
    void UpdateChildrenContextDpi(float dpiScale) override;
    void UpdateChildrenContextModalResize(bool inModalResize) override;

    // Optional background fill painted before children (transparent by default).
    virtual void OnRenderBackground(const DrawingContext& dc) {
        UNREFERENCED_PARAMETER(dc);
    }

    // Layout strategy hooks. MeasureOverride returns the panel's desired size
    // given the available space; ArrangeOverride positions children within the
    // panel's content rect.
    virtual SizeDip MeasureOverride(float availW, float availH) = 0;
    virtual void ArrangeOverride(const RectDip& content) = 0;

    // Place a child within `slot` honoring its margin + alignment. Helper for
    // ArrangeOverride implementations.
    static void ArrangeChild(FrameworkElement* child, const RectDip& slot);

private:
    // Shared body of OnVisibilityChanged/OnAncestorVisibilityChanged: neither
    // needs the argument (children recompute their own IsEffectivelyVisible()),
    // so both just forward here.
    void NotifyChildrenAncestorVisibilityChanged();

protected:
    std::vector<std::unique_ptr<FrameworkElement>> children_;
};

} // namespace fluent
