// ToolBarTests.cpp — headless tests for ToolBar layout and overflow behavior.

#include "../framework/Test.h"
#include "../../FluentUI/controls/ToolBar.h"
#include "../../FluentUI/controls/Button.h"
#include "../../FluentUI/controls/MenuFlyout.h"
#include "../../FluentUI/core/UIContext.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include <memory>

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

UIContext MakeCtx(MockHost& host) {
    UIContext ctx;
    ctx.window = &host;
    ctx.dpiScale = 1.0f;
    return ctx;
}

std::unique_ptr<Button> MakeButton(const wchar_t* text, float width = 80.0f) {
    auto btn = std::make_unique<Button>();
    btn->SetText(text);
    btn->SetWidth(width);
    btn->SetHeight(32.0f);
    return btn;
}

}  // namespace

TEST(ToolBar, Construction) {
    ToolBar tb;
    EXPECT_EQ(tb.TotalButtonCount(), 0);
    EXPECT_EQ(tb.VisibleButtonCount(), 0);
    EXPECT_FALSE(tb.HasOverflow());
}

TEST(ToolBar, AddButtons) {
    ToolBar tb;
    tb.AddButton(MakeButton(L"New"), []{});
    tb.AddButton(MakeButton(L"Open"), []{});
    tb.AddButton(MakeButton(L"Save"), []{});
    EXPECT_EQ(tb.TotalButtonCount(), 3);
}

TEST(ToolBar, AllButtonsVisible_WhenWideEnough) {
    MockHost host;
    ToolBar tb;
    tb.AddButton(MakeButton(L"New", 60), []{});
    tb.AddButton(MakeButton(L"Open", 60), []{});
    tb.AddButton(MakeButton(L"Save", 60), []{});
    tb.AttachToContext(MakeCtx(host));

    tb.Measure(500, 32);
    tb.Arrange(RectDip{0, 0, 500, 32});

    EXPECT_EQ(tb.VisibleButtonCount(), 3);
    EXPECT_FALSE(tb.HasOverflow());
}

TEST(ToolBar, Overflow_WhenTooNarrow) {
    MockHost host;
    ToolBar tb;
    // 5 buttons x 80 DIP each = 400 + spacing, but we only give 200.
    for (int i = 0; i < 5; ++i) {
        wchar_t label[16];
        _snwprintf_s(label, _TRUNCATE, L"Btn%d", i);
        tb.AddButton(MakeButton(label, 80), []{});
    }
    tb.AttachToContext(MakeCtx(host));

    tb.Measure(200, 32);
    tb.Arrange(RectDip{0, 0, 200, 32});

    EXPECT_TRUE(tb.HasOverflow());
    EXPECT_TRUE(tb.VisibleButtonCount() < 5);
    EXPECT_TRUE(tb.VisibleButtonCount() >= 1);  // at least one button visible
}

TEST(ToolBar, SeparatorCountsAsItem) {
    ToolBar tb;
    tb.AddButton(MakeButton(L"New"), []{});
    tb.AddSeparator();
    tb.AddButton(MakeButton(L"Save"), []{});
    EXPECT_EQ(tb.TotalButtonCount(), 2);  // separators don't count as buttons
}

TEST(ToolBar, MeasureIncludesOverflowButton) {
    MockHost host;
    ToolBar tb;
    for (int i = 0; i < 5; ++i) {
        wchar_t label[16];
        _snwprintf_s(label, _TRUNCATE, L"Button%d", i);
        tb.AddButton(MakeButton(label, 80), []{});
    }
    tb.AttachToContext(MakeCtx(host));

    // Wide enough: no overflow.
    tb.Measure(500, 32);
    EXPECT_FALSE(tb.HasOverflow());
    const float wideW = tb.Desired().w;

    // Narrow: overflow button appears.
    tb.Measure(200, 32);
    EXPECT_TRUE(tb.HasOverflow());
    const float narrowW = tb.Desired().w;

    // The narrow measurement should be smaller (fewer visible buttons).
    EXPECT_TRUE(narrowW < wideW);
}
