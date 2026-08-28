// EmptyTextBoxCaretTests.cpp — an empty focused TextBox must show a caret.
//
// THE BUG, from a screenshot: clicking an empty TextBox produced no visible caret, so
// the field looked unfocused and the user could not tell typing would go there.
//
// The cause is a branch, not a missing feature. TextBox::Render did:
//
//     if (disp.empty() && !placeholder_.empty())  { draw placeholder }
//     else { build layout; ...; if (focused) draw caret }
//
// The caret lived only in the else-branch, so ANY empty box that had a placeholder took
// the first branch and drew no caret. Every field in the demo has a placeholder, which
// is why it looked systemic. An empty box with NO placeholder happened to work, because
// it fell through to the else and laid out an empty string — which is why this went
// unnoticed.
//
// WHY PIXELS AND NOT A FLAG. There is no observable "caret was drawn" state to assert:
// caretVisible_ is the blink phase and was already true, and CaretRectDip() returned the
// right rect the whole time. The rect was correct and simply never painted. Only the
// surface can tell those two apart, so this renders into a real offscreen D2D target
// (the same harness FocusRingPixelTests uses) and counts ink in the caret column.

#include "../framework/Test.h"
#include "../framework/PixelSurface.h"
#include "../../FluentUI/controls/TextBox.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/styling/ThemeManager.h"

#include <cstdio>

using namespace fluent;
using fltest::Pixel;
using fltest::PixelSurface;

namespace {

constexpr Pixel kBg{255, 255, 255, 255};

// Build a TextBox the way a page does, give it bounds, and put a real DWrite context on
// it. UIContext::dwrite MUST be set explicitly: Control::Dwrite() reads that field, and
// a null one sends text controls down estimate paths that never lay anything out — a
// caret test against a null dwrite would pass vacuously.
struct Harness {
    UIContext ctx{};
    TextBox box;

    Harness(PixelSurface& surf, float w, float h) {
        ctx.dwrite = surf.Dwrite();
        box.AttachToContext(ctx);
        box.SetBounds({0.0f, 0.0f, w, h});
        box.SetFocused(true);
    }
};

// Does any pixel in the given column band differ from the background? The caret is a
// 1 DIP fill in textPrimary on a light control fill, so "moved away from white" is a
// robust test: it survives antialiasing and the exact subpixel x, and still fails hard
// when nothing was painted.
int InkedPixelsInBand(const PixelSurface& s, int x0, int x1, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Pixel p = s.At(x, y);
            const int d = std::abs(int(p.r) - int(kBg.r)) +
                          std::abs(int(p.g) - int(kBg.g)) +
                          std::abs(int(p.b) - int(kBg.b));
            if (d > 60) ++n;   // clearly darker than the page, not a faint AA edge
        }
    }
    return n;
}

}  // namespace

// Count ink in the caret band for an empty box that HAS a placeholder, at a given focus
// state. Returns focused and unfocused counts from two independent surfaces.
void PlaceholderBandInk(int* focused, int* unfocused) {
    for (int pass = 0; pass < 2; ++pass) {
        PixelSurface surf(200, 40);
        if (!surf.Valid()) { *focused = *unfocused = -1; return; }

        Harness h(surf, 200.0f, 32.0f);
        h.box.SetPlaceholder(L"最多 10 个字符");
        h.box.SetFocused(pass == 0);

        surf.Paint(kBg, [&](DrawingContext& gc) { h.box.Render(gc); });
        if (!surf.ReadBack()) { *focused = *unfocused = -1; return; }

        // Scan a narrow band at the left inset over the caret's vertical extent (Render
        // insets it 4 DIP top and bottom).
        const int ink = InkedPixelsInBand(surf, 2, 12, 6, 26);
        if (pass == 0) *focused = ink; else *unfocused = ink;
    }
}

// The regression: empty + placeholder + focused must still paint a caret.
//
// WHY THIS IS A DIFFERENCE AND NOT AN ABSOLUTE COUNT. The first version of this test just
// asserted "ink > 0" on the focused surface, and it PASSED against the reintroduced bug --
// because the placeholder's own glyphs start at the same origin as the caret and inked
// that band by themselves. Mutation testing is what exposed it: putting the caret back
// inside the else-branch did not flip the assertion, which means the assertion was never
// measuring the caret.
//
// Rendering the SAME box twice, differing only in focus, cancels the placeholder out: the
// placeholder is identical in both passes, so any extra ink in the focused pass is the
// caret and nothing else.
TEST(EmptyTextBoxCaret, EmptyBoxWithPlaceholderStillDrawsCaret) {
    int focused = 0, unfocused = 0;
    PlaceholderBandInk(&focused, &unfocused);
    if (focused < 0) { std::printf("  SKIP: no D2D device\n"); EXPECT_TRUE(false); return; }

    std::printf("  empty+placeholder band ink: focused %d, unfocused %d (caret = %d)\n",
                focused, unfocused, focused - unfocused);

    // The placeholder alone must not be mistaken for a caret, and focusing must add ink.
    EXPECT_TRUE(focused > unfocused);
}

// The discriminating case: NO placeholder, so nothing else can paint in that band. If a
// caret is missing here, the band is empty.
TEST(EmptyTextBoxCaret, EmptyBoxWithoutPlaceholderDrawsCaret) {
    PixelSurface surf(200, 40);
    if (!surf.Valid()) { std::printf("  SKIP: no D2D device\n"); EXPECT_TRUE(false); return; }

    Harness h(surf, 200.0f, 32.0f);
    // no placeholder at all
    surf.Paint(kBg, [&](DrawingContext& gc) { h.box.Render(gc); });
    EXPECT_TRUE(surf.ReadBack());

    const int ink = InkedPixelsInBand(surf, 2, 12, 6, 26);
    std::printf("  empty, no placeholder, focused: %d inked px\n", ink);
    EXPECT_TRUE(ink > 0);
}

// And the negative control: UNFOCUSED must NOT paint a caret. Without this, a fix that
// simply always draws the caret would pass the two tests above.
TEST(EmptyTextBoxCaret, UnfocusedEmptyBoxDrawsNoCaret) {
    PixelSurface surf(200, 40);
    if (!surf.Valid()) { std::printf("  SKIP: no D2D device\n"); EXPECT_TRUE(false); return; }

    Harness h(surf, 200.0f, 32.0f);
    h.box.SetFocused(false);

    surf.Paint(kBg, [&](DrawingContext& gc) { h.box.Render(gc); });
    EXPECT_TRUE(surf.ReadBack());

    const int ink = InkedPixelsInBand(surf, 2, 12, 6, 26);
    std::printf("  empty, no placeholder, UNfocused: %d inked px (want 0)\n", ink);
    EXPECT_TRUE(ink == 0);
}

// --- TextArea: the same defect, the same fix, so the same test ----------------
//
// TextArea::Render had the identical structure and therefore the identical bug: the caret
// lived inside the else-branch, so an empty multi-line field with a placeholder never
// showed one. The fix hoisted it out the same way.
//
// This test exists because the TextBox fix went in with a test and the TextArea fix
// initially did not -- and an untested fix in a 2100-line file is one refactor away from
// silently reverting.
namespace {

// Same two-pass difference trick: the placeholder is identical in both passes, so the
// extra ink in the focused pass is the caret alone.
void TextAreaPlaceholderBandInk(int* focused, int* unfocused) {
    for (int pass = 0; pass < 2; ++pass) {
        PixelSurface surf(240, 80);
        if (!surf.Valid()) { *focused = *unfocused = -1; return; }

        UIContext ctx{};
        ctx.dwrite = surf.Dwrite();
        TextArea area;
        area.AttachToContext(ctx);
        area.SetBounds({0.0f, 0.0f, 240.0f, 72.0f});
        area.SetPlaceholder(L"在此输入备注");
        area.SetFocused(pass == 0);

        surf.Paint(kBg, [&](DrawingContext& gc) { area.Render(gc); });
        if (!surf.ReadBack()) { *focused = *unfocused = -1; return; }

        // The caret sits at the text origin on the first line.
        const int ink = InkedPixelsInBand(surf, 2, 12, 4, 30);
        if (pass == 0) *focused = ink; else *unfocused = ink;
    }
}

}  // namespace

TEST(EmptyTextBoxCaret, EmptyTextAreaWithPlaceholderStillDrawsCaret) {
    int focused = 0, unfocused = 0;
    TextAreaPlaceholderBandInk(&focused, &unfocused);
    if (focused < 0) { std::printf("  SKIP: no D2D device\n"); EXPECT_TRUE(false); return; }

    std::printf("  TextArea empty+placeholder band ink: focused %d, unfocused %d (caret = %d)\n",
                focused, unfocused, focused - unfocused);
    EXPECT_TRUE(focused > unfocused);
}
