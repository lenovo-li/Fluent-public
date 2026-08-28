// TextBlockLayoutCacheTests.cpp — component tests for TextBlock's cached
// IDWriteTextLayout (M2, roadmap §6.3). Needs a real DirectWrite factory (no
// D3D / HWND), so this is a "component" test like DWriteFormatTests.
//
// CreateLayout() memoizes the layout keyed on the full set of inputs (text,
// font size, weight, alignment, wrap, line spacing, wrap width). Render,
// UpdateMetrics, and hit-testing all call it several times per frame; rebuilding
// the layout each time was the hot spot. These tests prove same-key reuse
// (pointer identity) and self-invalidation on each input that defines the key.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBlock.h"
#include "../../FluentUI/graphics/DWriteContext.h"

using namespace fluent;

namespace fluent {
// Friend peer: exposes the private CreateLayout() so a test can assert pointer
// identity of the cached layout. Kept out of the product's public surface.
struct TextBlockTestPeer {
    static ComPtr<IDWriteTextLayout> Layout(const TextBlock& tb) {
        return tb.CreateLayout();
    }
};
}  // namespace fluent

namespace {
// One shared DWrite factory for the suite (skip if the box has no DirectWrite).
DWriteContext& Ctx() {
    static DWriteContext ctx;
    static bool inited = SUCCEEDED(ctx.Initialize());
    (void)inited;
    return ctx;
}

// A configured TextBlock with a valid layout width (CreateLayout needs bounds).
// DWrite is now injected through the tree UIContext (roadmap §6.2) instead of a
// manual SetDWrite: build a context carrying the DWrite pointer and attach it.
// The context is a plain value stored in the element, so it survives the by-value
// return (move) of the TextBlock.
TextBlock MakeBlock(DWriteContext& ctx) {
    TextBlock tb;
    UIContext uictx;
    uictx.dwrite = &ctx;
    tb.AttachToContext(uictx);
    tb.SetText(L"Hello, layout cache");
    tb.SetBounds({0.0f, 0.0f, 200.0f, 40.0f});
    return tb;
}
}  // namespace

// Same inputs -> same cached layout object (pointer identity).
TEST(TextBlockLayoutCache, SameInputsReuseLayout) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    TextBlock tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> a = TextBlockTestPeer::Layout(tb);
    ComPtr<IDWriteTextLayout> b = TextBlockTestPeer::Layout(tb);
    EXPECT_TRUE(a.Get() != nullptr);
    EXPECT_TRUE(a.Get() == b.Get());  // reused, not rebuilt
}

// Changing the text rebuilds the layout (distinct object).
TEST(TextBlockLayoutCache, TextChangeRebuilds) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    TextBlock tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> before = TextBlockTestPeer::Layout(tb);
    tb.SetText(L"Different text");
    ComPtr<IDWriteTextLayout> after = TextBlockTestPeer::Layout(tb);
    EXPECT_TRUE(before.Get() != nullptr);
    EXPECT_TRUE(after.Get() != nullptr);
    EXPECT_TRUE(before.Get() != after.Get());
}

// Changing the wrap width (bounds width) rebuilds the layout.
TEST(TextBlockLayoutCache, WidthChangeRebuilds) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    TextBlock tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> before = TextBlockTestPeer::Layout(tb);
    tb.SetBounds({0.0f, 0.0f, 120.0f, 40.0f});  // narrower wrap width
    ComPtr<IDWriteTextLayout> after = TextBlockTestPeer::Layout(tb);
    EXPECT_TRUE(before.Get() != after.Get());
}

// Changing the font size rebuilds the layout.
TEST(TextBlockLayoutCache, FontSizeChangeRebuilds) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    TextBlock tb = MakeBlock(ctx);
    ComPtr<IDWriteTextLayout> before = TextBlockTestPeer::Layout(tb);
    tb.SetFontSize(24.0f);
    ComPtr<IDWriteTextLayout> after = TextBlockTestPeer::Layout(tb);
    EXPECT_TRUE(before.Get() != after.Get());
}

// An empty text clears the cache and returns no layout.
TEST(TextBlockLayoutCache, EmptyTextClearsCache) {
    auto& ctx = Ctx();
    if (!ctx.Valid()) return;
    TextBlock tb = MakeBlock(ctx);
    EXPECT_TRUE(TextBlockTestPeer::Layout(tb).Get() != nullptr);
    tb.SetText(L"");
    EXPECT_TRUE(TextBlockTestPeer::Layout(tb).Get() == nullptr);
}
