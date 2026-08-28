// ControlPropertyTests.cpp —阶段 4 交叉发现 #4：Button 颜色逻辑测试
//
// 动机：Button::Render 的颜色决策分支（种类 × 状态 × 覆盖）需验证，但 Render 本身不
// 可测（需 window/device/dc）。一个录制型 fake DrawingContext 会给每个绘制操作加虚函
// 数派发（框架拒绝），所以把颜色决策提为纯函数（Button.h），Render 调用并绘制结果。
//
// 这个测试套件调用那些纯函数，构造最小的 ButtonAppearance + ColorTokens 实例，验证
// "种类 K 在状态 S 下配覆盖 O 使用颜色 Z"——无需 window 也无需 fake。
//
// 覆盖测试的三个原则（此测试套件示范，所有控件覆盖逻辑都应当类似对待）：
//   1. **明确覆盖必在全部状态生效**，包括 Disabled。否则用户设的品牌色一 hover 就变。
//   2. **Disabled 状态的语义降级是默认机制，不是覆盖的覆盖**。用户说"我要这个蓝"，
//      disabled 还去淡化它是错的——没有地方让用户说"我要蓝，除了 disabled 用灰"。
//   3. **测试必覆盖所有（种类 × 状态）组合** + 明确覆盖 + accent 覆盖回退。缺一个，那个
//      分支在真实使用前就是未验证路径。
//
// 为何不虚拟化 DrawingContext？11 个绘制方法 × 每个控件每帧 → vtable 调度成本拒绝
// （project documentation "small and fast"）。纯函数提取是零成本抽象：调用内联，测试可达。

#include "../framework/Test.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/styling/ThemeTokens.h"
#include <cmath>

using namespace fluent;

namespace {

// 造 ColorTokens 的最小子集（只填 Button 实际读的令牌）
ColorTokens MakeTokens() {
    ColorTokens t{};
    t.accent        = D2D1::ColorF(0.22f, 0.43f, 0.76f, 1.0f);
    t.accentHover   = D2D1::ColorF(0.18f, 0.36f, 0.66f, 1.0f);
    t.accentPressed = D2D1::ColorF(0.14f, 0.28f, 0.54f, 1.0f);
    t.onAccent      = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    t.controlFillDefault = D2D1::ColorF(0.95f, 0.95f, 0.97f, 0.7f);
    t.controlFillHover   = D2D1::ColorF(0.96f, 0.96f, 0.98f, 0.8f);
    t.controlFillPressed = D2D1::ColorF(0.94f, 0.94f, 0.96f, 0.9f);
    t.textPrimary   = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    t.textSecondary = D2D1::ColorF(0.4f, 0.4f, 0.4f, 1.0f);
    return t;
}

bool ColorEq(D2D1_COLOR_F a, D2D1_COLOR_F b, float eps = 0.01f) {
    return std::fabs(a.r - b.r) < eps && std::fabs(a.g - b.g) < eps &&
           std::fabs(a.b - b.b) < eps && std::fabs(a.a - b.a) < eps;
}

} // namespace

// --- Fill 覆盖测试（最高优先级，所有状态都用它） ---------------

TEST(ControlProperty, ButtonFillColor_ExplicitBackgroundWinsAtRestAndWhenDisabled) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brand = D2D1::ColorF(1.0f, 0.5f, 0.0f, 1.0f);
    ButtonAppearance app{};
    app.background = brand;

    // 静止态与禁用态：调用者给的精确色值原样使用，Disabled 也不淡化它
    // （禁用的提示由文字色 textSecondary 承担，见 ButtonTextColor）。
    for (auto kind : {Button::Kind::Standard, Button::Kind::Accent, Button::Kind::Subtle}) {
        for (auto state : {VisualState::Normal, VisualState::Disabled}) {
            D2D1_COLOR_F got = ButtonFillColor(kind, state, app, c);
            EXPECT_TRUE(ColorEq(got, brand));
        }
    }
}

// Hover/Pressed 刻意【不再】等于基色。此前它们原样返回基色，导致自定义颜色的按钮
// 对鼠标毫无反应 —— 悬浮没有变化、按下没有反馈，看起来像不可点击。现在从基色派生
// 出两个状态色（暗色向白偏移、亮色向黑偏移，按亮度决定方向）。
TEST(ControlProperty, ButtonFillColor_DerivesHoverAndPressedFromExplicitBackground) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brand = D2D1::ColorF(1.0f, 0.5f, 0.0f, 1.0f);
    ButtonAppearance app{};
    app.background = brand;

    D2D1_COLOR_F rest    = ButtonFillColor(Button::Kind::Standard, VisualState::Normal,  app, c);
    D2D1_COLOR_F hover   = ButtonFillColor(Button::Kind::Standard, VisualState::Hover,   app, c);
    D2D1_COLOR_F pressed = ButtonFillColor(Button::Kind::Standard, VisualState::Pressed, app, c);

    EXPECT_TRUE(ColorEq(rest, brand));
    EXPECT_FALSE(ColorEq(hover, rest));        // 悬浮必须可见
    EXPECT_FALSE(ColorEq(pressed, rest));      // 按下必须可见
    EXPECT_FALSE(ColorEq(pressed, hover));     // 且两者互不相同
    // 按下比悬浮偏移更多，所以离基色更远。
    auto dist = [](D2D1_COLOR_F a, D2D1_COLOR_F b) {
        return std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b);
    };
    EXPECT_TRUE(dist(pressed, rest) > dist(hover, rest));
}

// 但显式设置的状态色优先于派生 —— 这就是用户要求的「开放让用户设置，没设置就按规则来」。
TEST(ControlProperty, ButtonFillColor_ExplicitStateColorsBeatDerivation) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brand   = D2D1::ColorF(1.0f, 0.5f, 0.0f, 1.0f);
    D2D1_COLOR_F myHover = D2D1::ColorF(0.0f, 0.6f, 0.9f, 1.0f);
    D2D1_COLOR_F myPress = D2D1::ColorF(0.1f, 0.1f, 0.1f, 1.0f);
    ButtonAppearance app{};
    app.background = brand;
    app.backgroundHover = myHover;
    app.backgroundPressed = myPress;

    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Hover, app, c), myHover));
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Pressed, app, c), myPress));
    // 静止态不受影响。
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Normal, app, c), brand));
}

// 没有任何覆盖时，显式状态色也能单独设置（此时基色跟随主题）。
TEST(ControlProperty, ButtonFillColor_ExplicitHoverWithoutBaseOverride) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F myHover = D2D1::ColorF(0.0f, 0.6f, 0.9f, 1.0f);
    ButtonAppearance app{};
    app.backgroundHover = myHover;

    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Hover, app, c), myHover));
    // 静止态仍然是主题色。
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Normal, app, c),
                        c.controlFillDefault));
}

// --- Accent 覆盖测试（无 background 时，accent 覆盖回退到 hover/pressed 版） -----

TEST(ControlProperty, ButtonFillColor_AccentKindUsesExplicitAccent) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandAccent = D2D1::ColorF(0.0f, 0.8f, 0.4f, 1.0f);
    ButtonAppearance app{};
    app.accent = brandAccent;

    // 静止态用品牌 accent 本身。
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Accent, VisualState::Normal, app, c),
                        brandAccent));
    // Hover/Pressed 从它派生（不再原样返回），否则自定义 accent 的按钮对鼠标无反应。
    D2D1_COLOR_F hover   = ButtonFillColor(Button::Kind::Accent, VisualState::Hover, app, c);
    D2D1_COLOR_F pressed = ButtonFillColor(Button::Kind::Accent, VisualState::Pressed, app, c);
    EXPECT_FALSE(ColorEq(hover, brandAccent));
    EXPECT_FALSE(ColorEq(pressed, brandAccent));
    EXPECT_FALSE(ColorEq(pressed, hover));
    // 显式设置时优先。
    ButtonAppearance withStates = app;
    withStates.accentHover = D2D1::ColorF(0.2f, 0.9f, 0.5f, 1.0f);
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Accent, VisualState::Hover, withStates, c),
                        *withStates.accentHover));

    // Disabled 淡化品牌 accent（alpha *= 0.4）
    D2D1_COLOR_F disabled = ButtonFillColor(Button::Kind::Accent, VisualState::Disabled, app, c);
    EXPECT_TRUE(std::fabs(disabled.a - 0.4f) < 0.01f && ColorEq(disabled, D2D1::ColorF(0.0f, 0.8f, 0.4f, 0.4f)));
}

// --- 默认路径测试（无覆盖时，状态驱动颜色） -------------------------------

TEST(ControlProperty, ButtonFillColor_AccentKindStates_NoOverride) {
    ColorTokens c = MakeTokens();
    ButtonAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Accent, VisualState::Normal, noOverride, c), c.accent));
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Accent, VisualState::Hover, noOverride, c), c.accentHover));
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Accent, VisualState::Pressed, noOverride, c), c.accentPressed));

    // Disabled 淡化主题 accent
    D2D1_COLOR_F disabled = ButtonFillColor(Button::Kind::Accent, VisualState::Disabled, noOverride, c);
    EXPECT_TRUE(std::fabs(disabled.a - 0.4f) < 0.01f);
}

TEST(ControlProperty, ButtonFillColor_StandardKindStates_NoOverride) {
    ColorTokens c = MakeTokens();
    ButtonAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Normal, noOverride, c), c.controlFillDefault));
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Hover, noOverride, c), c.controlFillHover));
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Standard, VisualState::Pressed, noOverride, c), c.controlFillPressed));

    // Disabled 把 alpha 设为 0.4（不是乘以 0.4 —— WithAlpha 是赋值不是相乘）。
    // 这是迁移前就有的行为，提取纯函数时原样保留。
    D2D1_COLOR_F disabled = ButtonFillColor(Button::Kind::Standard, VisualState::Disabled, noOverride, c);
    EXPECT_TRUE(std::fabs(disabled.a - 0.4f) < 0.01f);
}

TEST(ControlProperty, ButtonFillColor_SubtleKindStates_NoOverride) {
    ColorTokens c = MakeTokens();
    ButtonAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    // Subtle 静止时透明（alpha=0，让背景条显现）
    D2D1_COLOR_F normal = ButtonFillColor(Button::Kind::Subtle, VisualState::Normal, noOverride, c);
    EXPECT_TRUE(normal.a < 0.01f);

    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Subtle, VisualState::Hover, noOverride, c), c.controlFillHover));
    EXPECT_TRUE(ColorEq(ButtonFillColor(Button::Kind::Subtle, VisualState::Pressed, noOverride, c), c.controlFillPressed));

    // Subtle disabled 也透明
    D2D1_COLOR_F disabled = ButtonFillColor(Button::Kind::Subtle, VisualState::Disabled, noOverride, c);
    EXPECT_TRUE(disabled.a < 0.01f);
}

// --- 文本颜色覆盖测试 ---------------------------------------------

TEST(ControlProperty, ButtonTextColor_ExplicitForegroundWinsInAllStates) {
    ColorTokens c = MakeTokens();
    D2D1_COLOR_F brandText = D2D1::ColorF(0.9f, 0.1f, 0.1f, 1.0f);
    ButtonAppearance app{std::nullopt, brandText, std::nullopt};

    // 明确前景色在所有（种类 × 状态）生效，Disabled 也不降级到 textSecondary
    for (auto kind : {Button::Kind::Standard, Button::Kind::Accent, Button::Kind::Subtle}) {
        for (auto state : {VisualState::Normal, VisualState::Hover,
                           VisualState::Pressed, VisualState::Disabled}) {
            D2D1_COLOR_F got = ButtonTextColor(kind, state, app, c);
            EXPECT_TRUE(ColorEq(got, brandText));
        }
    }
}

// --- 文本颜色默认路径（Accent 用 onAccent，Standard/Subtle 用 textPrimary，Disabled 降级） --

TEST(ControlProperty, ButtonTextColor_AccentKindUsesOnAccent) {
    ColorTokens c = MakeTokens();
    ButtonAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    // Accent 的 Normal/Hover/Pressed 用 onAccent（对比色，深底亮字）
    for (auto state : {VisualState::Normal, VisualState::Hover, VisualState::Pressed}) {
        D2D1_COLOR_F got = ButtonTextColor(Button::Kind::Accent, state, noOverride, c);
        EXPECT_TRUE(ColorEq(got, c.onAccent));
    }

    // Disabled 降到 textSecondary
    D2D1_COLOR_F disabled = ButtonTextColor(Button::Kind::Accent, VisualState::Disabled, noOverride, c);
    EXPECT_TRUE(ColorEq(disabled, c.textSecondary));
}

TEST(ControlProperty, ButtonTextColor_StandardAndSubtleUseTextPrimary) {
    ColorTokens c = MakeTokens();
    ButtonAppearance noOverride{std::nullopt, std::nullopt, std::nullopt};

    for (auto kind : {Button::Kind::Standard, Button::Kind::Subtle}) {
        // Normal/Hover/Pressed 用 textPrimary
        for (auto state : {VisualState::Normal, VisualState::Hover, VisualState::Pressed}) {
            D2D1_COLOR_F got = ButtonTextColor(kind, state, noOverride, c);
            EXPECT_TRUE(ColorEq(got, c.textPrimary));
        }
        // Disabled 降到 textSecondary
        D2D1_COLOR_F disabled = ButtonTextColor(kind, VisualState::Disabled, noOverride, c);
        EXPECT_TRUE(ColorEq(disabled, c.textSecondary));
    }
}

// --- 边框测试（简单谓词） -----------------------------------------

TEST(ControlProperty, ButtonDrawsBorder_SubtleHasNoBorder) {
    EXPECT_TRUE(!ButtonDrawsBorder(Button::Kind::Subtle));
    EXPECT_TRUE(ButtonDrawsBorder(Button::Kind::Standard));
    EXPECT_TRUE(ButtonDrawsBorder(Button::Kind::Accent));
}
