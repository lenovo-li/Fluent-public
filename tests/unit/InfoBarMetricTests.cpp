// InfoBarMetricTests.cpp — the two small display controls, plus the theme tokens they
// depend on.
//
// The interesting assertions here are not "does it draw" but the two SEMANTIC contracts
// that a wrong implementation gets wrong silently:
//
//   1. SEVERITY -> COLOUR. Each severity must resolve to its own token pair, in both light
//      and dark mode, and the dark values must not be the light ones (a dark-mode success
//      stroke of 0x0E700E on a 0x202020 surface is invisible). These tokens exist
//      precisely so a control never hard-codes a literal, so the test asserts against the
//      theme rather than against numbers.
//
//   2. TREND -> COLOUR, WITH POLARITY. Whether "up" is painted as good is a property of
//      the QUANTITY, not of the number: up is good for revenue and bad for error rate. No
//      inspection of the value can decide it, so Metric takes SetInverted and this pins
//      that inversion actually swaps the pair — including that Flat is unaffected by it.

#include "../framework/Test.h"
#include "../../FluentUI/controls/InfoBar.h"
#include "../../FluentUI/controls/Metric.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <cstdio>

using namespace fluent;

namespace {

class MockHost : public WindowServices {
public:
    HINSTANCE Instance() const override { return nullptr; }
    HWND Hwnd() const override { return nullptr; }
    float DpiScale() const override { return 1.0f; }
    D2DContext& D2D() override { return d2d_; }
    DWriteContext& DWrite() override { return dwrite_; }
    ICompositionBackend* Composition() override { return nullptr; }
    Subscription RegisterActivePopupDismiss(
        std::function<bool(PopupDismissReason, HWND, int, int)>) override { return {}; }
    Subscription RegisterActivePopupKeyHandler(
        std::function<bool(UINT)>) override { return {}; }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

const ThemeSnapshot& LightTheme() {
    static const ThemeSnapshot t = BuildSnapshot(ThemeInputs{}, 0);
    return t;
}

const ThemeSnapshot& DarkTheme() {
    static const ThemeSnapshot t = [] {
        ThemeInputs in;
        in.dark = true;
        return BuildSnapshot(in, 0);
    }();
    return t;
}

UIContext MakeCtx(MockHost& host, const ThemeSnapshot& theme) {
    UIContext c;
    c.window = &host;
    c.theme = &theme;
    // ctx.dwrite must be set explicitly: Control::Dwrite() reads it, NOT
    // WindowServices::DWrite(). Leaving it null sends every text-measuring control down
    // its estimate path, which ignores the offered width — the first version of the
    // wrapping test then saw identical heights at 600 and 200 DIP and looked like a
    // control bug.
    c.dwrite = &host.DWrite();
    c.dpiScale = 1.0f;
    return c;
}

bool SameColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

}  // namespace

// --- Severity tokens ----------------------------------------------------------

// The four severities must be four DISTINCT colours. If two collided, an error and a
// warning would be indistinguishable, which defeats the point of having severities.
TEST(SeverityTokens, FourSeveritiesHaveFourDistinctStrokes) {
    const ColorTokens& c = LightTheme().colors;
    const D2D1_COLOR_F strokes[] = {c.severityInfoStroke, c.severitySuccessStroke,
                                    c.severityWarningStroke, c.severityErrorStroke};
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            EXPECT_FALSE(SameColor(strokes[i], strokes[j]));
}

// Dark mode must supply its OWN severity values, not reuse the light ones. This is the
// failure these tokens exist to prevent: a control hard-coding a light-mode green is
// invisible on a dark surface, and so is a theme that forgets to override it.
TEST(SeverityTokens, DarkModeSeverityDiffersFromLight) {
    const ColorTokens& l = LightTheme().colors;
    const ColorTokens& d = DarkTheme().colors;
    EXPECT_FALSE(SameColor(l.severitySuccessStroke, d.severitySuccessStroke));
    EXPECT_FALSE(SameColor(l.severityErrorStroke, d.severityErrorStroke));
    EXPECT_FALSE(SameColor(l.severityWarningFill, d.severityWarningFill));
}

// Informational must not be an alias for accent. The accent is user-personalisable (it can
// be set to red), so aliasing would make an info message look like an error on some
// machines.
TEST(SeverityTokens, InformationalIsNotAnAliasForAccent) {
    const ColorTokens& c = LightTheme().colors;
    EXPECT_FALSE(SameColor(c.severityInfoStroke, c.accent));
}

// The data roles must be overridable, because the up/down colour convention is regional
// (red is up in mainland China, down in Europe/North America). An app must be able to swap
// them without editing a control.
TEST(DataTokens, UpDownColorsAreOverridableForRegionalConventions) {
    ThemeInputs in;
    in.colors.dataPositive = D2D1::ColorF(D2D1::ColorF::Green);
    in.colors.dataNegative = D2D1::ColorF(D2D1::ColorF::Red);
    const ThemeSnapshot s = BuildSnapshot(in, 0);

    EXPECT_NEAR(s.colors.dataPositive.g, 0.5f, 0.5f);   // green-ish
    EXPECT_TRUE(s.colors.dataPositive.r < 0.5f);
    EXPECT_TRUE(s.colors.dataNegative.r > 0.5f);
    // And they really changed from the default (which is CJK red-up).
    EXPECT_FALSE(SameColor(s.colors.dataPositive, LightTheme().colors.dataPositive));
}

// Round-tripping a snapshot back through ThemeInputs must preserve the new roles, or a
// settings dialog that reads the current theme and re-applies it would silently reset them
// to the palette default.
TEST(DataTokens, SnapshotToInputsRoundTripsTheNewRoles) {
    ThemeInputs in;
    in.colors.dataPositive = D2D1::ColorF(D2D1::ColorF::Magenta);
    in.colors.gridLine = D2D1::ColorF(D2D1::ColorF::Yellow);
    const ThemeSnapshot first = BuildSnapshot(in, 0);

    ThemeInputs round = ThemeInputsFromSnapshot(first);
    const ThemeSnapshot second = BuildSnapshot(round, 0);

    EXPECT_TRUE(SameColor(first.colors.dataPositive, second.colors.dataPositive));
    EXPECT_TRUE(SameColor(first.colors.gridLine, second.colors.gridLine));
    EXPECT_TRUE(SameColor(first.colors.rowFillAlternate, second.colors.rowFillAlternate));
}

// --- InfoBar ------------------------------------------------------------------

// A non-closable InfoBar must stay out of the tab order: it is read-only text, and tabbing
// through static messages with nothing to activate is noise, not accessibility.
TEST(InfoBar, FocusableOnlyWhenClosable) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    EXPECT_FALSE(bar.IsFocusable());

    bar.SetClosable(true);
    EXPECT_TRUE(bar.IsFocusable());
    bar.SetClosable(false);
    EXPECT_FALSE(bar.IsFocusable());
}

// Height must follow the content: a long message in a narrow bar wraps to more lines and
// the control must report the taller height, or the text is clipped.
TEST(InfoBar, HeightGrowsWithAWrappedMessage) {
    MockHost host;
    if (FAILED(host.DWrite().Initialize())) { std::printf("  [SKIP] no DWrite\n"); return; }
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetMessage(L"This is a deliberately long message that has to wrap onto several "
                   L"lines when the available width is small, which is exactly the case "
                   L"an inline status bar has to survive inside a narrow column.");

    bar.Measure(600.0f, 1000.0f);
    const float wide = bar.Desired().h;
    bar.Measure(200.0f, 1000.0f);
    const float narrow = bar.Desired().h;

    std::printf("  wrapped height: 600 DIP -> %.1f, 200 DIP -> %.1f\n", wide, narrow);
    EXPECT_TRUE(narrow > wide);
}

// A title adds a line. Without it, no vertical space is reserved for one — a one-line
// notice must not be padded as though a title were there.
TEST(InfoBar, TitleAddsHeightAndItsAbsenceCostsNothing) {
    MockHost host;
    if (FAILED(host.DWrite().Initialize())) return;
    InfoBar withTitle, without;
    withTitle.AttachToContext(MakeCtx(host, LightTheme()));
    without.AttachToContext(MakeCtx(host, LightTheme()));

    withTitle.SetTitle(L"Heads up");
    withTitle.SetMessage(L"Something happened.");
    without.SetMessage(L"Something happened.");

    withTitle.Measure(400.0f, 1000.0f);
    without.Measure(400.0f, 1000.0f);
    std::printf("  with title %.1f, without %.1f\n",
                withTitle.Desired().h, without.Desired().h);
    EXPECT_TRUE(withTitle.Desired().h > without.Desired().h);
}

// The close button must sit inside the bar and near the top. Top-aligned rather than
// centred so it stays next to the title on a tall multi-line bar.
TEST(InfoBar, CloseButtonIsInsideTheBarAndNearTheTop) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetClosable(true);
    bar.Arrange(RectDip{0.0f, 0.0f, 400.0f, 120.0f});

    const RectDip cb = bar.CloseButtonRect();
    EXPECT_TRUE(cb.w > 0.0f && cb.h > 0.0f);
    EXPECT_TRUE(cb.right() <= 400.0f);
    EXPECT_TRUE(cb.y >= 0.0f);
    // Near the top, not the vertical middle of a 120 DIP bar.
    EXPECT_TRUE(cb.bottom() < 60.0f);

    // A non-closable bar reports an empty rect rather than a phantom hit region.
    bar.SetClosable(false);
    EXPECT_NEAR(bar.CloseButtonRect().w, 0.0f, 0.01f);
}

// Closing is the APP's decision: the control reports the click and does not hide itself.
// A control that vanished would leave a hole its parent still sizes.
TEST(InfoBar, ClosedEventFiresAndTheBarDoesNotHideItself) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetClosable(true);
    bar.Arrange(RectDip{0.0f, 0.0f, 400.0f, 60.0f});

    int fired = 0;
    // Keep the Subscription alive: it is RAII and discarding it unsubscribes at once.
    Subscription sub = bar.Closed().Subscribe(&fired,
        [](void* o, InfoBar&, RoutedEventArgs&) { ++*static_cast<int*>(o); });

    const RectDip cb = bar.CloseButtonRect();
    PointerEventArgs e{};
    e.position = {cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f};
    bar.OnPointerPressed(e);

    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(e.handled);
    EXPECT_TRUE(bar.IsVisible());     // still there: dismissal is the app's job
}

// A press away from the close button must not fire it, or clicking the message text
// dismisses the bar.
TEST(InfoBar, PressOnTheMessageAreaDoesNotClose) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetClosable(true);
    bar.Arrange(RectDip{0.0f, 0.0f, 400.0f, 60.0f});

    int fired = 0;
    Subscription sub = bar.Closed().Subscribe(&fired,
        [](void* o, InfoBar&, RoutedEventArgs&) { ++*static_cast<int*>(o); });

    PointerEventArgs e{};
    e.position = {40.0f, 30.0f};      // left side, over the text
    bar.OnPointerPressed(e);
    EXPECT_EQ(fired, 0);
}

// Severity is Render-level, not Measure-level: it swaps colours and changes no metric, so
// escalating to Measure would relayout for nothing.
TEST(InfoBar, SeverityChangeDoesNotDirtyMeasure) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetMessage(L"x");
    bar.Measure(400.0f, 100.0f);
    bar.ClearDirtySubtree();

    bar.SetSeverity(InfoBar::Severity::Error);
    EXPECT_FALSE(Has(bar.Dirty(), DirtyFlags::Measure));
    EXPECT_TRUE(Has(bar.Dirty(), DirtyFlags::Render));
}

// Closability IS Measure-level, by contrast: the button takes horizontal space away from
// the message, which changes how it wraps.
TEST(InfoBar, ClosabilityChangeDirtiesMeasure) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetMessage(L"x");
    bar.Measure(400.0f, 100.0f);
    bar.ClearDirtySubtree();

    bar.SetClosable(true);
    EXPECT_TRUE(Has(bar.Dirty(), DirtyFlags::Measure));
}

// The focus ring is stroked outside bounds_, so the overflow must be declared
// unconditionally — the frame where focus LEAVES still has to clear those pixels, and
// IsFocused() is already false by then. Hyperlink and Expander both shipped this bug.
TEST(InfoBar, OverflowIsDeclaredAndNotGatedOnFocus) {
    MockHost host;
    InfoBar bar;
    bar.AttachToContext(MakeCtx(host, LightTheme()));
    bar.SetClosable(true);
    const float unfocused = bar.VisualOverflowDip();
    bar.SetFocused(true);
    const float focused = bar.VisualOverflowDip();

    EXPECT_TRUE(unfocused > 0.0f);
    EXPECT_NEAR(unfocused, focused, 0.001f);
}

// --- Metric -------------------------------------------------------------------

// The core semantic: inversion swaps which token a direction resolves to, so "up is bad"
// quantities (error rate, drawdown) are not painted as gains.
TEST(Metric, InversionSwapsTheTrendColorsButNotFlat) {
    MockHost host;
    Metric m;
    m.AttachToContext(MakeCtx(host, LightTheme()));
    const ColorTokens& c = LightTheme().colors;

    EXPECT_TRUE(SameColor(m.TrendColor(Metric::Trend::Up), c.dataPositive));
    EXPECT_TRUE(SameColor(m.TrendColor(Metric::Trend::Down), c.dataNegative));

    m.SetInverted(true);
    EXPECT_TRUE(SameColor(m.TrendColor(Metric::Trend::Up), c.dataNegative));
    EXPECT_TRUE(SameColor(m.TrendColor(Metric::Trend::Down), c.dataPositive));

    // Flat has no direction, so inversion must not touch it.
    EXPECT_TRUE(SameColor(m.TrendColor(Metric::Trend::Flat), c.dataNeutral));
}

// Trend::None hides the delta line and reclaims its space; a metric with no change to show
// must not reserve a blank row.
TEST(Metric, TrendNoneReclaimsTheDeltaLine) {
    MockHost host;
    if (FAILED(host.DWrite().Initialize())) return;
    Metric with, without;
    with.AttachToContext(MakeCtx(host, LightTheme()));
    without.AttachToContext(MakeCtx(host, LightTheme()));

    with.SetLabel(L"Revenue");
    with.SetValue(L"1,234");
    with.SetDelta(L"+5.2%", Metric::Trend::Up);

    without.SetLabel(L"Revenue");
    without.SetValue(L"1,234");
    without.SetDelta(L"+5.2%", Metric::Trend::None);   // text set but no trend

    with.Measure(200.0f, 200.0f);
    without.Measure(200.0f, 200.0f);
    std::printf("  with delta %.1f, without %.1f\n",
                with.Desired().h, without.Desired().h);
    EXPECT_TRUE(with.Desired().h > without.Desired().h);
}

// Switching only the trend (same text, both non-None) is Render-level: same box, different
// colour. Escalating to Measure would relayout a dashboard on every tick of live data.
TEST(Metric, TrendOnlyChangeDoesNotDirtyMeasure) {
    MockHost host;
    Metric m;
    m.AttachToContext(MakeCtx(host, LightTheme()));
    m.SetValue(L"10");
    m.SetDelta(L"+1", Metric::Trend::Up);
    m.Measure(200.0f, 200.0f);
    m.ClearDirtySubtree();

    m.SetDelta(L"+1", Metric::Trend::Down);   // same text, opposite direction
    EXPECT_FALSE(Has(m.Dirty(), DirtyFlags::Measure));
    EXPECT_TRUE(Has(m.Dirty(), DirtyFlags::Render));
}

// ...but appearing or vanishing IS Measure-level, because it adds or removes a line.
TEST(Metric, DeltaAppearingDirtiesMeasure) {
    MockHost host;
    Metric m;
    m.AttachToContext(MakeCtx(host, LightTheme()));
    m.SetValue(L"10");
    m.Measure(200.0f, 200.0f);
    m.ClearDirtySubtree();

    m.SetDelta(L"+1", Metric::Trend::Up);     // None -> Up: a line appears
    EXPECT_TRUE(Has(m.Dirty(), DirtyFlags::Measure));
}

// Inversion is Render-level: it only decides which colour a direction maps to.
TEST(Metric, InversionDoesNotDirtyMeasure) {
    MockHost host;
    Metric m;
    m.AttachToContext(MakeCtx(host, LightTheme()));
    m.SetValue(L"10");
    m.SetDelta(L"-2", Metric::Trend::Down);
    m.Measure(200.0f, 200.0f);
    m.ClearDirtySubtree();

    m.SetInverted(true);
    EXPECT_FALSE(Has(m.Dirty(), DirtyFlags::Measure));
}
