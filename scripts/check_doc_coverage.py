#!/usr/bin/env python3
"""Report Doxygen documentation coverage for the first-party C++ reference.

Coverage counts class/struct/union/enum types and documented declaration
members (functions, typedefs, variables, macros, and enum values) in include/,
src/, and tests/. Private members are included because Doxygen extracts them
for the project's implementation reference. A declaration is documented when
it has non-empty brief or detailed Doxygen text. The script uses Doxygen XML,
so it audits the same parsed source used to publish the API reference.
"""
from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

FIRST_PARTY = ("include/", "src/", "tests/")
TYPE_KINDS = {"class", "struct", "union", "interface"}
MEMBER_KINDS = {"function", "variable", "typedef", "define", "enumvalue", "friend", "property", "concept"}


def content(node: ET.Element | None) -> str:
    if node is None:
        return ""
    return " ".join(" ".join(node.itertext()).split())


def location(node: ET.Element) -> tuple[str, str]:
    loc = node.find("location")
    if loc is None:
        return "", ""
    file = loc.get("file", "").replace("\\", "/")
    line = loc.get("line", "")
    return file, line


def documented(node: ET.Element) -> bool:
    return bool(content(node.find("briefdescription")) or content(node.find("detaileddescription")))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--xml-dir", type=Path, default=Path(".pio/doxygen/xml"))
    ap.add_argument("--report", type=Path, help="Write Markdown report to this file (also prints it).")
    ap.add_argument("--fail-under", type=float, default=100.0, help="Exit nonzero below this percentage (default: 100).")
    args = ap.parse_args()
    index = args.xml_dir / "index.xml"
    if not index.is_file():
        print(f"Doxygen XML index not found: {index}", file=sys.stderr)
        return 2

    root = ET.parse(index).getroot()
    seen: set[str] = set()
    entries: list[tuple[str, str, str, bool]] = []
    for compound in root.findall("compound"):
        refid = compound.get("refid", "")
        xml = args.xml_dir / f"{refid}.xml"
        if not xml.is_file():
            continue
        try:
            comp_root = ET.parse(xml).getroot()
        except ET.ParseError as exc:
            print(f"Invalid Doxygen XML {xml}: {exc}", file=sys.stderr)
            return 2
        comp = comp_root.find("compounddef")
        if comp is None:
            continue
        kind = comp.get("kind", compound.get("kind", ""))
        name = content(comp.find("compoundname")) or content(compound.find("name"))
        file, line = location(comp)
        if kind in TYPE_KINDS and file.startswith(FIRST_PARTY) and refid not in seen:
            seen.add(refid)
            entries.append((kind, name, file, documented(comp)))
        # Compound members may be repeated in file compounds; member ids dedupe.
        for member in comp.findall(".//memberdef"):
            mid = member.get("id", "")
            if not mid or mid in seen or member.get("kind") not in MEMBER_KINDS:
                continue
            mfile, mline = location(member)
            if not mfile.startswith(FIRST_PARTY):
                continue
            prot = member.get("prot", "public")
            # Public reference counts public/protected declarations and all
            # free functions. Private fields/methods remain in the full source
            # but are implementation details, not public API docs.
            if prot == "private":
                continue
            seen.add(mid)
            mname = content(member.find("qualifiedname")) or content(member.find("name"))
            entries.append((member.get("kind", "member"), mname, mfile, documented(member)))

    total = len(entries)
    done = sum(e[3] for e in entries)
    pct = (100.0 * done / total) if total else 0.0
    missing = sorted((kind, name, file) for kind, name, file, ok in entries if not ok)
    lines = [
        "# C++ documentation coverage",
        "",
        f"**Coverage: {done}/{total} ({pct:.2f}%)**",
        "",
        "Scope: Doxygen-parsed first-party declarations in `include/`, `src/`, and `tests/`. "
        "Types and public/protected members are counted; private implementation members are excluded. "
        "A declaration is covered when its Doxygen brief or detailed description is non-empty.",
        "",
    ]
    if missing:
        lines += ["## Missing documentation", "", "| Kind | Symbol | File |", "|---|---|---|"]
        lines.extend(f"| `{kind}` | `{name}` | `{file}` |" for kind, name, file in missing)
    else:
        lines += ["All declarations in scope have Doxygen descriptions."]
    report = "\n".join(lines) + "\n"
    print(report, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    return 1 if pct < args.fail_under else 0


if __name__ == "__main__":
    raise SystemExit(main())
