// Metric.h — a single labelled number with an optional signed delta ("KPI tile").
//
// The shape every dashboard needs: a caption, a large value, and a change indicator that
// is coloured by direction. Streamlit calls it st.metric; WinUI has no equivalent, so
// without this control an app builds it from three TextBlocks in a StackPanel and then
// hard-codes the up/down colours — which is the part that goes wrong, because the
// direction-to-colour convention is regional (red is UP in mainland China / Japan /
// Korea, DOWN in Europe and North America). This reads ColorTokens dataPositive /
// dataNegative / dataNeutral so the app decides once, in the theme.
//
// DELTA SIGN vs DELTA COLOUR are separate concerns and this is the crux of the control:
//   * the sign shown comes from the delta VALUE (or the caller's own string), and
//   * the colour comes from a SEPARATE polarity question: is "up" good here?
// For a stock price, up is normally shown as gain. For a metric like "error rate" or
// "debt ratio", up is bad and must not be painted with the gain colour. SetInverted()
// exposes that, because no amount of inspecting the number can tell the control which
// kind of quantity it is.
//
// TEXT, NOT NUMBERS, on the wire. SetValue takes a string rather than a double: this
// control must not own formatting decisions (currency, thousands separators, percent,
// significant digits, locale) that the app already has to make consistently elsewhere.
// A control that formatted numbers itself would disagree with the app's own tables.

#pragma once
#include "../core/Control.h"
#include <string>

namespace fluent {

class Metric : public Control {
public:
    // How to colour a delta. Explicit rather than inferred from the string so the caller
    // cannot be surprised: parsing "+1.2%" to guess a direction would break on "1.2pp",
    // "▲1.2" or a localised minus sign.
    enum class Trend {
        None,      // no delta shown
        Up,        // delta is an increase
        Down,      // delta is a decrease
        Flat,      // explicitly unchanged (dataNeutral, not "no delta")
    };

    Metric();

    // The caption above the value.
    void SetLabel(std::wstring text);
    const std::wstring& Label() const { return label_; }

    // The headline, pre-formatted by the caller.
    void SetValue(std::wstring text);
    const std::wstring& Value() const { return value_; }

    // The change line, pre-formatted, plus its direction. Passing Trend::None hides the
    // line entirely and reclaims its vertical space.
    void SetDelta(std::wstring text, Trend trend);
    const std::wstring& Delta() const { return delta_; }
    Trend DeltaTrend() const { return trend_; }

    // When true, Up is painted with the NEGATIVE colour and Down with the positive one.
    // For "up is bad" quantities: error rate, drawdown, debt ratio, latency.
    void SetInverted(bool inverted);
    bool IsInverted() const { return inverted_; }

    // The colour a given trend resolves to under the current theme and inversion.
    // Public because it is the whole semantic contract of the control, and a test that
    // recomputed it would be asserting its own copy of the logic rather than the
    // control's.
    D2D1_COLOR_F TrendColor(Trend t) const;

    // --- Element contract -------------------------------------------------
    void Measure(float availW, float availH) override;
    void Render(const DrawingContext& dc) override;

private:
    std::wstring label_;
    std::wstring value_;
    std::wstring delta_;
    Trend trend_ = Trend::None;
    bool inverted_ = false;
};

}  // namespace fluent
