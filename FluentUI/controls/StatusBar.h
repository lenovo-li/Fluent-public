// StatusBar.h — bottom-of-window status strip.
//
// A StatusBar sits at the bottom of a window and shows a left-aligned text
// message plus optional right-aligned content (a progress bar, a spinner, a
// button). It is a thin container: 32 DIP tall, cardFill background, a 1px
// top separator, and a two-column Grid layout (left text star, right content
// auto).
#pragma once

#include "../layout/Panel.h"
#include <memory>
#include <string>

namespace fluent {

class TextBlock;

class StatusBar : public Panel {
public:
    StatusBar();

    // Left-aligned status text. Empty hides the text block.
    void SetText(std::wstring text);
    const std::wstring& Text() const { return textStr_; }

    // Optional right-aligned content (a ProgressBar, a Button, etc.). Owned by
    // the StatusBar. Pass nullptr to clear.
    void SetRightContent(std::unique_ptr<FrameworkElement> content);

protected:
    SizeDip MeasureOverride(float availW, float availH) override;
    void ArrangeOverride(const RectDip& content) override;
    void OnRenderBackground(const DrawingContext& dc) override;

private:
    TextBlock* text_ = nullptr;               // borrowed (owned by Panel base)
    FrameworkElement* rightContent_ = nullptr;  // borrowed (owned by Panel base)
    std::wstring textStr_;                    // current status text
};

} // namespace fluent
