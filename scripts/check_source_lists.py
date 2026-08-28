#!/usr/bin/env python3
# scripts/check_source_lists.py — verify that FLUENTUI_SOURCES in CMakeLists.txt
# and the <ClCompile> entries in FluentUI/FluentUI.vcxproj list the same files.
#
# Exit code 0 = no drift. Non-zero = lists disagree; output names the gap.
#
# Usage (from repo root):
#   python scripts/check_source_lists.py
#   python scripts/check_source_lists.py --verbose   # also print matched count
#
# Intended for CI; see plan.md §4.6.
#
# WHY THIS EXISTS. CMakeLists.txt and FluentUI.vcxproj are two independent hand-
# maintained source lists. project documentation says "new sources must be registered in
# vcxproj — there is no glob", and CMakeLists.txt now imposes the same discipline
# for the CMake path. Without a check, it is easy to add a .cpp to one and forget
# the other; the CMake build silently uses only its own list, and the VS solution
# quietly includes a dangling item (or vice-versa), so neither consumer is correct.
#
# WHAT IT CHECKS. Only .cpp files that are already relative to the FluentUI/
# directory. Header-only additions (like WindowRegistry.h) do not appear in either
# list, so they produce no false positives. The comparison is path-normalised
# (forward/backward slashes, case-folded) so platform differences don't matter.

import argparse
import io
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def cmake_sources(root: Path) -> set[str]:
    """Return the set of .cpp paths from FLUENTUI_SOURCES in CMakeLists.txt,
    normalised to forward-slash paths relative to the FluentUI/ directory."""
    text = io.open(root / "CMakeLists.txt", encoding="utf-8").read()
    m = re.search(r"set\(FLUENTUI_SOURCES\s*(.*?)\)", text, re.DOTALL)
    if not m:
        raise ValueError("Could not find set(FLUENTUI_SOURCES ...) in CMakeLists.txt")
    result = set()
    for line in m.group(1).splitlines():
        s = line.strip()
        if not s:
            continue
        # CMakeLists entries look like "FluentUI/animation/Animation.cpp"; strip prefix.
        if s.startswith("FluentUI/"):
            s = s[len("FluentUI/"):]
        # Normalise separators and case for comparison.
        result.add(s.replace("\\", "/").lower())
    return result


def vcxproj_sources(root: Path) -> set[str]:
    """Return the set of .cpp paths from ClCompile entries in FluentUI.vcxproj,
    normalised to forward-slash paths relative to the FluentUI/ directory."""
    text = io.open(root / "FluentUI" / "FluentUI.vcxproj",
                   encoding="utf-8-sig").read()
    result = set()
    for m in re.finditer(r'<ClCompile Include="([^"]+)"', text):
        p = m.group(1).strip()
        result.add(p.replace("\\", "/").lower())
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that CMakeLists.txt and FluentUI.vcxproj list the same sources.")
    parser.add_argument("--verbose", action="store_true",
                        help="Print matched file count even on success")
    args = parser.parse_args()

    cmake = cmake_sources(REPO_ROOT)
    vcx   = vcxproj_sources(REPO_ROOT)

    only_cmake = sorted(cmake - vcx)
    only_vcx   = sorted(vcx - cmake)

    if not only_cmake and not only_vcx:
        if args.verbose:
            print(f"OK — {len(cmake)} sources match between CMakeLists.txt and FluentUI.vcxproj")
        return 0

    print("ERROR: source list drift detected between CMakeLists.txt and FluentUI.vcxproj\n")
    if only_cmake:
        print("In CMakeLists.txt FLUENTUI_SOURCES but NOT in FluentUI.vcxproj:")
        for f in only_cmake:
            print(f"  {f}")
    if only_vcx:
        print("In FluentUI.vcxproj but NOT in CMakeLists.txt FLUENTUI_SOURCES:")
        for f in only_vcx:
            print(f"  {f}")
    print("\nFix: add the missing entry to the other list, then re-run this check.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
