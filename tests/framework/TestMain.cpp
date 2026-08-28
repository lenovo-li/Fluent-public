// TestMain.cpp — entry point for the FluentUI native test runner.
//
// Runs every registered test (optionally filtered by a substring passed as the
// first command-line argument, matched against "Suite.Name"), prints a summary,
// and returns 0 if all tests passed, 1 otherwise.
//
//   FluentUITests.exe            run all tests
//   FluentUITests.exe Grid       run tests whose "Suite.Name" contains "Grid"

#include "Test.h"

#include <cstring>

int main(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    auto& cases = fltest::Registry();
    int total = 0, passed = 0, failed = 0, skipped = 0;

    std::printf("=== FluentUI tests: %zu registered ===\n", cases.size());

    for (const auto& tc : cases) {
        std::string full = std::string(tc.suite) + "." + tc.name;
        if (filter && full.find(filter) == std::string::npos) {
            skipped++;
            continue;
        }
        total++;

        auto& st = fltest::Current();
        st.failures = 0;
        st.suiteDotName = full;

        std::printf("[ RUN  ] %s\n", full.c_str());
        try {
            tc.fn();
        } catch (const std::exception& ex) {
            fltest::ReportFailure(__FILE__, __LINE__,
                                  std::string("unhandled exception: ") + ex.what());
        } catch (...) {
            fltest::ReportFailure(__FILE__, __LINE__, "unhandled unknown exception");
        }

        if (st.failures == 0) {
            std::printf("[  OK  ] %s\n", full.c_str());
            passed++;
        } else {
            std::printf("[ FAIL ] %s (%d failure%s)\n", full.c_str(),
                        st.failures, st.failures == 1 ? "" : "s");
            failed++;
        }
    }

    std::printf("=== summary: %d run, %d passed, %d failed", total, passed, failed);
    if (skipped) std::printf(", %d skipped by filter", skipped);
    std::printf(" ===\n");

    return failed == 0 ? 0 : 1;
}
