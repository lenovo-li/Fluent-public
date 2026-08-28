// StatusBar.cpp
#include "StatusBar.h"
#include "../controls/TextBlock.h"
#include "../styling/ThemeTokens.h"
#include "../graphics/DrawingContext.h"
#include <algorithm>

namespace fluent {

namespace {
constexpr float kHeight = 32.0f;
constexpr float kPadX = 12.0f;
}  // namespace

StatusBar::StatusBar() {
    SetHeight(kHeight);
    SetVAlign(VAlign::Bottom);

    // Left text block.
    auto* text = Emplace<TextBlock>();
    text->SetVAlign(VAlign::Center);
    text->SetHAlign(HAlign::Left);
    text->SetMargin(Thickness(kPadX, 0.0f, 0.0f, 0.0f));
    text_ = text;
}

void StatusBar::SetText(std::wstring text) {
    textStr_ = text;
    if (text_) text_->SetText(std::move(text));
}

void StatusBar::SetRightContent(std::unique_ptr<FrameworkElement> content) {
    if (rightContent_) {
        // Find and remove the old content from the panel's child list so it is
        // destroyed (not just detached). Panel has no Remove API, so we scan
        // children_ directly.
        for (auto it = children_.begin(); it != children_.end(); ++it) {
            if (it->get() == rightContent_) {
                if ((*it)->IsAttached()) (*it)->DetachFromContext();
                (*it)->SetParent(nullptr);
                children_.erase(it);
                break;
            }
        }
        rightContent_ = nullptr;
    }
    if (content) {
        rightContent_ = content.get();
        Add(std::move(content));
    }
    InvalidateMeasure();
}

SizeDip StatusBar::MeasureOverride(float availW, float availH) {
    UNREFERENCED_PARAMETER(availH);
    float textW = 0.0f;
    if (text_) {
        text_->MeasureCached(availW, kHeight);
        textW = text_->Desired().w;
    }
    float rightW = 0.0f;
    if (rightContent_) {
        rightContent_->MeasureCached(availW, kHeight);
        rightW = rightContent_->Desired().w;
    }
    return SizeDip{std::max(textW + rightW + kPadX * 3.0f, availW), kHeight};
}

void StatusBar::ArrangeOverride(const RectDip& content) {
    // Left text: takes the remaining space after the right content.
    float rightW = 0.0f;
    if (rightContent_) rightW = rightContent_->Desired().w;

    if (text_) {
        const float textW = std::max(0.0f, content.w - rightW - kPadX * 3.0f);
        // Center using the text's OWN desired height, not the full strip height:
        // TextBlock draws its glyphs top-aligned within its bounds, so handing it
        // the full kHeight left the single line of text sitting at the top of the
        // strip instead of centered in it.
        const float th = text_->Desired().h;
        const float ty = content.y + (content.h - th) * 0.5f;
        text_->Arrange(RectDip{content.x, ty, textW, th});
    }

    if (rightContent_) {
        const float x = content.x + content.w - rightW - kPadX;
        const float rh = rightContent_->Desired().h;
        const float ry = content.y + (content.h - rh) * 0.5f;
        rightContent_->Arrange(RectDip{x, ry, rightW, rh});
    }
}

void StatusBar::OnRenderBackground(const DrawingContext& dc) {
    const ColorTokens& pal = Theme().colors;
    const SpacingTokens& spacing = Theme().spacing;

    // StatusBar uses a darker, more prominent fill than cardFill (which is
    // semi-transparent and meant to sit over Mica). In light theme cardFill reads
    // as too bright for a status strip; use controlFillDefault (opaque, darker).
    // Round the top corners to visually separate from content above — the bottom
    // edge is flush with the window and stays square. Win11 status strips use the
    // small radius, not the card radius.
    const float radius = spacing.cornerRadiusSmall;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.bottom()),
        radius, radius);
    dc.FillRoundedRect(rr, pal.controlFillDefault);

    // Subtle 1px top separator for depth.
    dc.FillRect(D2D1::RectF(bounds_.x, bounds_.y, bounds_.right(), bounds_.y + 1.0f),
                pal.controlStrokeDefault);
}

} // namespace fluent
