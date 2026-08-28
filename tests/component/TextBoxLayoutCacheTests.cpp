// TextBoxLayoutCacheTests.cpp — component tests proving TextBox routes its
// per-frame IDWriteTextLayout through the shared ResourceCache (roadmap §13.3,
// WP-04). TextBox used to rebuild an identical layout 4-6 times per frame
// (Render + CaretX + HitIndex + ClampScroll); centralizing it means the same
// (text, size, height) is built once and reused.
//
// Needs a real DirectWrite factory (no D3D / HWND / GPU), so this is a component
// test like TextBlockLayoutCacheTests and self-skips when DWrite is unavailable.

#include "../framework/Test.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/graphics/ResourceCache.h"
#include "../../FluentUI/core/UIContext.h"

using namespace fluent;

namespace fluent {
// Friend peer: exposes the private CreateLayout(text) so a test can assert the
// cached layout's pointer identity. Kept out of the product's public surface.
struct TextBoxTestPeer {
    static ComPtr<IDWriteTextLayout> Layout(const TextBox& tb, const std::wstring& s) {
        return tb.CreateLayout(s);
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
}  // namespace

// Same inputs, cache present -> the layout object is reused (pointer identity),
// and the cache records a hit rather than a second build.
TEST(TextBoxLayoutCache, SameInputsReuseLayout) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);  // no D2D factory needed for text layout

    TextBox tb;
    UIContext uictx;
    uictx.dwrite = &dw;
    uictx.resourceCache = &cache;
    tb.AttachToContext(uictx);
    tb.SetBounds({0.0f, 0.0f, 200.0f, 28.0f});

    cache.ResetFrameStats();
    ComPtr<IDWriteTextLayout> a = TextBoxTestPeer::Layout(tb, L"hello");
    ComPtr<IDWriteTextLayout> b = TextBoxTestPeer::Layout(tb, L"hello");
    EXPECT_TRUE(a.Get() != nullptr);
    EXPECT_TRUE(a.Get() == b.Get());          // reused, not rebuilt
    EXPECT_EQ(cache.Stats().misses, 1u);      // built exactly once
    EXPECT_EQ(cache.Stats().hits, 1u);        // second call served from cache
}

// Different strings are distinct keys -> distinct layouts, two misses.
TEST(TextBoxLayoutCache, DifferentTextRebuilds) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);
    TextBox tb;
    UIContext uictx;
    uictx.dwrite = &dw;
    uictx.resourceCache = &cache;
    tb.AttachToContext(uictx);
    tb.SetBounds({0.0f, 0.0f, 200.0f, 28.0f});

    cache.ResetFrameStats();
    ComPtr<IDWriteTextLayout> a = TextBoxTestPeer::Layout(tb, L"alpha");
    ComPtr<IDWriteTextLayout> b = TextBoxTestPeer::Layout(tb, L"beta");
    EXPECT_TRUE(a.Get() != nullptr);
    EXPECT_TRUE(b.Get() != nullptr);
    EXPECT_TRUE(a.Get() != b.Get());
    EXPECT_EQ(cache.Stats().misses, 2u);
}

// Bumping the epoch (theme/DPI change) invalidates the cached layout: the same
// string rebuilds into a new object.
TEST(TextBoxLayoutCache, EpochBumpRebuilds) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    ResourceCache cache;
    cache.Initialize(&dw, nullptr);
    TextBox tb;
    UIContext uictx;
    uictx.dwrite = &dw;
    uictx.resourceCache = &cache;
    tb.AttachToContext(uictx);
    tb.SetBounds({0.0f, 0.0f, 200.0f, 28.0f});

    ComPtr<IDWriteTextLayout> before = TextBoxTestPeer::Layout(tb, L"same");
    cache.BumpEpoch();  // simulate a theme / DPI change
    ComPtr<IDWriteTextLayout> after = TextBoxTestPeer::Layout(tb, L"same");
    EXPECT_TRUE(before.Get() != nullptr);
    EXPECT_TRUE(after.Get() != nullptr);
    EXPECT_TRUE(before.Get() != after.Get());  // stale-epoch entry not reused
}

// Without a cache in the context (detached / headless), CreateLayout still works
// via the direct fallback path — just no reuse.
TEST(TextBoxLayoutCache, FallbackWithoutCache) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    TextBox tb;
    UIContext uictx;
    uictx.dwrite = &dw;  // no resourceCache
    tb.AttachToContext(uictx);
    tb.SetBounds({0.0f, 0.0f, 200.0f, 28.0f});

    ComPtr<IDWriteTextLayout> a = TextBoxTestPeer::Layout(tb, L"nocache");
    EXPECT_TRUE(a.Get() != nullptr);
}

// --- IME caret geometry -----------------------------------------------------
//
// CaretRectDip places the IME candidate window, and it asks for the caret at
// caret_ + composition_.size() — an index PAST the inline composition. That has to be
// measured against a string which CONTAINS the composition; measured against one that
// does not, the index is out of range, gets clamped to the end, and the candidate
// window freezes where composing began while the drawn caret keeps moving.
//
// The two disagreeing is what hid this: Render already used DisplayText, so the visible
// caret was always right and only the popup lagged.

TEST(TextBoxLayoutCache, CaretRectAdvancesWhileComposing) {
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    TextBox tb;
    UIContext uictx;
    uictx.dwrite = &dw;
    tb.AttachToContext(uictx);
    tb.SetBounds({0.0f, 0.0f, 200.0f, 28.0f});
    tb.SetText(L"ab");

    RectDip before{};
    EXPECT_TRUE(tb.CaretRectDip(before));

    // One composing character, then two: the caret must move right each time.
    tb.TestSetComposition(L"\u4e2d");
    RectDip one{};
    EXPECT_TRUE(tb.CaretRectDip(one));
    EXPECT_TRUE(one.x > before.x);

    tb.TestSetComposition(L"\u4e2d\u6587");
    RectDip two{};
    EXPECT_TRUE(tb.CaretRectDip(two));
    EXPECT_TRUE(two.x > one.x);

    // Committing the composition leaves the caret where the text now ends, not back
    // at the pre-composition position.
    tb.TestSetComposition(L"");
    RectDip after{};
    EXPECT_TRUE(tb.CaretRectDip(after));
    EXPECT_NEAR(after.x, before.x, 0.5f);   // text_ unchanged, so back to start
}

TEST(TextBoxLayoutCache, CaretRectMatchesDrawnCaretWhileComposing) {
    // The invariant that was violated: the caret X used for the candidate window and
    // the caret X the Render path draws must be the same number. They both come from
    // CaretX now, so this pins them together — if they diverge again, the popup drifts
    // away from the visible caret.
    auto& dw = Ctx();
    if (!dw.Valid()) return;

    TextBox tb;
    UIContext uictx;
    uictx.dwrite = &dw;
    tb.AttachToContext(uictx);
    tb.SetBounds({0.0f, 0.0f, 200.0f, 28.0f});
    tb.SetText(L"hello world");
    tb.TestSetComposition(L"\u4e2d\u6587\u5b57");

    RectDip rect{};
    EXPECT_TRUE(tb.CaretRectDip(rect));
    // Measure independently: a layout over the composed display string, hit-tested at
    // the same index Render uses. This is the position the caret is DRAWN at.
    std::wstring scratch;
    const std::wstring composed = L"hello world\u4e2d\u6587\u5b57";
    ComPtr<IDWriteTextLayout> layout = TextBoxTestPeer::Layout(tb, composed);
    EXPECT_TRUE(layout.Get() != nullptr);
    if (!layout) return;
    DWRITE_HIT_TEST_METRICS hm{};
    float x = 0.0f, y = 0.0f;
    layout->HitTestTextPosition(static_cast<UINT32>(composed.size()), FALSE, &x, &y, &hm);
    EXPECT_TRUE(x > 0.0f);

    // CaretRectDip = bounds_.x + ContentLeft() + CaretX(idx) - scrollX_. Here bounds_.x
    // is 0 and the text fits (no horizontal scroll), so the whole expression reduces to
    // the 10 DIP padding plus the layout-local x measured above. If CaretX were still
    // measuring the composition-free string, its result would be clamped short and this
    // difference would exceed the padding by the width of the composed characters.
    EXPECT_NEAR(rect.x, 10.0f + x, 0.5f);
}
