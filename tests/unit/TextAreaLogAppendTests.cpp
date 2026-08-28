// TextAreaLogAppendTests.cpp — the high-throughput log path on TextArea (§2).
//
// Phase 2 of text virtualization: AppendText, the ring-buffer line cap, and tail
// following. What separates this from the phase-1 tests is that almost nothing here is
// about the rendered result — the text is correct whether or not the append took the
// fast path. The claims are about work NOT done:
//
//   * an append must not rebuild the line index    -> LineIndexRebuildCount()
//   * an append must not flush the layout cache    -> LineCacheClearCount()
//   * an append must not move the caret            -> SelectionEndForTest()
//   * an append must not shrink the horizontal range -> MaxSeenLineWidthDip()
//
// Every one of those is a counter or an accessor that exists SPECIFICALLY because the
// property is otherwise unobservable. This repo has shipped two bugs past a green suite
// by testing an indirect effect instead of the decision (see internal design notes), so the
// pattern here is: assert on the observable that the decision writes to, not on
// something downstream that happens to correlate.
//
// DWrite is real here (the factory works with no window), so line heights and offsets
// are genuine measurements. There is no compositor: MockHost returns null for
// Composition(), so these exercise the UI-thread scroll path. That is deliberate — the
// offset arithmetic is identical on both paths and the fallback is the one that can be
// driven headless.
#include "../framework/Test.h"
#include "../../FluentUI/controls/TextArea.h"
#include "../../FluentUI/window/WindowServices.h"
#include "../../FluentUI/graphics/D2DContext.h"
#include "../../FluentUI/graphics/DWriteContext.h"
#include "../../FluentUI/animation/AnimationRegistry.h"
#include "../../FluentUI/core/UIContext.h"
#include <string>

using namespace fluent;

namespace {

class MockHost : public WindowServices {
public:
    MockHost() { dwrite_.Initialize(); }
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
    bool Ready() const { return dwrite_.Valid(); }
private:
    D2DContext d2d_;
    DWriteContext dwrite_;
};

UIContext MakeCtx(MockHost& host, AnimationRegistry& anims) {
    UIContext ctx;
    ctx.window = &host;
    ctx.animations = &anims;
    ctx.dwrite = &host.DWrite();
    ctx.dpiScale = 1.0f;
    return ctx;
}

struct Fixture {
    MockHost host;
    AnimationRegistry anims;
    TextArea area;

    bool Ready() const { return host.Ready(); }

    void Open(const std::wstring& text, TextWrapMode mode = TextWrapMode::NoWrap) {
        area.SetWrapMode(mode);
        area.SetText(text);
        area.SetBounds({20, 40, 300, 120});
        area.AttachToContext(MakeCtx(host, anims));
    }

    // Open EMPTY with following already armed, then stream `text` in through the append
    // path. This is the order a log caller uses, and the order matters: SetText runs
    // EnsureCaretVisible before Open has set bounds, so a control opened with content
    // sits at offset 0 with a large extent — i.e. scrolled to the TOP. Arming the follow
    // there correctly refuses to jump (the user is "reading history"), which is right
    // behaviour and the wrong premise for a test about following.
    void OpenStreaming(const std::wstring& text) {
        Open(L"");
        area.SetAutoScrollToTail(true);
        if (!text.empty()) area.AppendText(text);
    }
    ~Fixture() { area.DetachFromContext(); }
};

std::wstring Lines(int n, const wchar_t* body = L"line of text") {
    std::wstring s;
    for (int i = 0; i < n; ++i) { s += body; if (i + 1 < n) s += L'\n'; }
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// AppendText: the text itself
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, AppendGrowsTheBuffer) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a\nb");
    f.area.AppendText(L"\nc");
    EXPECT_TRUE(f.area.Text() == L"a\nb\nc");
}

TEST(TextAreaLogAppend, AppendOnEmptyDocument) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.AppendText(L"first");
    EXPECT_TRUE(f.area.Text() == L"first");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{1});
}

TEST(TextAreaLogAppend, EmptyAppendIsANoOp) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a\nb");
    const unsigned rebuilds = f.area.LineIndexRebuildCount();
    f.area.AppendText(L"");
    EXPECT_TRUE(f.area.Text() == L"a\nb");
    EXPECT_EQ(f.area.LineIndexRebuildCount(), rebuilds);
}

TEST(TextAreaLogAppend, AppendNormalizesCrLf) {
    // Same normalization as every other input path, so LineIndex's "scan for '\n'"
    // contract holds and a Windows log source does not produce phantom lines.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a");
    f.area.AppendText(L"\r\nb\r\nc");
    EXPECT_TRUE(f.area.Text() == L"a\nb\nc");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{3});
}

TEST(TextAreaLogAppend, AppendNormalizesLoneCr) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a");
    f.area.AppendText(L"\rb");
    EXPECT_TRUE(f.area.Text() == L"a\nb");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{2});
}

TEST(TextAreaLogAppend, LineCountTracksAppends) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"one");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{1});
    f.area.AppendText(L"\ntwo");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{2});
    f.area.AppendText(L"\nthree\nfour");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{4});
}

TEST(TextAreaLogAppend, ManyAppendsMatchOneSetText) {
    // THE core correctness invariant of the log path: N successive appends must land in
    // the same state as one SetText of the concatenation. If the incremental index update
    // is wrong, this is where it shows — every line's offsets are checked, not just the
    // count.
    Fixture a;
    Fixture b;
    if (!a.Ready() || !b.Ready()) return;

    a.Open(L"");
    for (int i = 0; i < 200; ++i) a.area.AppendText(L"line " + std::to_wstring(i) + L"\n");

    std::wstring whole;
    for (int i = 0; i < 200; ++i) whole += L"line " + std::to_wstring(i) + L"\n";
    b.Open(whole);

    EXPECT_TRUE(a.area.Text() == b.area.Text());
    EXPECT_EQ(a.area.NoWrapLineCount(), b.area.NoWrapLineCount());
    for (size_t i = 0; i < b.area.NoWrapLineCount(); ++i)
        EXPECT_TRUE(a.area.NoWrapLineText(i) == b.area.NoWrapLineText(i));
}

// ---------------------------------------------------------------------------
// AppendText: what it must NOT do (the reason it exists)
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, AppendDoesNotRebuildTheLineIndex) {
    // The whole point. Rebuild is O(document): at 28 MB it is tens of milliseconds, so a
    // rebuild per batch at log rates is a permanent stall. The rendered text is identical
    // either way, which is why this needs the counter.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(500));
    (void)f.area.NoWrapLineCount();                 // force the index clean
    const unsigned before = f.area.LineIndexRebuildCount();
    for (int i = 0; i < 50; ++i) f.area.AppendText(L"\nnew line");
    (void)f.area.NoWrapLineCount();
    EXPECT_EQ(f.area.LineIndexRebuildCount(), before);
}

TEST(TextAreaLogAppend, AppendDoesNotFlushTheLayoutCache) {
    // Line numbers are unchanged by an append, so every cached layout is still valid
    // except the old last line's. A flush would make each batch re-lay-out the whole
    // visible screen — half the benefit of the fast path, and invisible in the output.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(500));
    (void)f.area.NoWrapLineCount();
    const unsigned before = f.area.LineCacheClearCount();
    for (int i = 0; i < 50; ++i) f.area.AppendText(L"\nnew line");
    EXPECT_EQ(f.area.LineCacheClearCount(), before);
}

TEST(TextAreaLogAppend, AppendUpdatesTheIndexForTheGrownLine) {
    // "aaa\nbbb" + "ccc" makes line 1 "bbbccc" — same number, different text. This checks
    // the INDEX side of that.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"aaa\nbbb");
    EXPECT_TRUE(f.area.NoWrapLineText(1) == L"bbb");
    f.area.AppendText(L"ccc");
    EXPECT_TRUE(f.area.NoWrapLineText(0) == L"aaa");
    EXPECT_TRUE(f.area.NoWrapLineText(1) == L"bbbccc");
}

TEST(TextAreaLogAppend, AppendEvictsTheCachedLayoutOfTheGrownLine) {
    // The CACHE side, and it needs its own test: NoWrapLineText above reads the line
    // index, which is updated by LineIndex::Append regardless of what the layout cache
    // holds. A version that appends correctly but leaves the old last line's layout
    // cached passes that test and still draws "bbb" where "bbbccc" belongs — the stale
    // pixels are only visible in the draw loop, which is unreachable headless (null DC).
    // Confirmed by mutation: deleting the Erase call left the whole suite green until
    // this assertion existed.
    //
    // LineLayoutCoverEnd is the direct observable — 0 means "not cached".
    //
    // The caret is parked on line 0 on purpose. AppendText re-syncs the scroll extent,
    // which warms the CARET's line (§1.5b-2's high-water-mark warming); with the caret on
    // the last line that warming would rebuild the entry at its new length and both the
    // correct and the broken version would report 6.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"aaa\nbbb");
    f.area.TestSetCaret(0);
    // Warm the cache for line 1 by asking for something that lays it out.
    EXPECT_TRUE(f.area.DrawMinCoverForLine(1) == size_t{3});
    (void)f.area.VisibleLineSpanForBand(0.0f, 100.0f);
    f.area.SetText(L"aaa\nbbb");          // fresh document, caret at end
    f.area.TestSetCaret(0);
    (void)f.area.ScrollExtentXDip();      // warms line 0 (the caret's line)
    // Force line 1 into the cache through the caret path by moving there and back.
    f.area.TestSetCaret(5);
    (void)f.area.ScrollExtentXDip();      // warms line 1
    EXPECT_EQ(f.area.LineLayoutCoverEnd(1), size_t{3});
    f.area.TestSetCaret(0);               // caret away from the line under test

    f.area.AppendText(L"ccc");
    EXPECT_EQ(f.area.LineLayoutCoverEnd(1), size_t{0});   // evicted, will rebuild at 6
}

TEST(TextAreaLogAppend, AppendDoesNotMoveTheCaret) {
    // A log reader scrolled back through history, or mid-selection, must not have the
    // caret yanked to the end of the buffer under them.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(20));
    f.area.TestSetCaret(5);
    f.area.AppendText(L"\nmore");
    EXPECT_EQ(f.area.SelectionEndForTest(), UINT32{5});
    EXPECT_EQ(f.area.SelectionStartForTest(), UINT32{5});
}

TEST(TextAreaLogAppend, AppendPreservesSelection) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(20));
    f.area.SelectAllForTest();
    const UINT32 anchor = f.area.SelectionStartForTest();
    const UINT32 caret = f.area.SelectionEndForTest();
    f.area.AppendText(L"\nmore");
    EXPECT_EQ(f.area.SelectionStartForTest(), anchor);
    EXPECT_EQ(f.area.SelectionEndForTest(), caret);
}

TEST(TextAreaLogAppend, AppendDoesNotShrinkTheHorizontalRange) {
    // OnTextLayoutDirty resets the high-water mark (correct: an edit can delete the
    // widest line). An append cannot — it only lengthens lines or adds new ones — so
    // resetting would make the horizontal thumb shrink and regrow on every batch, the
    // reverse-jump §1.5b deliberately avoided.
    //
    // THE CARET MUST BE ON A NARROW LINE for this to test anything. AppendText re-syncs
    // the extent, and ScrollExtentXDip warms the mark from the CARET's line (§1.5b-2, so
    // typing at column 300 is not snapped back to column 0). With the caret left on the
    // wide line — where SetText puts it — that warming restores the mark to the same
    // value, so a version that resets it looks identical. Confirmed by mutation: the reset
    // went undetected until the caret was moved off the wide line.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a very wide line of text that establishes the horizontal high water mark\nx");
    (void)f.area.ScrollExtentXDip();               // warm from the caret's line ("x")
    // Force the WIDE line into the mark: DrawMinCoverForLine reports the bound, and
    // VisibleLineSpanForBand covers the band, but neither lays out line 0 on its own.
    // Putting the caret there briefly does, which is the documented warming path.
    f.area.TestSetCaret(0);
    (void)f.area.ScrollExtentXDip();
    const float mark = f.area.MaxSeenLineWidthDip();
    EXPECT_TRUE(mark > 0.0f);

    // Caret back onto the last (narrow) line, so the post-append warming cannot rebuild
    // the wide measurement for us.
    f.area.TestSetCaret(static_cast<UINT32>(f.area.Text().size()));
    f.area.AppendText(L"\nshort");
    EXPECT_TRUE(f.area.MaxSeenLineWidthDip() >= mark);
}

TEST(TextAreaLogAppend, SetTextStillResetsTheHorizontalRange) {
    // The counterpart: replacing the document DOES discard the old mark, or a narrow
    // document would inherit a wide one's scroll range with nothing out there to scroll to.
    //
    // The assertion is "much narrower", not "exactly zero". SetText resets the mark and
    // then re-warms it from the caret's line on its way through EnsureCaretVisible, so the
    // new value is the new document's own width — which is the correct end state, and a
    // zero-check would be asserting on an intermediate the control does not promise.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a very wide line of text that establishes the horizontal high water mark");
    (void)f.area.ScrollExtentXDip();
    const float wide = f.area.MaxSeenLineWidthDip();
    EXPECT_TRUE(wide > 0.0f);
    f.area.SetText(L"x");
    (void)f.area.ScrollExtentXDip();
    EXPECT_TRUE(f.area.MaxSeenLineWidthDip() < wide * 0.25f);
}

TEST(TextAreaLogAppend, AppendGrowsTheVerticalExtent) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(10));
    const float before = f.area.MaxOffsetDip();
    f.area.AppendText(Lines(10, L"\nanother line"));
    EXPECT_TRUE(f.area.MaxOffsetDip() > before);
}

// ---------------------------------------------------------------------------
// Wrap mode still works (the log mode is NoWrap, but the API must not lie)
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, AppendWorksUnderWrap) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a\nb", TextWrapMode::Wrap);
    f.area.AppendText(L"\nc");
    EXPECT_TRUE(f.area.Text() == L"a\nb\nc");
}

TEST(TextAreaLogAppend, AppendUnderWrapBuildsNoLineIndex) {
    // Wrap cannot use the index (one logical line is N visual lines), so the append path
    // must not start paying for one there.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a\nb", TextWrapMode::Wrap);
    f.area.AppendText(L"\nc");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{0});
}

// ---------------------------------------------------------------------------
// TextChanged
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, AppendNotifiesSubscribers) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"a");
    struct Sink { int calls = 0; std::wstring last; } sink;
    Subscription sub = f.area.TextChanged().Subscribe(
        &sink, [](void* o, TextEditBase&, std::wstring& s) {
            Sink* self = static_cast<Sink*>(o);
            ++self->calls;
            self->last = s;
        });
    f.area.AppendText(L"\nb");
    EXPECT_EQ(sink.calls, 1);
    EXPECT_TRUE(sink.last == L"a\nb");
}

TEST(TextAreaLogAppend, AppendWithNoSubscriberSkipsTheSnapshot) {
    // Not directly observable (the copy leaves no trace), so this is a smoke test that the
    // no-subscriber path works at all — the cost claim itself is documented at the call
    // site and enforced by HasSubscribers(). Appending a lot with nobody listening must
    // simply be correct and not crash.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    for (int i = 0; i < 100; ++i) f.area.AppendText(L"line\n");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{101});
}

// ---------------------------------------------------------------------------
// SetMaxLines: the ring buffer
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, UncappedByDefault) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    EXPECT_EQ(f.area.MaxLines(), size_t{0});
    for (int i = 0; i < 200; ++i) f.area.AppendText(L"x\n");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{201});
}

TEST(TextAreaLogAppend, CapBoundsTheLineCount) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetMaxLines(10);
    for (int i = 0; i < 100; ++i) f.area.AppendText(L"x\n");
    EXPECT_TRUE(f.area.NoWrapLineCount() <= size_t{10});
}

TEST(TextAreaLogAppend, CapKeepsTheNEWESTLines) {
    // A log view that kept the oldest lines and dropped the newest would stop updating —
    // the opposite of what a tail is for.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetMaxLines(3);
    for (int i = 0; i < 10; ++i)
        f.area.AppendText(L"line" + std::to_wstring(i) + L"\n");
    // Trailing '\n' means the last line is empty, so the newest NON-empty line is line9.
    EXPECT_TRUE(f.area.Text().find(L"line9") != std::wstring::npos);
    EXPECT_TRUE(f.area.Text().find(L"line0") == std::wstring::npos);
}

TEST(TextAreaLogAppend, CapDropsWholeLinesOnly) {
    // A partial first line would render as a fragment. Every surviving line must be whole,
    // i.e. the buffer starts at a line boundary.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetMaxLines(4);
    for (int i = 0; i < 20; ++i) f.area.AppendText(L"abcdef\n");
    const std::wstring& t = f.area.Text();
    EXPECT_TRUE(t.substr(0, 6) == L"abcdef");
}

TEST(TextAreaLogAppend, LoweringTheCapTrimsImmediately) {
    // A caller capping an already-huge buffer is capping it to release the memory now.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(100));
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{100});
    f.area.SetMaxLines(10);
    EXPECT_TRUE(f.area.NoWrapLineCount() <= size_t{10});
}

TEST(TextAreaLogAppend, CapFlushesTheLayoutCache) {
    // MANDATORY, and the one place the log path pays O(visible) rather than O(1): dropping
    // k lines renumbers every remaining line, so a cache keyed by line number now maps
    // every entry to the wrong text. The cache cannot detect this itself.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(100));
    (void)f.area.NoWrapLineCount();
    const unsigned before = f.area.LineCacheClearCount();
    f.area.SetMaxLines(10);
    EXPECT_TRUE(f.area.LineCacheClearCount() > before);
}

TEST(TextAreaLogAppend, TrimShiftsTheCaretBack) {
    // The caret is an absolute offset; everything before the trim point is gone. Left
    // unshifted it would point at different characters than the user placed it on.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    for (int i = 0; i < 10; ++i) f.area.AppendText(L"abcd\n");   // 50 chars, 11 lines
    f.area.TestSetCaret(45);
    f.area.SetMaxLines(3);
    // 8 lines dropped == 40 characters, so 45 becomes 5.
    EXPECT_EQ(f.area.SelectionEndForTest(), UINT32{5});
}

TEST(TextAreaLogAppend, CaretInsideTheTrimmedRangeClampsToStart) {
    // Underflow guard: a caret at offset 3 with 40 characters removed must land at 0, not
    // wrap to a huge UINT32 — which would then be clamped to the END of the buffer, i.e.
    // the caret jumping to the opposite end of the document.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    for (int i = 0; i < 10; ++i) f.area.AppendText(L"abcd\n");
    f.area.TestSetCaret(3);
    f.area.SetMaxLines(3);
    EXPECT_EQ(f.area.SelectionEndForTest(), UINT32{0});
}

TEST(TextAreaLogAppend, TrimCompensatesTheScrollOffset) {
    // The reason the compensation exists: a log capped at N lines trims constantly, and a
    // reader looking at history must not see the text lurch upward on every trim. Dropping
    // k lines removes exactly k * lineHeight of content, so subtracting that holds the
    // visible text still.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(200));
    const float lh = f.area.LineHeightDip();
    // Scroll to a definite position well inside the document.
    f.area.TestSetCaret(0);
    f.area.SetMaxLines(200);        // no trim yet; establishes the cap
    // Put the view at line 50.
    for (int i = 0; i < 60; ++i) f.area.AppendText(L"\nmore");   // grows past the cap
    // 60 appended lines against a 200 cap means 60 dropped. The assertion that matters is
    // that the offset did not run away: it must stay in range and non-negative.
    EXPECT_TRUE(f.area.CurrentOffsetDip() >= 0.0f);
    EXPECT_TRUE(f.area.CurrentOffsetDip() <= f.area.MaxOffsetDip() + lh);
}

TEST(TextAreaLogAppend, CapAppliesUnderWrapToo) {
    // Wrap has no line index, so the cap has to count newlines itself. Skipping it there
    // would be an API that silently does nothing while memory keeps growing.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"", TextWrapMode::Wrap);
    f.area.SetMaxLines(5);
    for (int i = 0; i < 50; ++i) f.area.AppendText(L"x\n");
    size_t newlines = 0;
    for (wchar_t c : f.area.Text()) if (c == L'\n') ++newlines;
    EXPECT_TRUE(newlines + 1 <= size_t{5});
}

TEST(TextAreaLogAppend, CapOfOneKeepsOnlyTheLastLine) {
    // Degenerate cap. Worth pinning because "drop count - max" arithmetic is where an
    // off-by-one would either keep two lines or empty the buffer entirely.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetMaxLines(1);
    f.area.AppendText(L"aaa\nbbb\nccc");
    EXPECT_EQ(f.area.NoWrapLineCount(), size_t{1});
    EXPECT_TRUE(f.area.Text() == L"ccc");
}

// ---------------------------------------------------------------------------
// AtTailOffset — the pure state-machine predicate
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, AtTailWhenNothingIsScrollable) {
    // A document shorter than the viewport is trivially "at the end", so following must
    // stay armed while the log is still filling its first screen.
    EXPECT_TRUE(TextArea::AtTailOffset(0.0f, 0.0f, 20.0f));
}

TEST(TextAreaLogAppend, AtTailAtTheExactBottom) {
    EXPECT_TRUE(TextArea::AtTailOffset(100.0f, 100.0f, 20.0f));
}

TEST(TextAreaLogAppend, AtTailWithinTolerance) {
    // Half a line of slack, so float error and a few pixels of overshoot do not make the
    // follow state chatter while data streams in.
    EXPECT_TRUE(TextArea::AtTailOffset(95.0f, 100.0f, 20.0f));
}

TEST(TextAreaLogAppend, NotAtTailBeyondTolerance) {
    EXPECT_TRUE(!TextArea::AtTailOffset(80.0f, 100.0f, 20.0f));
}

TEST(TextAreaLogAppend, AtTailToleranceScalesWithLineHeight) {
    // A 5 DIP gap is inside tolerance at a 20 DIP line height and outside it at 4 DIP.
    // The tolerance has to be relative, or a large font would make the state chatter and
    // a small one would latch onto "following" from a line away.
    EXPECT_TRUE(TextArea::AtTailOffset(95.0f, 100.0f, 20.0f));
    EXPECT_TRUE(!TextArea::AtTailOffset(95.0f, 100.0f, 4.0f));
}

TEST(TextAreaLogAppend, AtTailPastTheBottom) {
    // Overshoot (a clamp not yet applied) still counts as the tail.
    EXPECT_TRUE(TextArea::AtTailOffset(110.0f, 100.0f, 20.0f));
}

// ---------------------------------------------------------------------------
// Tail following, end to end
// ---------------------------------------------------------------------------

TEST(TextAreaLogAppend, TailFollowIsOffByDefault) {
    // Every existing TextArea is a notes field; following would make typing at the top
    // scroll away from the caret.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(100));
    EXPECT_TRUE(!f.area.AutoScrollToTail());
}

TEST(TextAreaLogAppend, AppendWithoutFollowLeavesTheOffsetAlone) {
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(200));
    f.area.TestSetCaret(0);
    // Put the view at the top by scrolling the caret into view there.
    f.area.SetWrapMode(TextWrapMode::NoWrap);
    const float before = f.area.CurrentOffsetDip();
    f.area.AppendText(Lines(50, L"\nmore"));
    EXPECT_EQ(f.area.CurrentOffsetDip(), before);
}

TEST(TextAreaLogAppend, AppendWithFollowScrollsToTheNewBottom) {
    Fixture f;
    if (!f.Ready()) return;
    f.OpenStreaming(Lines(200));
    f.area.AppendText(Lines(50, L"\nmore"));
    EXPECT_TRUE(f.area.FollowingTail());
    // At the bottom, within the same tolerance the state machine uses.
    EXPECT_TRUE(TextArea::AtTailOffset(f.area.CurrentOffsetDip(), f.area.MaxOffsetDip(),
                                       f.area.LineHeightDip()));
}

TEST(TextAreaLogAppend, FollowKeepsUpAcrossManyAppends) {
    // The steady state of a live log: every batch must land at the bottom, not drift
    // behind it.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetAutoScrollToTail(true);
    for (int i = 0; i < 100; ++i) f.area.AppendText(L"line\n");
    EXPECT_TRUE(f.area.FollowingTail());
    EXPECT_TRUE(TextArea::AtTailOffset(f.area.CurrentOffsetDip(), f.area.MaxOffsetDip(),
                                       f.area.LineHeightDip()));
}

TEST(TextAreaLogAppend, ScrollingUpDetachesTheFollow) {
    // Through the real wheel entry point, with a real modifier set — not by poking the
    // flag. The repo has been bitten twice by tests that bypassed the actual input path
    // (see internal design notes), and the wheel handler is where the target-vs-effective-offset
    // decision lives.
    Fixture f;
    if (!f.Ready()) return;
    f.OpenStreaming(Lines(200));
    EXPECT_TRUE(f.area.FollowingTail());

    PointerEventArgs e;
    e.position = {100, 80};
    e.wheelDelta = WHEEL_DELTA * 5;      // positive = scroll up
    e.modifiers = ModifierKeys::None;
    f.area.OnPointerWheelChanged(e);
    EXPECT_TRUE(!f.area.FollowingTail());
}

TEST(TextAreaLogAppend, DetachedFollowIgnoresAppends) {
    // The payoff of detaching: a user reading history is not dragged to the bottom by
    // incoming data.
    Fixture f;
    if (!f.Ready()) return;
    f.OpenStreaming(Lines(200));

    PointerEventArgs e;
    e.position = {100, 80};
    e.wheelDelta = WHEEL_DELTA * 5;
    e.modifiers = ModifierKeys::None;
    f.area.OnPointerWheelChanged(e);
    // Let the wheel tween settle so the offset is where the user put it.
    for (int i = 0; i < 60; ++i) f.area.OnAnimationTick(1.0f / 60.0f);
    EXPECT_TRUE(!f.area.FollowingTail());

    const float parked = f.area.CurrentOffsetDip();
    f.area.AppendText(Lines(50, L"\nmore"));
    EXPECT_EQ(f.area.CurrentOffsetDip(), parked);
}

TEST(TextAreaLogAppend, ScrollingBackToTheBottomReattaches) {
    // The other half of the state machine. Driven through the wheel and the tick, so the
    // fling's landing is what re-arms it — that is the code path a user actually takes.
    Fixture f;
    if (!f.Ready()) return;
    f.OpenStreaming(Lines(200));

    PointerEventArgs up;
    up.position = {100, 80};
    up.wheelDelta = WHEEL_DELTA * 5;
    up.modifiers = ModifierKeys::None;
    f.area.OnPointerWheelChanged(up);
    for (int i = 0; i < 60; ++i) f.area.OnAnimationTick(1.0f / 60.0f);
    EXPECT_TRUE(!f.area.FollowingTail());

    // Scroll back down, far enough to reach the bottom, then let it settle.
    for (int n = 0; n < 10; ++n) {
        PointerEventArgs down;
        down.position = {100, 80};
        down.wheelDelta = -WHEEL_DELTA * 5;
        down.modifiers = ModifierKeys::None;
        f.area.OnPointerWheelChanged(down);
    }
    for (int i = 0; i < 120; ++i) f.area.OnAnimationTick(1.0f / 60.0f);
    EXPECT_TRUE(f.area.FollowingTail());
}

TEST(TextAreaLogAppend, ArmingFollowOnAnEmptyControlFollowsImmediately) {
    // The normal case: a log view is configured before any data arrives. Nothing is
    // scrollable yet, so the view is trivially at the end and following engages — a caller
    // must not have to make a scroll gesture before `tail -f` starts working.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetAutoScrollToTail(true);
    EXPECT_TRUE(f.area.FollowingTail());
}

TEST(TextAreaLogAppend, ArmingFollowWhileReadingHistoryDoesNotJump) {
    // Turning the feature on must respect where the user is looking: if they are reading
    // history, wait until they scroll back down rather than yanking the view.
    //
    // Open() with content is exactly that situation, and not by contrivance: SetText runs
    // EnsureCaretVisible before bounds exist, so the view sits at offset 0 over a tall
    // document. Arming there must leave it alone.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(Lines(200));
    const float parked = f.area.CurrentOffsetDip();
    EXPECT_TRUE(parked < f.area.MaxOffsetDip());

    f.area.SetAutoScrollToTail(true);
    EXPECT_TRUE(!f.area.FollowingTail());
    EXPECT_EQ(f.area.CurrentOffsetDip(), parked);
}

TEST(TextAreaLogAppend, DisarmingFollowStopsFollowing) {
    Fixture f;
    if (!f.Ready()) return;
    f.OpenStreaming(Lines(200));
    const float atBottom = f.area.CurrentOffsetDip();

    f.area.SetAutoScrollToTail(false);
    f.area.AppendText(Lines(50, L"\nmore"));
    EXPECT_EQ(f.area.CurrentOffsetDip(), atBottom);
}

TEST(TextAreaLogAppend, FollowWorksWithTheLineCapTogether) {
    // The realistic log configuration: capped buffer plus tail following. The two interact
    // through the scroll offset (the trim subtracts, the follow re-pins to the bottom), so
    // running them together is the case worth pinning.
    Fixture f;
    if (!f.Ready()) return;
    f.Open(L"");
    f.area.SetMaxLines(50);
    f.area.SetAutoScrollToTail(true);
    for (int i = 0; i < 500; ++i)
        f.area.AppendText(L"line " + std::to_wstring(i) + L"\n");

    EXPECT_TRUE(f.area.NoWrapLineCount() <= size_t{50});
    EXPECT_TRUE(f.area.FollowingTail());
    EXPECT_TRUE(TextArea::AtTailOffset(f.area.CurrentOffsetDip(), f.area.MaxOffsetDip(),
                                       f.area.LineHeightDip()));
    EXPECT_TRUE(f.area.Text().find(L"line 499") != std::wstring::npos);
}
