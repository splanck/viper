#!/usr/bin/env python3
"""
Audit C/C++ files for extended headers and basic API doc comments.

Checks:
- File-level header: within first 12 lines, must contain one of:
  - LLVM-style banner with "Part of the Viper project"
  - "// File:" marker
  - Doxygen "@file" marker
- Prototype doc: for C headers under src/runtime/, each function prototype
  should have a preceding comment with '///' or a block comment.

This is a best-effort heuristic; multi-line declarations are handled by only
evaluating the first line that starts the declaration.
"""
from __future__ import annotations
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def has_file_header(path: pathlib.Path) -> bool:
    try:
        head = path.read_text(errors="ignore").splitlines()[:12]
    except Exception:
        return True
    for line in head:
        s = line.strip()
        if (
            "Part of the Viper project" in s
            or s.startswith("// File:")
            or s.startswith("/// @file")
        ):
            return True
    return False


def iter_runtime_prototypes(path: pathlib.Path):
    lines = path.read_text(errors="ignore").splitlines()
    proto_re = re.compile(r"^[^/].*\)\s*;\s*$")
    i = 0
    while i < len(lines):
        line = lines[i]
        if proto_re.match(line):
            # Skip obvious in-body statements (e.g., 'return foo();')
            if 'return ' in line or '=' in line:
                i += 1
                continue
            # Only flag if this looks like the start of a declaration (no leading comma)
            if not line.strip().startswith(","):
                prev = "\n".join(lines[max(0, i - 3) : i])
                has_doc = ("///" in prev) or ("/*" in prev)
                if not has_doc:
                    yield i + 1, line.strip()
        i += 1


def main() -> int:
    # Use git ls-files for tracked files
    import subprocess

    out = subprocess.check_output(["git", "ls-files"], cwd=ROOT).decode().splitlines()
    paths = [ROOT / p for p in out if re.search(r"\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$", p)]

    missing_headers = []
    for p in paths:
        try:
            head = p.read_text(errors="ignore").splitlines()[:3]
        except Exception:
            head = []
        if any("Generated file" in l for l in head):
            continue
        if not has_file_header(p):
            missing_headers.append(p)

    runtime_missing_proto_docs = []
    for p in paths:
        # Limit prototype doc audit to the C runtime headers only
        if p.as_posix().startswith((ROOT / "src" / "runtime").as_posix()) and p.suffix in {".h", ".hpp"}:
            for line_no, sig in iter_runtime_prototypes(p):
                runtime_missing_proto_docs.append((p, line_no, sig))

    print(f"Files missing file-level header: {len(missing_headers)}")
    for p in missing_headers[:50]:
        print(f"  - {p.relative_to(ROOT)}")

    print(f"Runtime headers prototypes lacking doc comments: {len(runtime_missing_proto_docs)}")
    for p, n, sig in runtime_missing_proto_docs[:50]:
        print(f"  - {p.relative_to(ROOT)}:{n}: {sig}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
