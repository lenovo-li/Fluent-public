// ContentControl.h — a Control that carries a text label (roadmap §WP-06).
//
// The interactive controls that show a caption (Button, CheckBox, RadioButton,
// ToggleSwitch) all held their own `std::wstring text_` + an identical
// `SetText(...)` that dirties Measure, and CheckBox / RadioButton additionally
// carried a byte-for-byte identical "measure the label width with a throwaway
// DWrite layout" block. ContentControl lifts both: the text property lives here,
// and MeasureLabelWidth centralizes the DWrite metrics call.
//
// This is deliberately text-only (not a general WPF-style object Content): every
// content control in this library shows a string, so a string is the content.
// Layout and painting of the label stay in the subclass, which knows where the
// box / track sits relative to the caption.
#pragma once

#include "Control.h"
#include "../graphics/DWriteContext.h"
#include "../graphics/ResourceCache.h"
#include <algorithm>
#include <string>

namespace fluent {

class ContentControl : public Control {
public:
    // The caption. Changing it can change the measured width, so it dirties
    // Measure (same semantics as the old per-control SetText).
    void SetText(std::wstring text) {
        SetProperty(text_, std::move(text), DirtyFlags::Measure);
    }
    const std::wstring& Text() const { return text_; }

protected:
    // Width (DIP) of the label laid out at `fontSize` with the normal weight,
    // leading-aligned, no wrap — the exact metric CheckBox/RadioButton computed
    // inline. Returns 0 when there is no DWrite yet (detached) or no text.
    // `maxWidth` bounds the layout box (callers pass the available width, else a
    // large sentinel), and `boxHeight` is the layout height (label is single-line
    // so it only affects the box, not the returned width).
    // Route through the shared, epoch-versioned layout cache (roadmap §13.3), the
    // same path TextBox uses. This runs on EVERY Measure of every Button / CheckBox
    // / RadioButton / ToggleSwitch, and a Measure re-runs whenever the constraint
    // changes — so a resize drag rebuilt one throwaway IDWriteTextLayout per
    // labelled control per frame, then dropped it, having only read one float off
    // it. The label text is what determines the width, and it almost never changes;
    // the cache turns the steady state into a hash lookup.
    //
    // maxWidth IS in the key (it is the layout box, and a narrower box changes the
    // reported width once the label would have to break), which means a resize that
    // genuinely varies the offered width still misses — correctly. The win is the
    // common case: a fixed-width or Auto-in-a-fixed-parent control measured
    // repeatedly at one constraint.
    //
    // Delegates to MeasureLabelSize so there is exactly ONE cache key construction
    // and one fallback in this class. Two copies drifted apart once already (the
    // key claimed one paragraph alignment while the fallback built another), and a
    // key that disagrees with the object it names hands out layouts built to the
    // wrong spec.
    float MeasureLabelWidth(float fontSize, float maxWidth, float boxHeight,
                            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) const {
        UNREFERENCED_PARAMETER(boxHeight);   // never clip tall glyphs — see below
        return MeasureLabelSize(fontSize, maxWidth, DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, weight).w;
    }

    // Width AND height of the label, from the same cached layout MeasureLabelWidth
    // uses. Exists because a control whose label can be taller than its box glyph
    // (ToggleSwitch: a 20 DIP track beside CJK text with descenders) needs the
    // height too, and reading it off a second, separately-built layout would put
    // back exactly the per-frame allocation the cache removed.
    //
    // `textAlign` / `paraAlign` are explicit here: the key must describe the layout
    // that gets built, and a caller measuring text it will later draw centered must
    // not share a cache entry with one that draws it leading-aligned. (Alignment
    // does not change the reported metrics, but a key that lies about how the entry
    // was built is a trap for the next person to read a different field off it.)
    //
    // `weight` IS A REAL MEASUREMENT INPUT, unlike alignment — SemiBold is wider than
    // Normal for the same string, so measuring at the wrong weight reports the wrong
    // width and the label overflows the box the parent allocated. This used to be
    // hard-coded NORMAL, which silently disagreed with every render path that draws
    // through `EffectiveFontWeight(...)`:
    //
    //   * Button::Kind::Accent renders SemiBold (Button.cpp) but measured Normal.
    //   * Any control the user calls SetFontWeight(SEMI_BOLD) on measured Normal.
    //
    // So pass the SAME expression Render passes. This is the third instance of one
    // recurring defect in this class — Measure and Render reading different font
    // state — after ToggleSwitch's font SIZE mismatch (bodySize vs EffectiveFontSize)
    // and Expander's em-size-as-line-height. If you add a label-measuring path, take
    // its typography from the same accessors the drawing code uses.
    SizeDip MeasureLabelSize(float fontSize, float maxWidth,
                             DWRITE_TEXT_ALIGNMENT textAlign,
                             DWRITE_PARAGRAPH_ALIGNMENT paraAlign,
                             DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) const {
        if (text_.empty() || !Dwrite()) return {0.0f, 0.0f};

        const float boxW = maxWidth > 0.0f ? maxWidth : 1000.0f;
        constexpr float kBoxH = 100000.0f;   // unbounded: never clip tall glyphs

        if (ResourceCache* cache = Context().resourceCache) {
            TextLayoutKey key;
            key.text = text_;
            key.fontSize = fontSize;
            key.weight = weight;
            key.textAlign = textAlign;
            key.paraAlign = paraAlign;
            key.wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
            key.maxWidth = boxW;
            key.maxHeight = kBoxH;
            ComPtr<IDWriteTextLayout> cached = cache->GetTextLayout(std::move(key));
            if (!cached) return {0.0f, 0.0f};
            DWRITE_TEXT_METRICS cm{};
            if (FAILED(cached->GetMetrics(&cm))) return {0.0f, 0.0f};
            return {cm.widthIncludingTrailingWhitespace, cm.height};
        }

        IDWriteTextFormat* fmt = Dwrite()->Format(
            fontSize, weight, textAlign, paraAlign,
            DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!fmt) return {0.0f, 0.0f};
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(Dwrite()->Factory()->CreateTextLayout(
                text_.c_str(), static_cast<UINT32>(text_.size()), fmt,
                boxW, kBoxH, layout.GetAddressOf())))
            return {0.0f, 0.0f};
        DWRITE_TEXT_METRICS m{};
        if (FAILED(layout->GetMetrics(&m))) return {0.0f, 0.0f};
        return {m.widthIncludingTrailingWhitespace, m.height};
    }

    std::wstring text_;
};

} // namespace fluent
