#!/usr/bin/env python3
"""
Copy a cpp_utils project header (and its transitive cross-project dependencies)
into a flat destination folder, rewriting relative #include paths.

Usage:
    python copy_util.py <project> <dest>

Examples:
    python copy_util.py logger ~/my_project/include
    python copy_util.py argparser /tmp/headers
"""

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).parent

# Matches cross-project relative includes: #include "../something/file.hxx"  // optional comment
CROSS_INCLUDE_RE = re.compile(r'(#include\s+)"(\.\.[^"]+)"(\s*//[^\n]*)?')


def find_headers(project: str) -> list[Path]:
    folder = REPO_ROOT / project
    if not folder.is_dir():
        sys.exit(f"error: project folder '{project}' not found in {REPO_ROOT}")
    headers = sorted(folder.glob("*.hxx"))
    if not headers:
        sys.exit(f"error: no .hxx file found in {folder}")
    return headers


def collect(header: Path, seen: set[Path]) -> list[Path]:
    """Recursively collect header and all cross-project deps, deps-first order."""
    if header in seen:
        return []
    seen.add(header)
    result: list[Path] = []
    for m in CROSS_INCLUDE_RE.finditer(header.read_text()):
        dep = (header.parent / m.group(2)).resolve()
        result.extend(collect(dep, seen))
    result.append(header)
    return result


def rewrite(text: str) -> str:
    """Replace '../something/file.hxx' with 'file.hxx' in include directives."""
    return CROSS_INCLUDE_RE.sub(
        lambda m: f'{m.group(1)}"{Path(m.group(2)).name}"', text
    )


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "project", nargs="+", help="project folder name(s) (e.g. logger timer)"
    )
    ap.add_argument("dest", help="destination folder path")
    args = ap.parse_args()

    dest = Path(args.dest)
    dest.mkdir(parents=True, exist_ok=True)

    seen: set[Path] = set()
    all_headers: list[Path] = []
    for project in args.project:
        for h in find_headers(project):
            all_headers.extend(collect(h, seen))

    for src in all_headers:
        dst = dest / src.name
        dst.write_text(rewrite(src.read_text()))
        print(f"  {src.relative_to(REPO_ROOT)}  ->  {dst}")

    print(f"\nDone. {len(all_headers)} file(s) copied to {dest}")


if __name__ == "__main__":
    main()
