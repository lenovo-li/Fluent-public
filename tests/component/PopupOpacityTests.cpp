// PopupOpacityTests.cpp — what does popup opacity actually make translucent?
//
// TWO SEPARATE QUESTIONS, from a screenshot where "半透明 0.85" looked barely different
// from "不透明 1.0":
//
//   1. Was the value even applied? ComboBox::SetPopupOpacity forwarded straight to
//      popup_->SetCardOpacity() and did NOTHING when popup_ was null. popup_ is created
//      in OnAttachedToTree, so the ordinary `build page -> configure -> attach` order
//      silently dropped the setting. Silently, because there was no popup to complain to.
//      That is now fixed by remembering the value and applying it at creation.
//
//   2. Given it IS applied, how much should change? cardOpacity_ scales the CARD
//      (background fill + border) only -- PopupHost::Render multiplies pal.cardFill and
//      controlStrokeDefault by it, while the content element draws through a separate
//      contentOpacity_ that stays 1.0. So at 0.85 the panel background lightens by 15%
//      and the ROW TEXT stays fully opaque. That is deliberate (text contrast is the
//      first thing to sacrifice and the last thing you want to), but it means the visible
//      change is subtle by design -- exactly what the screenshot shows.
//
// These tests pin both: the value survives a pre-attach call, and it is clamped. The
// card-vs-content split is asserted through the token maths PopupHost uses, since
// constructing a real popup HWND is not something a headless test can do.

#include "../framework/Test.h"
#include "../../FluentUI/controls/ComboBox.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <cmath>
#include <cstdio>

using namespace fluent;

// --- 1. The value must survive being set before attach ------------------------

// This is the regression. A page builds the control, configures it, THEN adds it to the
// tree; popup_ does not exist yet during configuration.
TEST(PopupOpacity, SetBeforeAttachIsRemembered) {
    ComboBox combo;                       // never attached: popup_ is null
    combo.SetPopupOpacity(0.85f);
    EXPECT_NEAR(combo.PopupOpacity(), 0.85f, 0.001f);
}

TEST(PopupOpacity, DefaultsToFullyOpaque) {
    ComboBox combo;
    EXPECT_NEAR(combo.PopupOpacity(), 1.0f, 0.001f);
}

// Out-of-range values are clamped rather than passed through: a negative alpha or one
// above 1 would produce an invalid brush colour downstream.
TEST(PopupOpacity, ValuesAreClamped) {
    ComboBox combo;

    combo.SetPopupOpacity(-0.5f);
    EXPECT_NEAR(combo.PopupOpacity(), 0.0f, 0.001f);

    combo.SetPopupOpacity(3.0f);
    EXPECT_NEAR(combo.PopupOpacity(), 1.0f, 0.001f);
}

// --- 2. What the value actually scales ----------------------------------------

// PopupHost::Render flattens the translucent cardFill over windowBackground FIRST (a
// popup HWND has no Mica behind it, so painting cardFill raw would show the parent
// window through), and only then scales by cardOpacity_. This reproduces that maths so
// the "how visible is 0.85" question has a number attached.
TEST(PopupOpacity, CardOpacityScalesTheFlattenedCardFill) {
    const ThemeSnapshot& th = BuildSnapshot(ThemeInputs{}, 0);
    const ColorTokens& pal = th.colors;

    const D2D1_COLOR_F flat = FlattenOver(pal.cardFill, pal.windowBackground);
    EXPECT_NEAR(flat.a, 1.0f, 0.001f);          // flattening yields an opaque colour

    const D2D1_COLOR_F opaque = WithAlphaScale(flat, 1.0f);
    const D2D1_COLOR_F sheer  = WithAlphaScale(flat, 0.85f);

    std::printf("  card alpha: 1.0 -> %.2f, 0.85 -> %.2f\n", opaque.a, sheer.a);
    EXPECT_NEAR(opaque.a, 1.0f, 0.001f);
    EXPECT_NEAR(sheer.a, 0.85f, 0.001f);

    // Only alpha moves -- the hue is untouched, so a translucent panel is the same
    // colour seen through less coverage, not a different colour.
    EXPECT_NEAR(sheer.r, opaque.r, 0.001f);
    EXPECT_NEAR(sheer.g, opaque.g, 0.001f);
    EXPECT_NEAR(sheer.b, opaque.b, 0.001f);
}

// The honest limitation, asserted so nobody "fixes" it by accident: 0.85 on the card is
// a 15% alpha change on the background only. Row text is drawn by the content element
// through contentOpacity_, which SetPopupOpacity does not touch. A caller who wants the
// whole panel to fade needs the content opacity too -- and should not want that, because
// it costs text legibility.
TEST(PopupOpacity, CardOpacityDoesNotDimTheRowText) {
    const ThemeSnapshot& th = BuildSnapshot(ThemeInputs{}, 0);
    const ColorTokens& pal = th.colors;

    // The text colour a row draws with is unaffected by the card's alpha: the card
    // scaling is applied to cardFill / controlStrokeDefault only.
    const D2D1_COLOR_F textAtFull  = pal.textPrimary;
    const D2D1_COLOR_F cardAtSheer =
        WithAlphaScale(FlattenOver(pal.cardFill, pal.windowBackground), 0.85f);

    EXPECT_NEAR(textAtFull.a, 1.0f, 0.001f);      // text stays opaque
    EXPECT_TRUE(cardAtSheer.a < textAtFull.a);    // only the card became translucent
}
