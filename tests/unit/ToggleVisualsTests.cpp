// ToggleVisualsTests.cpp — 阶段 4 交叉发现 #4：CheckBox/RadioButton/ToggleSwitch 颜色逻辑测试
//
// 动机见 ToggleVisuals.h。三个控件（CheckBox, RadioButton, ToggleSwitch）都是两态开关，
// 解析颜色的逻辑字节一致（checked → accent, unchecked → controlFill, hover 提升，
// 覆盖优先于主题），只有几何不同（圆角矩/圆/滑轨）。共享一套纯函数意味着共享一套测试
// 即可验证三者，而不是每个控件写一遍断言。

#include "../framework/Test.h"
#include "../../FluentUI/controls/ToggleVisuals.h"
#include <cmath>
#include <cstdio>

using namespace fluent;

namespace {

ColorTokens MakeTokens() {
    ColorTokens t{};
    t.accent        = D2D1::ColorF(0.22f, 0.43f, 0.76f, 1.0f);
    t.accentHover   = D2D1::ColorF(0.18f, 0.36f, 0.66f, 1.0f);
    t.accentPressed = D2D1::ColorF(0.14f, 0.28f, 0.54f, 1.0f);
    t.onAccent      = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    t.controlFillDefault = D2D1::ColorF(0.95f, 0.95f, 0.97f, 0.7f);
    t.controlFillHover   = D2D1::ColorF(0.96f, 0.96f, 0.98f, 0.8f);
    return t;
}

bool ColorEq(D2D1_COLOR_F a, D2D1_COLOR_F b, float eps = 0.01f) {
    return std::fabs(a.r - b.r) < eps && std::fabs(a.g - b.g) < eps &&
           std::fabs(a.b - b.b) < eps && std::fabs(a.a - b.a) < eps;
}

} // namespace

// --- Checked fill 覆盖测试（accent 覆盖必须在所有状态生效） ---------------

TEST(ToggleVisuals, CheckedFill_ExplicitAccentAtRestAndFadedWhenDisabled) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandAccent = D2D1::ColorF(0.0f, 0.8f, 0.4f, 1.0f);
    ToggleAppearance app{};
    app.accent = brandAccent;

    // 静止态：调用者给的精确 accent 原样使用。
    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Normal, app, c), brandAccent));

    // Disabled 按 Button 的既有规则淡化（保留色相、alpha ×0.4），不原样返回。
    // 这条原本被断言成「原样」，那正是禁用态的 ToggleSwitch 看起来和启用态一模一样
    // 的原因 —— 测试把 bug 钉住了。语义区别值得说清：accent 是控件用来派生各状态
    // 的语义角色，所以禁用时淡化；background 是字面值，禁用时原样保留。
    D2D1_COLOR_F disabled = ToggleCheckedFill(VisualState::Disabled, app, c);
    EXPECT_TRUE(std::fabs(disabled.r - brandAccent.r) < 0.01f);   // 色相保留
    EXPECT_TRUE(std::fabs(disabled.a - 0.4f) < 0.01f);            // 但淡化
}

// Hover/Pressed 从 accent 覆盖【派生】而非原样返回 —— 否则自定义颜色的勾选框对鼠标
// 毫无反应。旧注释声称「没有办法表达 hover 变体所以只能原样返回」，那个限制现在没了：
// Control 上有独立的 hover/pressed 槽位。
TEST(ToggleVisuals, CheckedFill_DerivesHoverAndPressedFromExplicitAccent) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandAccent = D2D1::ColorF(0.0f, 0.8f, 0.4f, 1.0f);
    ToggleAppearance app{};
    app.accent = brandAccent;

    D2D1_COLOR_F hover   = ToggleCheckedFill(VisualState::Hover, app, c);
    D2D1_COLOR_F pressed = ToggleCheckedFill(VisualState::Pressed, app, c);
    EXPECT_FALSE(ColorEq(hover, brandAccent));
    EXPECT_FALSE(ColorEq(pressed, brandAccent));
    EXPECT_FALSE(ColorEq(pressed, hover));
}

// 显式设置的状态色优先于派生，且和 Button 走同一条规则。
TEST(ToggleVisuals, CheckedFill_ExplicitStateColorsBeatDerivation) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F myHover = D2D1::ColorF(0.1f, 0.7f, 1.0f, 1.0f);
    D2D1_COLOR_F myPress = D2D1::ColorF(0.0f, 0.2f, 0.3f, 1.0f);
    ToggleAppearance app{};
    app.accent = D2D1::ColorF(0.0f, 0.8f, 0.4f, 1.0f);
    app.accentHover = myHover;
    app.accentPressed = myPress;

    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Hover, app, c), myHover));
    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Pressed, app, c), myPress));

    // 没有基础覆盖时也能单独设置状态色（基色继续跟随主题）。
    ToggleAppearance onlyStates{};
    onlyStates.accentHover = myHover;
    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Hover, onlyStates, c), myHover));
    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Normal, onlyStates, c), c.accent));
}

// --- Checked fill 默认路径（无覆盖时，Normal 用 accent, Hover/Pressed 用 accentHover） --

// 无覆盖时走主题的三档 ramp。Pressed 有自己的 token（accentPressed），不与 Hover 合并
// —— 这是 CheckBox/RadioButton 迁移前的行为，提取纯函数时原样保留。
TEST(ToggleVisuals, CheckedFill_NoOverride_UsesThemeRamp) {
    ColorTokens c = MakeTokens();
    ToggleAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Normal, noOverride, c), c.accent));
    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Hover, noOverride, c), c.accentHover));
    EXPECT_TRUE(ColorEq(ToggleCheckedFill(VisualState::Pressed, noOverride, c), c.accentPressed));
}

// --- Unchecked fill 覆盖测试（background 覆盖必须在所有状态生效） ---------

TEST(ToggleVisuals, UncheckedFill_ExplicitBackgroundWinsInAllStates) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandBg = D2D1::ColorF(1.0f, 0.5f, 0.0f, 1.0f);
    ToggleAppearance app{brandBg, std::nullopt, std::nullopt};

    // 明确背景色在所有状态生效 — hover/pressed 也用它
    for (auto state : {VisualState::Normal, VisualState::Hover,
                       VisualState::Pressed, VisualState::Disabled}) {
        D2D1_COLOR_F got = ToggleUncheckedFill(state, app, c);
        EXPECT_TRUE(ColorEq(got, brandBg));
    }
}

// --- Unchecked fill 默认路径（Normal → controlFillDefault, Hover/Pressed → controlFillHover） --

TEST(ToggleVisuals, UncheckedFill_NoOverride_UsesControlFill) {
    ColorTokens c = MakeTokens();
    ToggleAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    EXPECT_TRUE(ColorEq(ToggleUncheckedFill(VisualState::Normal, noOverride, c), c.controlFillDefault));
    EXPECT_TRUE(ColorEq(ToggleUncheckedFill(VisualState::Hover, noOverride, c), c.controlFillHover));
    EXPECT_TRUE(ColorEq(ToggleUncheckedFill(VisualState::Pressed, noOverride, c), c.controlFillHover));
}

// --- Mark color 覆盖测试 -------------------------------------------------

TEST(ToggleVisuals, MarkColor_ExplicitForegroundWins) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandFg = D2D1::ColorF(0.9f, 0.1f, 0.1f, 1.0f);
    ToggleAppearance app{std::nullopt, std::nullopt, brandFg};

    EXPECT_TRUE(ColorEq(ToggleMarkColor(VisualState::Normal, app, c), brandFg));
}

TEST(ToggleVisuals, MarkColor_NoOverride_FallsBackToOnAccent) {
    ColorTokens c = MakeTokens();
    ToggleAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    EXPECT_TRUE(ColorEq(ToggleMarkColor(VisualState::Normal, noOverride, c), c.onAccent));
}

// The mark has to fade with the rest of the control. This is the assertion that
// would have caught the original bug: a disabled CheckBox drew a full-strength
// white tick on a 0.4-alpha box, so half the control looked disabled and half
// looked live -- which reads as a rendering glitch, not as "you can't click this".
TEST(ToggleVisuals, MarkColor_Disabled_FadesThemedMark) {
    ColorTokens c = MakeTokens();
    ToggleAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    D2D1_COLOR_F live = ToggleMarkColor(VisualState::Normal, noOverride, c);
    D2D1_COLOR_F dead = ToggleMarkColor(VisualState::Disabled, noOverride, c);

    // Fainter than the live mark. (Framework has EXPECT_TRUE/EQ/NE/NEAR only.)
    EXPECT_TRUE(dead.a < live.a);
    // Hue is preserved -- fading is an alpha operation, not a desaturation.
    EXPECT_NEAR(dead.r, live.r, 0.001f);
    EXPECT_NEAR(dead.g, live.g, 0.001f);
    EXPECT_NEAR(dead.b, live.b, 0.001f);
}

// ...but an explicit Foreground survives Disabled unfaded, matching Button's rule
// that an exact colour the caller supplied is not the framework's to alter. The
// fill underneath still fades, so the control reads as inert either way.
TEST(ToggleVisuals, MarkColor_Disabled_KeepsExplicitForegroundExact) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandFg = D2D1::ColorF(0.9f, 0.1f, 0.1f, 1.0f);
    ToggleAppearance app{std::nullopt, std::nullopt, brandFg};

    EXPECT_TRUE(ColorEq(ToggleMarkColor(VisualState::Disabled, app, c), brandFg));
}

// --- ToggleIsHovered 谓词测试（确保三个控件判 hover 的表达式一致） ----------

TEST(ToggleVisuals, ToggleIsHovered_TrueForHoverAndPressed) {
    EXPECT_TRUE(ToggleIsHovered(VisualState::Hover));
    EXPECT_TRUE(ToggleIsHovered(VisualState::Pressed));
    EXPECT_FALSE(ToggleIsHovered(VisualState::Normal));
    EXPECT_FALSE(ToggleIsHovered(VisualState::Disabled));
}


// --- Disabled must be visibly disabled ---------------------------------------
//
// THE BUG. VisualState::Disabled fell through to `default:` in ToggleCheckedFill and was
// not considered at all by ToggleUncheckedFill or ToggleMarkColor, so a disabled CheckBox
// / RadioButton / ToggleSwitch painted with the full accent and looked exactly like an
// enabled one. Spotted in a screenshot: a demo row labelled "Disabled (On)" rendered as a
// solid blue switch identical to its live neighbour.
//
// Button already had the convention (WithAlpha(fill, 0.4) for fills, textSecondary for the
// glyph); the toggle family simply never implemented it. These assertions are phrased as
// "faded relative to the enabled colour" plus the specific 0.4 alpha, so an implementation
// that merely returns *something* different cannot satisfy them.

TEST(ToggleVisuals, DisabledCheckedFillIsFadedNotFullStrength) {
    ColorTokens c = MakeTokens();
    ToggleAppearance app{std::nullopt, std::nullopt, std::nullopt};

    D2D1_COLOR_F on  = ToggleCheckedFill(VisualState::Normal, app, c);
    D2D1_COLOR_F off = ToggleCheckedFill(VisualState::Disabled, app, c);

    std::printf("  checked fill alpha: enabled %.2f -> disabled %.2f\n", on.a, off.a);
    EXPECT_TRUE(off.a < on.a);
    EXPECT_TRUE(std::fabs(off.a - 0.4f) < 0.01f);   // same 0.4 convention as Button
}

// An explicit AccentColor must still be honoured when disabled -- faded, not discarded.
// A control themed red stays recognisably red while greyed out, which is the rule Button
// documents: "an explicit background wins in EVERY state, Disabled included".
TEST(ToggleVisuals, DisabledFadesAnExplicitAccentRatherThanDroppingIt) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brand = D2D1::ColorF(0.77f, 0.17f, 0.11f, 1.0f);
    ToggleAppearance app{std::nullopt, brand, std::nullopt};

    D2D1_COLOR_F out = ToggleCheckedFill(VisualState::Disabled, app, c);

    EXPECT_TRUE(std::fabs(out.r - brand.r) < 0.01f);   // hue preserved
    EXPECT_TRUE(std::fabs(out.g - brand.g) < 0.01f);
    EXPECT_TRUE(std::fabs(out.b - brand.b) < 0.01f);
    EXPECT_TRUE(std::fabs(out.a - 0.4f) < 0.01f);      // but faded
}

TEST(ToggleVisuals, DisabledUncheckedFillIsFadedToo) {
    ColorTokens c = MakeTokens();
    ToggleAppearance app{std::nullopt, std::nullopt, std::nullopt};

    D2D1_COLOR_F on  = ToggleUncheckedFill(VisualState::Normal, app, c);
    D2D1_COLOR_F off = ToggleUncheckedFill(VisualState::Disabled, app, c);

    std::printf("  unchecked fill alpha: enabled %.2f -> disabled %.2f\n", on.a, off.a);
    EXPECT_TRUE(off.a < on.a);
}

// Hover must not lift a disabled control: pointing at something inert should not suggest
// it can be pressed. Disabled has to win over Hover.
TEST(ToggleVisuals, DisabledWinsOverHover) {
    ColorTokens c = MakeTokens();
    ToggleAppearance app{std::nullopt, std::nullopt, std::nullopt};

    D2D1_COLOR_F hovered  = ToggleUncheckedFill(VisualState::Hover, app, c);
    D2D1_COLOR_F disabled = ToggleUncheckedFill(VisualState::Disabled, app, c);
    EXPECT_TRUE(disabled.a < hovered.a);
}
