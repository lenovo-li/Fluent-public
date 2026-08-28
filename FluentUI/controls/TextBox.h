// TextBox.h — single-line, self-drawn Fluent text input.
//
// A single-line editor built on TextEditBase (which supplies the text model,
// selection, IME, clipboard, blink, and editing keys). TextBox adds the
// single-line specifics: a non-wrapping layout, horizontal scrolling, left/right
// caret movement, password masking, newline-stripping input, and rendering.
//
// Fully DWrite-rendered into the window content surface (no child HWND). Unlike
// TextArea, TextBox scrolls horizontally over one line and has NOT been migrated to
// the compositor — it draws and blinks on the UI thread.
// Geometry is in DIPs; indices are UTF-16 code-unit offsets.
#pragma once

#include "TextEditBase.h"

namespace fluent {

struct TextBoxTestPeer;  // component test access to CreateLayout (below)

class TextBox : public TextEditBase {
    friend struct TextBoxTestPeer;
public:
    void SetPassword(bool on) { password_ = on; OnTextLayoutDirty(); Invalidate(); }

    // Element overrides.
    void Render(const DrawingContext& dc) override;
    bool CaretRectDip(RectDip& out) const override;

    // Natural size: WinUI TextBox MinHeight = 32 DIP, single line of body text
    // plus vertical padding. Without this the inherited FrameworkElement::Measure
    // mirrors the available height — which is infinity inside an auto-sized
    // container (StackPanel in a card) and collapses the control to zero.
    void Measure(float availW, float availH) override;

protected:
    // TextEditBase hooks.
    void OnTextLayoutDirty() override {}   // layout is rebuilt on demand (cheap)
    void EnsureCaretVisible() override;
    bool OnNavigationKey(UINT vk, bool shift) override;
    std::wstring SanitizeInput(std::wstring s) const override;
    const std::wstring& DisplayText(std::wstring& scratch) const override;

public:
    // Routed pointer input: click-to-place + drag-select (with capture).
    void OnPointerPressed(PointerEventArgs& e) override;
    void OnPointerMoved(PointerEventArgs& e) override;
    void OnPointerReleased(PointerEventArgs& e) override;

private:
    ComPtr<IDWriteTextLayout> CreateLayout(const std::wstring& s) const;
    float CaretX(UINT32 index) const;     // caret X (DIP, pre-scroll) for an index
    UINT32 HitIndex(float dipX) const;    // code-unit index nearest dipX
    // TextEditBase hook for the context menu. Single-line, so Y carries no information.
    UINT32 IndexAtPoint(float dipX, float /*dipY*/) const override {
        return HitIndex(dipX);
    }
    void ClampScroll();
    float ContentLeft() const;            // text origin X within bounds (padding)
    float ContentWidth() const;
    // The string laid out for measuring geometry (password dots, no composition).
    std::wstring GeometryText() const;

    bool password_ = false;
    float scrollX_ = 0.0f;                // horizontal scroll offset (DIP)
};

} // namespace fluent
