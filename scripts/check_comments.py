#!/usr/bin/env python3
# File: scripts/check_comments.py
# Purpose: Verify file headers and Doxygen comments across the repository.

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
    res = subprocess.run(['git', 'ls-files'], stdout=subprocess.PIPE, text=True, check=True, cwd=ROOT)
    files = [line.strip() for line in res.stdout.splitlines()]
    files = [f for f in files if f.endswith(('.hpp', '.c', '.cpp', '.cxx'))]
    ignored = set()
    if IGNORE_FILE.exists():
        ignored = {line.strip() for line in IGNORE_FILE.read_text().splitlines() if line.strip()}
    return [f for f in files if f not in ignored]


def has_file_header(lines):
    top = lines[:10]
    has_file = any(FILE_HEADER_RE.search(l) for l in top)
    has_purpose = any(PURPOSE_RE.search(l) for l in top)
    return has_file and has_purpose


def check_doxygen(lines):
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
