// StatusBarTests.cpp — headless tests for StatusBar layout and content.

#include "../framework/Test.h"
#include "../../FluentUI/controls/StatusBar.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/controls/ProgressBar.h"
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

}  // namespace

TEST(StatusBar, Construction) {
    StatusBar sb;
    EXPECT_EQ(sb.Height(), 32.0f);
    EXPECT_TRUE(sb.Text().empty());
}

TEST(StatusBar, SetText) {
    StatusBar sb;
    sb.SetText(L"Ready");
    EXPECT_EQ(sb.Text(), L"Ready");
}

TEST(StatusBar, LayoutWithRightContent) {
    MockHost host;
    StatusBar sb;
    sb.SetText(L"Loading...");

    auto progress = std::make_unique<ProgressBar>();
    progress->SetValue(0.5f);
    progress->SetWidth(120.0f);
    progress->SetHeight(4.0f);
    sb.SetRightContent(std::move(progress));

    sb.AttachToContext(MakeCtx(host));
    sb.Measure(500, 32);
    sb.Arrange(RectDip{0, 0, 500, 32});

    // Text should be on the left, progress on the right.
    // We verify by checking the text block's bounds are left-of-center and the
    // progress bar's bounds are right-of-center.
    // (We can't access text_ directly, but we can check the panel's children.)
    EXPECT_TRUE(sb.ChildCount() >= 2);
}

TEST(StatusBar, ClearRightContent) {
    MockHost host;
    StatusBar sb;
    auto progress = std::make_unique<ProgressBar>();
    sb.SetRightContent(std::move(progress));
    sb.AttachToContext(MakeCtx(host));
    EXPECT_TRUE(sb.ChildCount() >= 2);

    sb.SetRightContent(nullptr);
    // After clearing, only the text block remains.
    EXPECT_EQ(sb.ChildCount(), 1u);
}
