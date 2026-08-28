// BenchMain.cpp — FluentUIBench entry point (roadmap §18.2).
//
// Runs the registered benchmark scenes (optionally filtered by a substring
// passed as argv[1], matched against the scene name) and prints a table of
// results.
//
//   FluentUIBench.exe                run every scene, report numbers, exit 0
//   FluentUIBench.exe Layout         run scenes whose name contains "Layout"
//   FluentUIBench.exe --gate         FAIL (exit 1) if a scene exceeds its budget
//   FluentUIBench.exe --gate Expander  both
//
// WHY --gate EXISTS. Printing a number is not a test: nobody notices a scene drifting
// from 3% of a frame to 30% until a user on slower hardware reports stutter. A scene
// that declares a budget gets checked against it, and the process exit code carries the
// verdict so CI (or a pre-commit run) can refuse the regression.
//
// The budget is a FRACTION OF A FRAME, not a millisecond count, because a millisecond
// threshold means different things on different machines: 4.17 ms is a whole frame at
// 240 Hz and a quarter of one at 60 Hz. Scenes fill in Result::budgetFraction and the
// runner compares against the measured share of THIS display's frame interval — so the
// same gate is meaningful on a fast desktop and a slow laptop, which is the whole point
// when the developer's own machine is too fast to feel the problem.
//
// Gating is opt-in per scene (budgetFraction == 0 means "report only"), and off by
// default for the whole run so that an exploratory `FluentUIBench.exe` never fails.
//
// The scenes themselves live in Scenes.cpp. This file only owns the registry
// storage (Scenes()) and the runner/printer.

#include "Bench.h"

#include <cstdio>
#include <cstring>
#include <Windows.h>   // EnumDisplaySettingsW, for this display's frame interval

namespace bench {
std::vector<Scene>& Scenes() {
    static std::vector<Scene> scenes;
    return scenes;
}
} // namespace bench

namespace {

// This display's frame interval. Same source as NativeWindowHost::RefreshHz and the
// ExpanderReveal scene, so the gate and the app agree on what "a frame" is.
double FrameIntervalMs(int* hzOut) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) &&
        dm.dmDisplayFrequency > 1) {
        if (hzOut) *hzOut = static_cast<int>(dm.dmDisplayFrequency);
        return 1000.0 / static_cast<double>(dm.dmDisplayFrequency);
    }
    if (hzOut) *hzOut = 0;
    return 1000.0 / 60.0;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace bench;

    bool gate = false;
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gate") == 0) gate = true;
        else if (filter == nullptr) filter = argv[i];
    }

    int hz = 0;
    const double frameMs = FrameIntervalMs(&hz);

    std::printf("=== FluentUIBench: %zu scenes registered ===\n", Scenes().size());
    if (filter) std::printf("(filter: \"%s\")\n", filter);
    if (hz > 0) std::printf("(display: %d Hz -> %.2f ms per frame)\n", hz, frameMs);
    else        std::printf("(display refresh unknown; assuming 60 Hz -> 16.67 ms)\n");
    if (gate)   std::printf("(gate: ON — scenes with a declared budget can fail)\n");
    std::printf("\n");

    int ran = 0, stubs = 0, gated = 0, failed = 0;
    for (const Scene& s : Scenes()) {
        if (filter && std::strstr(s.name, filter) == nullptr) continue;

        std::printf("### %s\n    %s\n", s.name, s.description);
        Result r = s.run();
        if (r.measured) {
            std::printf("    result : %.3f ms\n", r.primaryMs);
            ran++;
        } else {
            std::printf("    result : (not measured here)\n");
            stubs++;
        }
        if (!r.metric.empty()) std::printf("    metric : %s\n", r.metric.c_str());
        if (!r.note.empty())   std::printf("    note   : %s\n", r.note.c_str());

        if (r.measured && r.budgetFraction > 0.0 && r.perFrameMs > 0.0) {
            const double share = r.perFrameMs / frameMs;
            const bool over = share > r.budgetFraction;
            gated++;
            std::printf("    budget : %.4f%% of a frame used, limit %.2f%% -> %s\n",
                        share * 100.0, r.budgetFraction * 100.0,
                        over ? "OVER BUDGET" : "ok");
            if (over && gate) {
                std::printf("    FAIL   : %s exceeds its per-frame budget on this "
                            "display (%d Hz). On slower hardware this is visible "
                            "stutter.\n", s.name, hz > 0 ? hz : 60);
                failed++;
            }
        }
        std::printf("\n");
    }

    std::printf("=== FluentUIBench done: %d measured, %d GPU/interactive stubs, "
                "%d budgeted, %d over budget ===\n", ran, stubs, gated, failed);
    // Non-zero only under --gate, so an exploratory run never breaks a build.
    return (gate && failed > 0) ? 1 : 0;
}
