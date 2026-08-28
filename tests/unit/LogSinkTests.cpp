// LogSinkTests.cpp — the producer-side staging buffer for high-throughput logs (§2.3).
//
// This is one of the few genuinely threaded pieces in the repo, and the properties that
// matter are all about work NOT done: exactly one wakeup per batch no matter how many
// writes, no data lost across a concurrent drain, a bounded buffer when the consumer
// stalls. Every one of those is invisible to a test that only checks the text came
// through — the text comes through either way — so the class exposes counters
// (WakeupCount, DroppedChars, PendingChars) specifically so they can be asserted on.
// That is the same reasoning that put LineIndexRebuildCount on TextArea, and the same
// bug class: a batch that posts ten thousand wakeups still shows the right text.
//
// The wakeup is injected rather than being a PostMessage, so all of this runs with no
// window and no message pump.
#include "../framework/Test.h"
#include "../../FluentUI/text/LogSink.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace fluent;

// ---------------------------------------------------------------------------
// Basic staging
// ---------------------------------------------------------------------------

TEST(LogSink, DrainOnEmptySinkReturnsFalse) {
    LogSink sink;
    std::wstring out;
    EXPECT_TRUE(!sink.Drain(out));
    EXPECT_TRUE(out.empty());
}

TEST(LogSink, WriteLineAddsTerminator) {
    LogSink sink;
    sink.WriteLine(L"hello");
    std::wstring out;
    EXPECT_TRUE(sink.Drain(out));
    EXPECT_TRUE(out == L"hello\n");
}

TEST(LogSink, WriteLineDoesNotDoubleTerminator) {
    // A producer that already terminates its lines must not get blank lines inserted
    // between them — that would double the line count of every log source that does.
    LogSink sink;
    sink.WriteLine(L"hello\n");
    std::wstring out;
    EXPECT_TRUE(sink.Drain(out));
    EXPECT_TRUE(out == L"hello\n");
}

TEST(LogSink, WriteIsVerbatim) {
    // Write() is the "I already have a correctly terminated block" path, so it must not
    // add anything — a pipe read ending mid-line has to stay mid-line until the rest
    // arrives.
    LogSink sink;
    sink.Write(L"part");
    std::wstring out;
    EXPECT_TRUE(sink.Drain(out));
    EXPECT_TRUE(out == L"part");
}

TEST(LogSink, WritesAccumulateIntoOneBatch) {
    LogSink sink;
    sink.WriteLine(L"a");
    sink.WriteLine(L"b");
    sink.WriteLine(L"c");
    std::wstring out;
    EXPECT_TRUE(sink.Drain(out));
    EXPECT_TRUE(out == L"a\nb\nc\n");
}

TEST(LogSink, DrainClearsPending) {
    LogSink sink;
    sink.WriteLine(L"x");
    std::wstring out;
    sink.Drain(out);
    EXPECT_EQ(sink.PendingChars(), size_t{0});
    EXPECT_TRUE(!sink.Drain(out));
}

TEST(LogSink, DrainClearsCallerBufferEvenWhenEmpty) {
    // The consumer reuses one buffer forever, so a drain that finds nothing must not
    // leave the PREVIOUS batch in it — that would append the same lines twice.
    LogSink sink;
    std::wstring out = L"stale content";
    EXPECT_TRUE(!sink.Drain(out));
    EXPECT_TRUE(out.empty());
}

TEST(LogSink, EmptyWriteIsIgnored) {
    LogSink sink;
    sink.Write(L"");
    EXPECT_EQ(sink.PendingChars(), size_t{0});
    EXPECT_EQ(sink.WakeupCount(), size_t{0});
}

TEST(LogSink, EmptyWriteLineStillProducesABlankLine) {
    // WriteLine("") is a producer emitting a blank log line, which is real data — unlike
    // Write(""), which is a producer with nothing to say.
    LogSink sink;
    sink.WriteLine(L"");
    std::wstring out;
    EXPECT_TRUE(sink.Drain(out));
    EXPECT_TRUE(out == L"\n");
}

// ---------------------------------------------------------------------------
// The wakeup invariant: at most one unconsumed wakeup
// ---------------------------------------------------------------------------

TEST(LogSink, FirstWriteFiresTheWakeup) {
    LogSink sink;
    int fired = 0;
    sink.SetWakeup([&fired] { ++fired; });
    sink.WriteLine(L"a");
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(sink.WakeupPending());
}

TEST(LogSink, ManyWritesFireExactlyOneWakeup) {
    // THE central property. At ten thousand lines a second, one wakeup per line means the
    // UI thread spends the burst dispatching notifications for work it has already done,
    // and the backlog outlives the burst. The text would still be correct — which is why
    // this needs a counter rather than an output check.
    LogSink sink;
    int fired = 0;
    sink.SetWakeup([&fired] { ++fired; });
    for (int i = 0; i < 1000; ++i) sink.WriteLine(L"line");
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(sink.WakeupCount(), size_t{1});
}

TEST(LogSink, WakeupRearmsAfterDrain) {
    LogSink sink;
    int fired = 0;
    sink.SetWakeup([&fired] { ++fired; });
    sink.WriteLine(L"a");
    std::wstring out;
    sink.Drain(out);
    EXPECT_TRUE(!sink.WakeupPending());
    sink.WriteLine(L"b");
    EXPECT_EQ(fired, 2);
}

TEST(LogSink, DrainWithNoDataStillRearms) {
    // A consumer may drain speculatively (every frame, say). That must not leave the slot
    // claimed, or the next real write would be silent.
    LogSink sink;
    int fired = 0;
    sink.SetWakeup([&fired] { ++fired; });
    std::wstring out;
    sink.Drain(out);
    sink.WriteLine(L"a");
    EXPECT_EQ(fired, 1);
}

TEST(LogSink, NoWakeupFunctionIsLegal) {
    // A polling consumer needs no wakeup, but the pending flag must still be maintained
    // so the coalescing behaviour is identical either way.
    LogSink sink;
    sink.WriteLine(L"a");
    EXPECT_TRUE(sink.WakeupPending());
    EXPECT_EQ(sink.WakeupCount(), size_t{1});
}

TEST(LogSink, WriteLineFiresAtMostOneWakeupDespiteTwoAppends) {
    // WriteLine appends the text and then the terminator. An implementation that claimed
    // the wakeup slot per append would fire twice for one line.
    LogSink sink;
    int fired = 0;
    sink.SetWakeup([&fired] { ++fired; });
    sink.WriteLine(L"unterminated");
    EXPECT_EQ(fired, 1);
}

// ---------------------------------------------------------------------------
// The pending cap
// ---------------------------------------------------------------------------

TEST(LogSink, UncappedByDefault) {
    LogSink sink;
    EXPECT_EQ(sink.MaxPending(), size_t{0});
    for (int i = 0; i < 100; ++i) sink.WriteLine(L"0123456789");
    EXPECT_EQ(sink.PendingChars(), size_t{1100});   // 100 * (10 + 1)
    EXPECT_EQ(sink.DroppedChars(), size_t{0});
}

TEST(LogSink, CapBoundsPendingSize) {
    // The point of the cap: a UI thread stalled inside a resize drag while a producer
    // keeps writing must not let the staging buffer grow without limit.
    LogSink sink;
    sink.SetMaxPending(50);
    for (int i = 0; i < 100; ++i) sink.WriteLine(L"0123456789");
    EXPECT_TRUE(sink.PendingChars() <= size_t{50});
    EXPECT_TRUE(sink.DroppedChars() > size_t{0});
}

TEST(LogSink, CapDropsOldestNotNewest) {
    // For a log the newest data is the interesting data, and the old lines were headed
    // for TextArea's own ring-buffer trim anyway. Dropping the newest would make the
    // view stop updating under load, which looks like a hang.
    LogSink sink;
    sink.SetMaxPending(12);
    sink.WriteLine(L"oldest");   // 7 chars with terminator
    sink.WriteLine(L"newest");   // 7 more -> 14 > 12, so "oldest\n" goes
    std::wstring out;
    EXPECT_TRUE(sink.Drain(out));
    EXPECT_TRUE(out == L"newest\n");
    EXPECT_EQ(sink.DroppedChars(), size_t{7});
}

TEST(LogSink, CapCutsAtLineBoundary) {
    // A partial line reaching the view would render as a fragment with no context. The
    // cut rounds FORWARD to the next newline, so what survives always starts at a line
    // start.
    LogSink sink;
    sink.SetMaxPending(20);
    sink.WriteLine(L"aaaa");    // 5
    sink.WriteLine(L"bbbb");    // 10
    sink.WriteLine(L"cccc");    // 15
    sink.WriteLine(L"dddd");    // 20
    sink.WriteLine(L"eeee");    // 25 -> over
    std::wstring out;
    sink.Drain(out);
    // Whatever survived, it must begin at a line start: no leading fragment.
    EXPECT_TRUE(!out.empty());
    EXPECT_TRUE(out.find(L"eeee\n") != std::wstring::npos);
    // Every surviving line is complete, i.e. the buffer ends with a terminator and
    // contains no half line at the front.
    EXPECT_TRUE(out.back() == L'\n');
    EXPECT_TRUE(out[0] == L'b' || out[0] == L'c' || out[0] == L'd' || out[0] == L'e');
}

TEST(LogSink, LoweringCapTrimsImmediately) {
    // A caller lowering the cap on an oversized buffer is doing it to release the memory
    // NOW, not at the next write.
    LogSink sink;
    for (int i = 0; i < 10; ++i) sink.WriteLine(L"0123456789");
    EXPECT_EQ(sink.PendingChars(), size_t{110});
    sink.SetMaxPending(30);
    EXPECT_TRUE(sink.PendingChars() <= size_t{30});
}

TEST(LogSink, LoweringCapDoesNotFireASecondWakeup) {
    // A trim only removes characters. Whatever staged them already claimed the wakeup, so
    // firing again would put two unconsumed wakeups in flight for one batch — the exact
    // invariant this class exists to hold.
    LogSink sink;
    int fired = 0;
    sink.SetWakeup([&fired] { ++fired; });
    for (int i = 0; i < 10; ++i) sink.WriteLine(L"0123456789");
    EXPECT_EQ(fired, 1);
    sink.SetMaxPending(30);
    EXPECT_EQ(fired, 1);
}

TEST(LogSink, RaisingCapDoesNotDrop) {
    LogSink sink;
    sink.SetMaxPending(20);
    sink.WriteLine(L"0123456789");
    sink.SetMaxPending(1000);
    EXPECT_EQ(sink.DroppedChars(), size_t{0});
    EXPECT_EQ(sink.PendingChars(), size_t{11});
}

TEST(LogSink, CapWithNoNewlineStillBoundsMemory) {
    // One enormous unterminated line: there is no newline to round the cut to. The cap
    // must still win — honouring the line boundary at the cost of unbounded memory would
    // defeat the only reason the cap exists.
    LogSink sink;
    sink.SetMaxPending(100);
    sink.Write(std::wstring(10000, L'x'));
    EXPECT_TRUE(sink.PendingChars() <= size_t{100});
}

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------

TEST(LogSink, ConcurrentProducersLoseNothing) {
    // Four producers, a fixed total, drained repeatedly by "the UI thread" while they run.
    // The invariant is conservation: every character written is either drained or still
    // pending, never lost. A missing chunk here is the classic swap-vs-append race.
    LogSink sink;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 500;

    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&sink] {
            for (int i = 0; i < kPerThread; ++i) sink.WriteLine(L"0123456789");
        });
    }

    size_t drained = 0;
    // Drain concurrently with the producers, which is what makes this a race test rather
    // than a "join then read" test.
    for (int spin = 0; spin < 2000; ++spin) {
        std::wstring out;
        if (sink.Drain(out)) drained += out.size();
    }
    for (std::thread& t : producers) t.join();
    std::wstring rest;
    if (sink.Drain(rest)) drained += rest.size();

    EXPECT_EQ(drained, size_t{kThreads * kPerThread * 11});
    EXPECT_EQ(sink.PendingChars(), size_t{0});
}

TEST(LogSink, ConcurrentWakeupNeverStrandsData) {
    // The failure this guards: a producer appends between Drain()'s swap and its re-arm,
    // sees the slot still claimed, skips its wakeup — and its data sits staged with
    // nobody told to come get it. In an app that is a log view that stops updating until
    // some unrelated later write happens to arrive.
    //
    // Expressed as: after everything settles, if data is pending then a wakeup is
    // outstanding for it.
    LogSink sink;
    std::atomic<bool> stop{false};
    std::atomic<int> wakeups{0};
    sink.SetWakeup([&wakeups] { wakeups.fetch_add(1, std::memory_order_relaxed); });

    std::thread producer([&sink, &stop] {
        for (int i = 0; i < 5000 && !stop.load(std::memory_order_relaxed); ++i)
            sink.WriteLine(L"data");
    });
    for (int spin = 0; spin < 5000; ++spin) {
        std::wstring out;
        sink.Drain(out);
    }
    producer.join();

    // Final state check. Whatever the interleaving produced, this must hold.
    if (sink.PendingChars() > 0) EXPECT_TRUE(sink.WakeupPending());
    // And the coalescing must have actually coalesced: 5000 writes must not have produced
    // 5000 wakeups. (The exact count depends on the interleaving, hence the loose bound —
    // a per-write wakeup would land at ~5000.)
    EXPECT_TRUE(wakeups.load() < 5000);
}
