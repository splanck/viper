#!/usr/bin/env python3
# File: scripts/check_comments.py
# Purpose: Verify file headers and Doxygen comments across the repository.

"""Utility script to enforce repository comment standards.

This tool scans tracked source files and ensures that each contains the
expected file header and, for headers, that declarations are documented with
Doxygen comments. It is intended to run in CI to keep comment conventions
consistent across the codebase.
"""

import re
import subprocess
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
IGNORE_FILE = Path(__file__).with_name('comment_check_excludes.txt')

FILE_HEADER_RE = re.compile(r'//\s*File:')
PURPOSE_RE = re.compile(r'//\s*Purpose:')
DECL_RE = re.compile(r'^\s*(class|struct|enum)\s+\w+|^\s*[^/\n][^\n]*\(.*\)\s*(const)?\s*;')


def list_files():
    """Return repository source files subject to comment checks.

    The function lists all tracked files in the git repository, filters them to
    source file extensions, and removes any paths listed in
    ``comment_check_excludes.txt``.

    Returns:
        list[str]: Paths of files to check relative to the repository root.
    """
    res = subprocess.run(['git', 'ls-files'], stdout=subprocess.PIPE, text=True, check=True, cwd=ROOT)
    files = [line.strip() for line in res.stdout.splitlines()]
    files = [f for f in files if f.endswith(('.hpp', '.c', '.cpp', '.cxx'))]
    ignored = set()
    if IGNORE_FILE.exists():
        ignored = {line.strip() for line in IGNORE_FILE.read_text().splitlines() if line.strip()}
    return [f for f in files if f not in ignored]


def has_file_header(lines):
    """Check whether a file contains the expected header comment.

    Args:
        lines (list[str]): Lines of the file to examine.

    Returns:
        bool: ``True`` if both the ``File:`` and ``Purpose:`` markers appear in
        the first ten lines, ``False`` otherwise.
    """
    top = lines[:10]
    has_file = any(FILE_HEADER_RE.search(l) for l in top)
    has_purpose = any(PURPOSE_RE.search(l) for l in top)
    return has_file and has_purpose


def check_doxygen(lines):
    """Identify declarations lacking a preceding Doxygen comment.

    Args:
        lines (list[str]): Lines from a header file to scan.

    Returns:
        list[int]: Line numbers of declarations without a ``///`` comment
        immediately above them.
    """
    missing = []
    for idx, line in enumerate(lines):
        if DECL_RE.match(line):
            j = idx - 1
            while j >= 0 and lines[j].strip() == '':
                j -= 1
            if j < 0 or not lines[j].lstrip().startswith('///'):
                missing.append(idx + 1)
    return missing


def main():
    """Run comment checks across the repository.

    Returns:
        int: ``0`` if all files pass the checks, ``1`` otherwise.
    """
    files = list_files()
    missing_headers = []
    missing_docs = []
    for f in files:
        path = ROOT / f
        lines = path.read_text().splitlines()
        if not has_file_header(lines):
            missing_headers.append(f)
        if path.suffix == '.hpp':
            missing = check_doxygen(lines)
            if missing:
                missing_docs.append((f, missing))
    if missing_headers or missing_docs:
        if missing_headers:
            print('Files missing headers:')
            for f in missing_headers:
                print(f'  {f}')
        if missing_docs:
            print('\nDeclarations missing Doxygen comments:')
            for f, lines in missing_docs:
                line_list = ', '.join(str(l) for l in lines)
                print(f'  {f}: lines {line_list}')
        return 1
    print('All files have headers and documented declarations.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
