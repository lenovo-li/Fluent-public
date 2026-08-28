// TextBlock.h — DWrite read-only text element.
#pragma once

#include "../core/Control.h"
#include "../graphics/DWriteContext.h"
#include <string>

namespace fluent {

// Test-only peer (defined in the tests) to reach the private layout cache for
// verifying reuse/invalidation without exposing it in the product API.
struct TextBlockTestPeer;

// Semantic type ramp role (roadmap §11). A TextBlock either follows a role —
// resolving its font size from the theme's TypographyTokens (Caption 12 / Body 14
// / Subtitle 20 / Title 28) so a theme change re-sizes it — or keeps an explicit
// SetFontSize override. Default is Body. Custom keeps whatever SetFontSize set.
enum class TypographyRole { Caption, Body, Subtitle, Title, Custom };

class TextBlock : public Control {
public:
    // A TextBlock is a pure static label by default: not focusable, no I-beam,
    // no text selection. A scrollable / selectable read-only panel (e.g. a
    // device-detail view) opts in with SetSelectable(true) — then a click moves
    // keyboard focus here (off e.g. a TreeView), arrow/page keys scroll it, the
    // wheel scrolls it, and the pointer selects text.
    TextBlock() = default;

    // DWrite is no longer injected per-control (roadmap §6.2): the text context is
    // read from the tree UIContext (Context().dwrite) once the element is attached.
    // Opt into focus + wheel/arrow scrolling + text selection (I-beam cursor).
    void SetSelectable(bool on) { selectable_ = on; SetFocusable(on); }
    void SetText(std::wstring text) { SetProperty(text_, std::move(text), DirtyFlags::Measure); }
    // An explicit size opts out of the type ramp (role becomes Custom).
    //
    // This writes the size TWICE, deliberately: to TextBlock's own fontSize_ (which
    // ResolvedFontSize() reads and which participates in the layout cache key) and
    // to the Control base's optional fontSize_ via Control::SetFontSize.
    //
    // Why both: TextBlock's fontSize_ member SHADOWS the base's std::optional<float>
    // fontSize_. Without the second write, generic code driving the Control property
    // layer — a property inspector, a style applier, anything calling FontSize() or
    // EffectiveFontSize() through a Control* — would read nullopt and conclude the
    // block is at the theme's bodySize, while the block actually renders at sizeDip.
    // Two observers of one control disagreeing about its font size is the bug this
    // prevents.
    //
    // This is a treatment of the symptom, not the disease (the disease is the two
    // parallel size systems), and that was the deliberate choice: unifying them would
    // change what ClearFontSize() means — today it restores 13 DIP, after a merge it
    // would restore whatever the TypographyRole maps to. That semantic change is not
    // worth making silently, so the two systems stay and are kept in sync here.
    //
    // Note the asymmetry: SetTypographyRole does NOT write the base field, because a
    // role is "follow the theme's ramp", which is exactly what nullopt already means.
    void SetFontSize(float sizeDip) {
        role_ = TypographyRole::Custom;
        Control::SetFontSize(sizeDip);
        SetProperty(fontSize_, sizeDip, DirtyFlags::Measure);
    }
    // Clears both: the TextBlock's fontSize_ and the Control base's optional field.
    // Without clearing both, generic code reading the Control layer would see nullopt
    // (theme-driven) while TextBlock renders at the old explicit size.
    void ClearFontSize() {
        role_ = TypographyRole::Body;  // restores semantic default
        Control::ClearFontSize();
        SetProperty(fontSize_, 13.0f, DirtyFlags::Measure);
    }
    // Follow a semantic type-ramp role: the effective size resolves from the
    // theme's TypographyTokens (re-measures, since the size may change).
    void SetTypographyRole(TypographyRole role) {
        SetProperty(role_, role, DirtyFlags::Measure);
    }
    TypographyRole Role() const { return role_; }
    void SetWeight(DWRITE_FONT_WEIGHT weight) { SetProperty(weight_, weight, DirtyFlags::Measure); }
    void SetDimmed(bool dimmed) { SetProperty(dimmed_, dimmed, DirtyFlags::Render); }
    void SetWrap(bool wrap) { SetProperty(wrap_, wrap, DirtyFlags::Measure); }
    void SetAlignment(DWRITE_TEXT_ALIGNMENT align) { SetProperty(align_, align, DirtyFlags::Render); }
    // Line height as a multiple of the font size (e.g. 1.5 = roomier lines). 0 =
    // DWrite default (tight). Applied uniformly so mixed CJK/Latin lines align.
    void SetLineSpacing(float factor) { SetProperty(lineSpacing_, factor, DirtyFlags::Measure); }
    void SetScrollOffset(float offsetDip);
    void ScrollBy(float deltaDip);
    float ScrollOffset() const { return scrollOffset_; }
    float ContentHeight() const { return contentHeight_; }
    bool HasSelection() const;
    void ClearSelection();
    bool CopySelectionToClipboard(HWND owner) const;

    void Render(const DrawingContext& dc) override;
    // Size to the text content: desired height = wrapped text height at the
    // available width (unless an explicit width/height overrides it).
    void Measure(float availW, float availH) override;

    // Consume the wheel when there is something to scroll, so the event does not
    // keep bubbling to a parent scroller. Arrow/Page/Home/End scroll while focused.
    // I-beam cursor when selectable. All routed (WP-03).
    void OnPointerWheelChanged(PointerEventArgs& e) override;
    void OnKeyDownRouted(KeyEventArgs& e) override;
    HCURSOR Cursor() const override;

    // Routed pointer input: thumb drag + text selection (with capture).
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

protected:
    // DWrite becomes available on attach: re-measure so a TextBlock built and
    // sized before it joined the tree lays out its text now (roadmap §6.2/§6.3).
    void OnAttachedToTree() override { InvalidateMeasure(); }
    void OnBoundsChanged() override;

private:
    void UpdateMetrics();
    // Returns a text layout for the current content at the current bounds width,
    // reusing a cached IDWriteTextLayout when none of the inputs that define it
    // changed (roadmap §6.3). Render + UpdateMetrics + hit-testing all call this,
    // several times per frame, so rebuilding the layout each time was a per-frame
    // DWrite hot spot. The cache is keyed on the full set of layout inputs and
    // self-invalidates on any mismatch, so setters need no special handling.
    ComPtr<IDWriteTextLayout> CreateLayout() const;

    // Cache key for CreateLayout: every field that changes the produced layout.
    struct LayoutKey {
        std::wstring text;
        float fontSize = 0.0f;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
        bool wrap = true;
        float lineSpacing = 0.0f;
        float width = 0.0f;   // wrap width (bounds_.w)
        bool valid = false;   // false until the first successful build
        bool Matches(const LayoutKey& o) const {
            return valid && o.valid && text == o.text && fontSize == o.fontSize &&
                   weight == o.weight && align == o.align && wrap == o.wrap &&
                   lineSpacing == o.lineSpacing && width == o.width;
        }
    };
    UINT32 HitTextPosition(float dipX, float dipY, bool* trailing = nullptr) const;
    void SelectionRange(UINT32& start, UINT32& length) const;
    RectDip ThumbRect() const;
    bool HitThumb(float dipX, float dipY) const;

    // Resolve the effective font size from the typography role: an explicit
    // SetFontSize (Custom) wins; otherwise map the role onto the theme's type ramp.
    // Renamed from the old EffectiveFontSize to avoid shadowing the Control-layer
    // property (which is font-size-as-a-property; this is role→size semantic routing).
    float ResolveTypographySize() const;

    std::wstring text_;
    // Default role is Custom with the historical 13 DIP size so an un-themed /
    // un-roled TextBlock renders exactly as before (value-preserving). Opt into
    // the type ramp with SetTypographyRole.
    TypographyRole role_ = TypographyRole::Custom;
    float fontSize_ = 13.0f;
    float scrollOffset_ = 0.0f;
    float contentHeight_ = 0.0f;
    bool draggingThumb_ = false;
    float dragStartY_ = 0.0f;
    float dragStartOffset_ = 0.0f;
    bool selecting_ = false;
    UINT32 selectionAnchor_ = 0;
    UINT32 selectionCaret_ = 0;
    DWRITE_FONT_WEIGHT weight_ = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_TEXT_ALIGNMENT align_ = DWRITE_TEXT_ALIGNMENT_LEADING;
    float lineSpacing_ = 0.0f;  // 0 = DWrite default; >0 = font-size multiple
    bool dimmed_ = false;
    bool wrap_ = true;
    bool selectable_ = false;  // opt-in: focus + scroll + text selection (SetSelectable)

    // Cached layout + the key it was built from (roadmap §6.3). Mutable because
    // CreateLayout() is const (called from Render/hit-test) but memoizes here.
    mutable ComPtr<IDWriteTextLayout> cachedLayout_;
    mutable LayoutKey cachedKey_;

    friend struct TextBlockTestPeer;
};

} // namespace fluent
