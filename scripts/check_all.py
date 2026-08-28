#!/usr/bin/env python3
# scripts/check_all.py — run every repo-hygiene check in one shot.
#
# Exit code 0 = all checks passed. Non-zero = the number of checks that failed;
# each check's own output is printed inline above the summary.
#
# Usage (from repo root):
#   python scripts/check_all.py
#   python scripts/check_all.py --verbose   # pass --verbose through to each check
#
# WHY THIS EXISTS. check_source_lists.py had been sitting in scripts/ with
# "Intended for CI" in its header and nothing calling it — a checker nobody runs
# is documentation with extra steps. This is the single entry point, so adding a
# check means appending one line here rather than hoping the next person
# remembers a script exists. There is no CI and no git hook in this repo; run this
# before committing a change that touches project files or the source lists.
#
# These are all fast, read-only, pure-Python checks: no compiler, no MSBuild, no
# network. They catch the class of mistake that keeps the build green while
# quietly breaking something else — a source registered in one build system but
# not the other, a file filed under the wrong Solution Explorer folder. They do
# NOT replace the real verification convention, which stays what project documentation says:
# MSBuild Debug + Release clean, then x64/Debug/FluentUITests.exe fully green.
#
# TO ADD A CHECK: write scripts/check_<thing>.py taking --verbose and returning 0
# on success, then add it to CHECKS below with a one-line description.

import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# (script filename, what it verifies)
CHECKS = [
    ("check_source_lists.py",
     "CMakeLists.txt and FluentUI.vcxproj list the same sources"),
    ("check_filters.py",
     "Solution Explorer tree mirrors the on-disk folder layout"),
]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run all repo-hygiene checks.")
    parser.add_argument("--verbose", action="store_true",
                        help="Pass --verbose to each check")
    args = parser.parse_args()

    results = []
    for script, description in CHECKS:
        path = REPO_ROOT / "scripts" / script
        print(f"=== {script} - {description}")
        if not path.exists():
            print(f"  ERROR: {path} not found\n")
            results.append((script, False))
            continue
        # Flush before handing the console to the child: our stdout is block-
        # buffered when redirected, the child writes straight through, and without
        # this every header lands after the output it is supposed to introduce.
        sys.stdout.flush()
        cmd = [sys.executable, str(path)] + (["--verbose"] if args.verbose else [])
        # cwd=REPO_ROOT so a check may resolve paths relative to the repo root.
        rc = subprocess.run(cmd, cwd=REPO_ROOT).returncode
        results.append((script, rc == 0))
        print()

    failed = [s for s, ok in results if not ok]
    width = max(len(s) for s, _ in results)
    print("=== summary")
    for script, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {script.ljust(width)}")
    if failed:
        print(f"\nFAILED - {len(failed)} of {len(results)} check(s): "
              f"{', '.join(failed)}")
        return len(failed)
    print(f"\nOK - all {len(results)} check(s) passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
