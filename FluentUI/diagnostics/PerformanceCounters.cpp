// PerformanceCounters.cpp — see PerformanceCounters.h.
//
// Only ProcessStats::Sample needs a .cpp: it pulls in psapi.h and the GDI/USER
// object-count APIs, which we keep out of the widely-included header. Everything
// else (FrameStats, ScopedTimer, QPC helpers) is header-only and inline.

#include "PerformanceCounters.h"

#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace fluent {

bool ProcessStats::Sample() {
    HANDLE proc = GetCurrentProcess();

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(proc,
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc))) {
        return false;
    }
    workingSetBytes = pmc.WorkingSetSize;
    privateBytes = pmc.PrivateUsage;

    gdiObjects = GetGuiResources(proc, GR_GDIOBJECTS);
    userObjects = GetGuiResources(proc, GR_USEROBJECTS);
    return true;
}

} // namespace fluent
