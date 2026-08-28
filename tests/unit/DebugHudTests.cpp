// DebugHudTests.cpp — unit tests for the pure HUD formatting (roadmap §18.3).
// No window/GPU: FormatHudLines/FormatFrameReason/FpsFromMs are pure functions.

#include "../framework/Test.h"
#include "../../FluentUI/diagnostics/DebugHud.h"

#include <string>

using namespace fluent;

// FPS is 1000 / frame-time-ms; 0 ms guards against divide-by-zero -> 0.
TEST(DebugHud, FpsFromMs) {
    EXPECT_NEAR(FpsFromMs(16.6667f), 60.0f, 0.1f);
    EXPECT_NEAR(FpsFromMs(6.9444f), 144.0f, 0.5f);
    EXPECT_NEAR(FpsFromMs(0.0f), 0.0f, 0.001f);   // no data, not a spike
    EXPECT_NEAR(FpsFromMs(-1.0f), 0.0f, 0.001f);  // guard non-positive
}

// Empty reason -> "-"; single + multiple bits decode in bit order.
TEST(DebugHud, FormatFrameReason) {
    EXPECT_TRUE(FormatFrameReason(0) == "-");
    EXPECT_TRUE(FormatFrameReason(1u << 1) == "Anim");      // Animation
    EXPECT_TRUE(FormatFrameReason(1u << 3) == "Paint");
    // Input(0) | Anim(1) | Paint(3) -> ordered "Input|Anim|Paint".
    EXPECT_TRUE(FormatFrameReason((1u << 0) | (1u << 1) | (1u << 3)) ==
                "Input|Anim|Paint");
}

// FormatHudLines produces 6 lines and embeds the key numbers.
TEST(DebugHud, FormatHudLinesShape) {
    FrameStats f;
    f.cpuFrameMs = 2.5;
    f.layoutMs = 0.3;
    f.renderMs = 1.5;
    f.presentMs = 0.4;
    f.resizeMs = 0.7;
    f.drawOps = 128;
    f.dirtyElements = 3;
    f.dirtyRects = 1;
    f.activeAnimations = 2;
    f.frameReason = (1u << 1) | (1u << 3);  // Anim|Paint

    HudPercentiles iv;
    iv.p50Ms = 16.6f; iv.p95Ms = 17.2f; iv.p99Ms = 22.0f; iv.maxMs = 34.0f;
    HudCpuPercentiles cpu;
    cpu.p50Ms = 2.10f; cpu.p99Ms = 8.40f; cpu.maxMs = 34.0f;

    const std::string s = FormatHudLines(f, iv, cpu);

    // Six lines (five '\n' separators). Was seven while async layout had its own
    // counter line; that machinery is gone, so the line is too.
    int newlines = 0;
    for (char c : s) if (c == '\n') ++newlines;
    EXPECT_EQ(newlines, 5);

    // Contains FPS (~60 from p50 16.6), the reason tag, and a work count.
    EXPECT_TRUE(s.find("FPS 60") != std::string::npos);
    EXPECT_TRUE(s.find("Anim|Paint") != std::string::npos);
    EXPECT_TRUE(s.find("draw 128") != std::string::npos);
    EXPECT_TRUE(s.find("anim 2") != std::string::npos);
    // The layout/render/present split is present (resize-diagnosis line).
    EXPECT_TRUE(s.find("lay 0.30") != std::string::npos);
    EXPECT_TRUE(s.find("ren 1.50") != std::string::npos);
    EXPECT_TRUE(s.find("pre 0.40") != std::string::npos);
    // ApplyPendingResize's cost, which runs outside cpuFrameMs.
    EXPECT_TRUE(s.find("rsz 0.70") != std::string::npos);
}

// The CPU percentile line is what makes a BIMODAL frame-time distribution visible.
// This is the regression that produced "1000+ fps" on a stuttering resize drag: a
// burst of trivial frames dominates the interval median, so FPS alone reports the
// cheap mode. P99/Max come from the CPU ring and cannot be pulled down by the burst.
TEST(DebugHud, CpuPercentilesExposeABimodalDistribution) {
    FrameStats f;
    f.cpuFrameMs = 0.8;   // this frame happened to be one of the cheap ones

    // Interval median sits in the cheap mode -> a four-digit FPS reading.
    HudPercentiles iv;
    iv.p50Ms = 0.8f; iv.p95Ms = 0.9f; iv.p99Ms = 910.0f; iv.maxMs = 940.0f;
    // The CPU ring still carries the expensive mode at the top end.
    HudCpuPercentiles cpu;
    cpu.p50Ms = 0.80f; cpu.p99Ms = 905.0f; cpu.maxMs = 938.0f;

    const std::string s = FormatHudLines(f, iv, cpu);

    // The misleading number is still shown (it is the real cadence of those frames)...
    EXPECT_TRUE(s.find("FPS 1250") != std::string::npos);
    // ...but the 900ms mode is now on screen next to it, in both rings.
    EXPECT_TRUE(s.find("P99 905.00") != std::string::npos);
    EXPECT_TRUE(s.find("Max 938.0 ms") != std::string::npos);
}

// Percentiles default to zero when the host passes none, so a caller that has not
// wired the CPU ring yet still formats (and reads as "no data", not a spike).
TEST(DebugHud, CpuPercentilesDefaultToZero) {
    FrameStats f;
    HudPercentiles iv;
    iv.p50Ms = 16.6f;
    const std::string s = FormatHudLines(f, iv);
    EXPECT_TRUE(s.find("CPU P50 0.00  P99 0.00  Max 0.0 ms") != std::string::npos);
}
