#!/usr/bin/env python3
# scripts/check_filters.py — verify that Visual Studio's Solution Explorer tree
# mirrors the folder layout on disk.
#
# Exit code 0 = no drift. Non-zero = something would show up in the wrong place
# in Solution Explorer; output names the file and both paths.
#
# Usage (from repo root):
#   python scripts/check_filters.py
#   python scripts/check_filters.py --verbose   # also print per-project counts
#
# WHY THIS EXISTS. A .vcxproj lists files by path; the sibling .vcxproj.filters
# independently says which Solution Explorer folder each one appears in. Nothing
# ties the two together — MSBuild will happily build a project whose filters file
# claims graphics/DCompHost.h lives under "controls", and the build stays green
# while the tree users browse by becomes fiction. project documentation states the convention
# ("Solution Explorer must mirror the folder layout on disk"); this is the check
# that makes it more than a note. It held with zero exceptions across all
# registered entries when written, so any failure here is new drift.
#
# WHAT IT CHECKS, per project that has a .filters file:
#   1. Every file in the .vcxproj also appears in the .filters. A file missing
#      from the filters file silently collapses to the project root in the tree.
#   2. Every file's <Filter> equals its own directory, with '\' separators.
#      controls/primitives/ButtonBase.h  =>  <Filter>controls\primitives</Filter>
#      A project-root file (fl_common.h) must have no <Filter> element at all.
#   3. Every filter path used by a file is declared as a <Filter Include="...">,
#      including intermediate levels ("controls" must exist for
#      "controls\primitives" to nest under it, which is what VS itself writes).
#   4. Every file listed in the .filters is still listed in the .vcxproj — a
#      leftover entry from a deleted or renamed file shows up as a phantom node.
#   5. <UniqueIdentifier> GUIDs are distinct; duplicates make VS merge or drop
#      folders unpredictably.
#
# Projects with no .filters file are reported as skipped, not failed: with every
# source at the project root (FluentSettings, FluentUIBench) there is no tree to
# get wrong. A file in a subdirectory without a filters file IS flagged, since
# that is the case where the flat tree actively misleads.
#
# Orphan filter declarations — a <Filter Include> nothing references — are printed
# as a warning but do not fail the check: an empty folder in the tree is untidy,
# not wrong, and it is a normal intermediate state while adding a subsystem.

import argparse
import io
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Item types that carry a <Filter>. This repo only uses these two; None /
# ResourceCompile / Manifest / Image would behave identically if added later.
ITEM_TAGS = ("ClCompile", "ClInclude", "None", "ResourceCompile", "Manifest", "Image")
_TAGS = "|".join(ITEM_TAGS)


def _read(path: Path) -> str:
    """Read a project file. utf-8-sig because MSBuild writes a BOM."""
    return io.open(path, encoding="utf-8-sig").read()


def _norm(p: str) -> str:
    """Normalise a project-relative path for comparison: forward slashes, lower
    case. Windows paths are case-insensitive and MSBuild accepts either
    separator, so neither difference is real drift."""
    return p.strip().replace("\\", "/").lower()


def vcxproj_items(path: Path) -> set[str]:
    """Every file the .vcxproj lists, normalised."""
    text = _read(path)
    return {
        _norm(m.group(1))
        for m in re.finditer(rf'<(?:{_TAGS})\s+Include="([^"]+)"', text)
    }


def filters_file(path: Path):
    """Parse a .vcxproj.filters.

    Returns (items, declared, guids) where
      items    = {normalised path: filter string as written, or '' for none}
      declared = {normalised filter path: filter string as written}
      guids    = [(filter string, guid string)] in file order
    """
    text = _read(path)

    declared = {}
    guids = []
    for m in re.finditer(
        r'<Filter\s+Include="([^"]+)"\s*>(.*?)</Filter>', text, re.DOTALL
    ):
        name = m.group(1).strip()
        declared[_norm(name)] = name
        g = re.search(r"<UniqueIdentifier>([^<]*)</UniqueIdentifier>", m.group(2))
        guids.append((name, g.group(1).strip().lower() if g else ""))

    items = {}
    # Paired form: <ClInclude Include="x"> ... <Filter>y</Filter> ... </ClInclude>
    for m in re.finditer(
        rf'<({_TAGS})\s+Include="([^"]+)"\s*>(.*?)</\1>', text, re.DOTALL
    ):
        f = re.search(r"<Filter>([^<]*)</Filter>", m.group(3))
        items[_norm(m.group(2))] = f.group(1).strip() if f else ""
    # Self-closing form: <ClInclude Include="x" />  — no filter, i.e. project root.
    for m in re.finditer(rf'<(?:{_TAGS})\s+Include="([^"]+)"\s*/>', text):
        items.setdefault(_norm(m.group(1)), "")

    return items, declared, guids


def check_project(vcxproj: Path, verbose: bool) -> tuple[int, int]:
    """Check one project. Returns (error_count, checked_item_count)."""
    rel = vcxproj.relative_to(REPO_ROOT).as_posix()
    filters = vcxproj.with_suffix(vcxproj.suffix + ".filters")
    items = vcxproj_items(vcxproj)

    if not filters.exists():
        # No tree to get wrong if everything sits at the project root.
        nested = sorted(p for p in items if "/" in p)
        if nested:
            print(f"ERROR [{rel}]: no .filters file, but these are in subdirectories:")
            for p in nested:
                print(f"    {p}")
            print("    Solution Explorer will show them flat at the project root.")
            return len(nested), len(items)
        if verbose:
            print(f"  skip  {rel} - no .filters, all {len(items)} item(s) at project root")
        return 0, len(items)

    fitems, declared, guids = filters_file(filters)
    errors = []

    # (1) + (2): every project item present, and filed under its own directory.
    for p in sorted(items):
        expected_dir = p.rsplit("/", 1)[0] if "/" in p else ""
        if p not in fitems:
            where = expected_dir.replace("/", "\\") or "<project root>"
            errors.append(
                f"  {p}\n"
                f"      missing from .filters - expected <Filter>{where}</Filter>"
            )
            continue
        actual = fitems[p]
        if _norm(actual) != expected_dir:
            errors.append(
                f"  {p}\n"
                f"      filter    = {actual or '<none, i.e. project root>'}\n"
                f"      disk dir  = {expected_dir.replace('/', chr(92)) or '<project root>'}"
            )

    # (3): every filter a file uses is declared, including intermediate levels.
    used = set()
    for p, f in fitems.items():
        if not f:
            continue
        parts = _norm(f).split("/")
        for i in range(len(parts)):
            used.add("/".join(parts[: i + 1]))
    for f in sorted(used - set(declared)):
        errors.append(
            f"  <Filter Include=\"{f.replace('/', chr(92))}\"> is used but never declared\n"
            f"      add it with a fresh <UniqueIdentifier> GUID"
        )

    # (4): no leftover entries for files the project no longer lists.
    for p in sorted(set(fitems) - items):
        errors.append(
            f"  {p}\n"
            f"      listed in .filters but NOT in the .vcxproj - phantom tree node"
        )

    # (5): GUIDs must be distinct.
    seen = {}
    for name, guid in guids:
        if not guid:
            errors.append(f"  <Filter Include=\"{name}\"> has no <UniqueIdentifier>")
        elif guid in seen:
            errors.append(
                f"  <Filter Include=\"{name}\"> reuses the GUID of "
                f"\"{seen[guid]}\" ({guid})"
            )
        else:
            seen[guid] = name

    if errors:
        print(f"ERROR [{rel}]: Solution Explorer does not mirror the folder layout\n")
        for e in errors:
            print(e)
        print()
    elif verbose:
        orphans = sorted(set(declared) - used)
        note = f", {len(orphans)} empty folder(s)" if orphans else ""
        print(f"  ok    {rel} - {len(items)} item(s), {len(declared)} filter(s){note}")
        for f in orphans:
            print(f"          warning: filter \"{declared[f]}\" contains no files")

    return len(errors), len(items)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that .vcxproj.filters mirrors the on-disk folder layout."
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Print per-project results on success"
    )
    args = parser.parse_args()

    projects = sorted(
        p for p in REPO_ROOT.glob("*/*.vcxproj") if "x64" not in p.parts
    )
    if not projects:
        print("ERROR: no .vcxproj files found under the repo root")
        return 1

    total_errors = 0
    total_items = 0
    for proj in projects:
        errs, items = check_project(proj, args.verbose)
        total_errors += errs
        total_items += items

    if total_errors:
        print(
            f"FAILED - {total_errors} problem(s) across {len(projects)} project(s).\n"
            "Fix: make each file's <Filter> equal its directory (backslashes), "
            "declare every filter level, and keep .vcxproj and .filters in sync."
        )
        return 1

    print(
        f"OK - {total_items} item(s) across {len(projects)} project(s); "
        "Solution Explorer mirrors the folder layout"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
