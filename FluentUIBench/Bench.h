// Bench.h — tiny benchmark-scene registry for FluentUIBench (roadmap §18.2).
//
// FluentUIBench is the standalone performance harness the roadmap requires. It
// hosts the ten scenarios from §18.2. WP-00's job is to make the harness exist,
// register every scenario, and actually run the ones whose cost is pure CPU /
// logic (layout, dirty-flag propagation, active-animation ticking, measure
// cache, idle detection). Scenarios that need a live swap chain / GPU (visual
// stress, DPI lab, input latency) are registered as stubs that print exactly
// what a real Windows+GPU run must measure — the roadmap forbids faking GPU
// numbers (§20.4), so those print "requires GPU" instead of a fabricated value.
//
// A scene is a name + a function that runs it and fills a Result. `main` runs
// all scenes (or a substring-filtered subset) and prints a table. Keeping this
// header-only and dependency-light means the harness links against FluentUI.lib
// and nothing else.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bench {

// Outcome of one scene run. `measured` is false for GPU-dependent stubs so the
// summary can show "—" instead of a misleading 0.
struct Result {
    bool measured = true;
    double primaryMs = 0.0;      // headline timing (meaning is per-scene)
    std::string metric;          // human-readable metric line(s)
    std::string note;            // caveats / "requires GPU" etc.

    // --- Optional regression gate (checked only under --gate) ------------------
    //
    // A scene that can express its cost as a share of one frame fills these in and the
    // runner fails the process when the share exceeds the budget. Left at 0 the scene is
    // report-only, which is right for the scenes whose primaryMs is a total over many
    // iterations rather than a per-frame cost.
    //
    // Expressed as a FRACTION OF A FRAME rather than milliseconds on purpose: a
    // millisecond threshold silently means something different on every refresh rate
    // (4.17 ms is an entire frame at 240 Hz, a quarter of one at 60 Hz), so a ms-based
    // gate tuned on one machine is wrong on the next. A fraction travels.
    double perFrameMs = 0.0;       // measured cost of ONE frame's worth of this work
    double budgetFraction = 0.0;   // e.g. 0.10 = must stay under 10% of a frame
};

struct Scene {
    const char* name;
    const char* description;
    std::function<Result()> run;
};

// The global scene table. Scenes register themselves at static-init time via the
// REGISTER_SCENE macro below.
std::vector<Scene>& Scenes();

struct SceneRegistrar {
    SceneRegistrar(const char* name, const char* desc, std::function<Result()> fn) {
        Scenes().push_back({name, desc, std::move(fn)});
    }
};

#define BENCH_CONCAT_(a, b) a##b
#define BENCH_CONCAT(a, b) BENCH_CONCAT_(a, b)
#define REGISTER_SCENE(name, desc, fn) \
    static ::bench::SceneRegistrar BENCH_CONCAT(reg_, __LINE__){name, desc, fn}

} // namespace bench
