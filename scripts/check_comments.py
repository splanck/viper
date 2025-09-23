#!/usr/bin/env python3
# File: scripts/check_comments.py
# Purpose: Verify file headers and Doxygen comments across the repository.

"""Utility script to enforce repository comment standards.

This tool scans tracked source files and ensures that each contains the
expected file header and, for headers, that declarations are documented with
Doxygen comments. It is intended to run in CI to keep comment conventions
consistent across the codebase.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from textwrap import dedent
from typing import Iterable, List, Optional

ROOT = Path(__file__).resolve().parent.parent
IGNORE_FILE = Path(__file__).with_name('comment_check_excludes.txt')

DECL_RE = re.compile(r'^\s*(class|struct|enum)\s+\w+|^\s*[^/\n][^\n]*\(.*\)\s*(const)?\s*;')

LICENSE_PATTERNS: tuple[str, ...] = (
    'spdx-license-identifier',
    'permission is hereby granted',
    'apache license',
    'gnu general public license',
    'redistribution and use in source and binary forms',
    'mit license',
    'bsd license',
)


@dataclass
class CommentBlock:
    """Representation of a contiguous comment block."""

    kind: str
    text: str
    start_line: int
    end_line: int


def list_files() -> List[Path]:
    """Return repository source files subject to comment checks."""

    res = subprocess.run(
        ['git', 'ls-files'],
        stdout=subprocess.PIPE,
        text=True,
        check=True,
        cwd=ROOT,
    )
    files = [line.strip() for line in res.stdout.splitlines()]
    files = [f for f in files if f.endswith(('.hpp', '.c', '.cpp', '.cxx'))]
    ignored: set[str] = set()
    if IGNORE_FILE.exists():
        ignored = {
            line.strip()
            for line in IGNORE_FILE.read_text().splitlines()
            if line.strip()
        }
    return [Path(f) for f in files if f not in ignored]


def resolve_targets(patterns: Iterable[str]) -> List[Path]:
    """Resolve user-specified file or glob targets relative to the repo root."""

    resolved: set[Path] = set()
    for pattern in patterns:
        candidate = Path(pattern)
        matches: List[Path] = []
        if candidate.is_absolute():
            if candidate.is_file():
                matches.append(candidate)
        else:
            matches.extend((ROOT / p) for p in ROOT.glob(pattern) if (ROOT / p).is_file())
            as_relative = ROOT / candidate
            if as_relative.is_file():
                matches.append(as_relative)
            elif candidate.exists() and candidate.is_file():
                matches.append(candidate.resolve())
        for match in matches:
            resolved.add(match.resolve())
    return sorted(resolved)


def is_license_block(text: str) -> bool:
    """Return ``True`` when ``text`` matches a known license snippet."""

    lowered = text.lower()
    return any(pattern in lowered for pattern in LICENSE_PATTERNS)


def is_valid_header(text: str, *, allow_doxygen: bool = True, strict: bool = False) -> bool:
    """Determine if ``text`` represents a valid file header comment."""

    lowered = text.lower()
    has_file = 'file:' in lowered
    has_purpose = 'purpose:' in lowered
    if has_file and has_purpose:
        return True
    if strict:
        return False
    if not allow_doxygen:
        return False
    return any(token in lowered for token in ('@file', '\\file', '@brief', '\\brief'))


def _extract_comment_block(
    lines: List[str],
    index: int,
    *,
    shebang_seen: bool,
    allow_yaml: bool,
) -> tuple[Optional[CommentBlock], int]:
    """Return the comment block starting at ``index`` or ``None``."""

    stripped = lines[index].lstrip()
    if allow_yaml and lines[index].strip() == '---':
        start = index
        j = index + 1
        block_lines = [lines[index]]
        while j < len(lines):
            block_lines.append(lines[j])
            if lines[j].strip() == '---':
                j += 1
                break
            j += 1
        else:
            j = len(lines)
        return CommentBlock('yaml', '\n'.join(block_lines), start + 1, j), j
    if stripped.startswith('//'):
        start = index
        j = index
        block_lines: List[str] = []
        while j < len(lines):
            current = lines[j].lstrip()
            if current.startswith('//'):
                block_lines.append(lines[j])
                j += 1
                continue
            break
        return CommentBlock('c_line', '\n'.join(block_lines), start + 1, j), j
    if stripped.startswith('/*'):
        start = index
        block_lines = [lines[index]]
        j = index + 1
        if '*/' not in lines[index]:
            while j < len(lines):
                block_lines.append(lines[j])
                if '*/' in lines[j]:
                    j += 1
                    break
                j += 1
            else:
                j = len(lines)
        return CommentBlock('c_block', '\n'.join(block_lines), start + 1, j), j
    if stripped.startswith('<!--'):
        start = index
        block_lines = [lines[index]]
        j = index + 1
        while j < len(lines):
            block_lines.append(lines[j])
            if '-->' in lines[j]:
                j += 1
                break
            j += 1
        else:
            j = len(lines)
        return CommentBlock('xml', '\n'.join(block_lines), start + 1, j), j
    if stripped.startswith('#'):
        allow_hash = shebang_seen or len(stripped) == 1 or stripped[1].isspace() or stripped[1] in ('!',)
        if allow_hash:
            start = index
            j = index
            block_lines = []
            while j < len(lines):
                current = lines[j].lstrip()
                if current.startswith('#'):
                    condition = shebang_seen or len(current) == 1 or current[1].isspace() or current[1] in ('!',)
                    if condition:
                        block_lines.append(lines[j])
                        j += 1
                        continue
                break
            return CommentBlock('hash', '\n'.join(block_lines), start + 1, j), j
    return None, index


def _find_next_block(
    lines: List[str],
    start_idx: int,
    window: int,
    *,
    shebang_seen: bool,
    allow_yaml: bool,
) -> tuple[Optional[CommentBlock], int, bool]:
    """Locate the next comment block starting within the search window."""

    limit = min(len(lines), start_idx + window)
    i = start_idx
    if start_idx == 0 and not shebang_seen and lines and lines[0].startswith('#!'):
        shebang_seen = True
        i = 1
        while i < len(lines) and i < limit and lines[i].strip() == '':
            i += 1
    else:
        while i < len(lines) and i < limit and lines[i].strip() == '':
            i += 1
    while i < len(lines) and i < limit:
        block, next_index = _extract_comment_block(
            lines,
            i,
            shebang_seen=shebang_seen,
            allow_yaml=allow_yaml,
        )
        if block:
            return block, next_index, shebang_seen
        if lines[i].strip() == '':
            i += 1
            continue
        break
    return None, start_idx, shebang_seen


def read_first_comment_blocks(lines: List[str], window: int) -> List[CommentBlock]:
    """Read up to two leading comment blocks within the ``window``."""

    blocks: List[CommentBlock] = []
    start_idx = 0
    shebang_seen = False
    allow_yaml = True
    while len(blocks) < 2:
        block, next_start, shebang_seen = _find_next_block(
            lines,
            start_idx,
            window,
            shebang_seen=shebang_seen,
            allow_yaml=allow_yaml,
        )
        if not block:
            break
        blocks.append(block)
        if len(blocks) == 1 and not is_license_block(block.text):
            break
        start_idx = next_start
        allow_yaml = False
        if start_idx >= len(lines):
            break
    return blocks


def file_has_valid_header(
    lines: List[str],
    *,
    window: int,
    allow_doxygen: bool,
    strict: bool,
) -> bool:
    """Determine whether the supplied ``lines`` contain a valid file header."""

    blocks = read_first_comment_blocks(lines, window)
    for block in blocks:
        if is_license_block(block.text):
            continue
        return is_valid_header(block.text, allow_doxygen=allow_doxygen, strict=strict)
    return False


def check_doxygen(lines: List[str]) -> List[int]:
    """Identify declarations lacking a preceding Doxygen comment."""

    missing: List[int] = []
    for idx, line in enumerate(lines):
        if DECL_RE.match(line):
            j = idx - 1
            while j >= 0 and lines[j].strip() == '':
                j -= 1
            if j < 0 or not lines[j].lstrip().startswith('///'):
                missing.append(idx + 1)
    return missing


def load_lines(path: Path) -> List[str]:
    """Read ``path`` as UTF-8 text returning a list of lines."""

    return path.read_text(encoding='utf-8', errors='ignore').splitlines()


def run_selftest() -> None:
    """Execute the built-in sample based regression tests."""

    import tempfile

    samples = [
        (
            'spdx_then_doxygen.cpp',
            """
            // SPDX-License-Identifier: MIT
            // Copyright (c) 2025
            /**
             * @file
             * @brief Demo
             */
            int main(){}
            """,
            True,
        ),
        (
            'mit_then_file_purpose.cpp',
            """
            /* Permission is hereby granted, free of charge, ... */
            // File: mit_then_file_purpose.cpp
            // Purpose: test header after license
            int main(){}
            """,
            True,
        ),
        (
            'brief_only.cpp',
            """
            // @brief Small demo
            int main(){}
            """,
            True,
        ),
        (
            'no_header.cpp',
            """
            // just a comment not a header
            int main(){}
            """,
            False,
        ),
        (
            'yaml_front_matter.md',
            """
            ---
            title: Foo
            SPDX-License-Identifier: Apache-2.0
            ---
            <!-- @brief Doc page -->
            # Hello
            """,
            True,
        ),
        (
            'shebang_then_hash.py',
            """
            #!/usr/bin/env python3
            # @file tool
            print("ok")
            """,
            True,
        ),
    ]

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        for name, contents, expected in samples:
            path = tmp_path / name
            text = dedent(contents).strip('\n') + '\n'
            path.write_text(text, encoding='utf-8')
            lines = load_lines(path)
            result = file_has_valid_header(
                lines,
                window=30,
                allow_doxygen=True,
                strict=False,
            )
            assert result == expected, f"{name} expected {expected} but got {result}"
    print('Selftest passed.')


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    """Parse command-line arguments."""

    parser = argparse.ArgumentParser(description='Check file headers and Doxygen usage.')
    parser.add_argument('targets', nargs='*', help='Files or glob patterns to check.')
    parser.add_argument('--window', type=int, default=30, help='Number of lines to scan when searching for headers.')
    parser.add_argument('--strict', action='store_true', help='Require both File: and Purpose: markers even if Doxygen tags are present.')
    parser.add_argument(
        '--allow-doxygen',
        dest='allow_doxygen',
        action='store_true',
        default=True,
        help='Allow @file/@brief (or \\file/\\brief) headers to satisfy the check.',
    )
    parser.add_argument(
        '--no-allow-doxygen',
        dest='allow_doxygen',
        action='store_false',
        help='Disallow Doxygen-only headers.',
    )
    parser.add_argument('--selftest', action='store_true', help='Run the built-in smoke tests and exit.')
    return parser.parse_args(argv)


def iter_files(targets: Iterable[str]) -> List[Path]:
    """Return absolute paths for files to process."""

    if not targets:
        return [ROOT / rel for rel in list_files()]
    return resolve_targets(targets)


def format_relative(path: Path) -> str:
    """Return ``path`` relative to ``ROOT`` when possible."""

    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path.resolve())


def main(argv: Optional[List[str]] = None) -> int:
    """Run comment checks across the repository."""

    args = parse_args(argv)
    if args.selftest:
        run_selftest()
        return 0

    files = iter_files(args.targets)
    if not files:
        return 0

    missing_headers: List[str] = []
    missing_docs: List[tuple[str, List[int]]] = []

    for path in files:
        lines = load_lines(path)
        if not file_has_valid_header(
            lines,
            window=args.window,
            allow_doxygen=args.allow_doxygen,
            strict=args.strict,
        ):
            missing_headers.append(format_relative(path))
        if path.suffix == '.hpp':
            missing = check_doxygen(lines)
            if missing:
                missing_docs.append((format_relative(path), missing))

    if missing_headers or missing_docs:
        if missing_headers:
            print('Files missing headers:')
            for item in missing_headers:
                print(f'  {item}')
        if missing_docs:
            print('\nDeclarations missing Doxygen comments:')
            for name, lines in missing_docs:
                line_list = ', '.join(str(num) for num in lines)
                print(f'  {name}: lines {line_list}')
        return 1

    print('All files have headers and documented declarations.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
