#!/usr/bin/env python3
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
il_version = (ROOT / "IL_VERSION").read_text(encoding="utf-8").strip()

# IL files that intentionally violate headers for negative tests (skip)
SKIP = {
    "tests/il/parse/duplicate_version.il",
    "tests/unit/test_il_parse_duplicate_version.cpp",
    "tests/unit/test_il_parse_missing_version.cpp",
    "tests/unit/test_il_parse_bom.cpp",
    "tests/unit/test_il_comments.cpp",
}

HEADER_RE = re.compile(r"^(\s*il\s+)([^\s;#]+)(.*)$")

changed = 0
for p in ROOT.rglob("*.il"):
    rel = p.relative_to(ROOT).as_posix()
    # Never rewrite test fixtures
    if rel.startswith("tests/"):
        continue
    if any(s in rel for s in SKIP):
        continue
    txt = p.read_text(encoding="utf-8")
    lines = txt.splitlines()
    if not lines:
        continue
    m = HEADER_RE.match(lines[0])
    if not m:
        continue
    prefix, ver, rest = m.groups()
    if ver != il_version:
        lines[0] = f"{prefix}{il_version}{rest}"
        p.write_text("\n".join(lines) + ("\n" if txt.endswith("\n") else ""), encoding="utf-8")
        changed += 1

print(f"Updated {changed} IL files to 'il {il_version}'")
