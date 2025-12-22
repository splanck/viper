#!/usr/bin/env python3
"""
Runtime sweep runner for ViperLang and BASIC tests.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
import time
import pty
import select
import codecs


DEFAULT_TIMEOUT_S = 10.0
EXPECT_RE = re.compile(r"^\s*(//|'|REM)\s*EXPECT_([A-Z0-9_]+)\s*:\s*(.*)$", re.IGNORECASE)
COVER_RE = re.compile(r"^\s*(//|'|REM)\s*COVER:\s*(.*)$", re.IGNORECASE)
CLASS_RE = re.compile(r"^-\s+\[[ xX]\]\s+`([^`]+)`\s*$")
CONSTRUCTOR_RE = re.compile(r"^\s+-\s+\[[ xX]\]\s+Constructor:\s+`([^`]+)`\s*$")
MEMBER_RE = re.compile(r"^\s+-\s+\[[ xX]\]\s+`([^`]+)`\s*$")


def decode_escapes(value: str) -> str:
    return codecs.decode(value, "unicode_escape")


def parse_directives(path: str) -> dict:
    directives = {
        "expect_exit": 0,
        "expect_out": [],
        "expect_err": [],
        "stdin": "",
        "tty_input": "",
        "args": [],
        "timeout_s": DEFAULT_TIMEOUT_S,
    }
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            match = EXPECT_RE.match(line)
            if not match:
                continue
            key = match.group(2).strip().upper()
            value = match.group(3).strip()
            if key == "EXIT":
                directives["expect_exit"] = int(value)
            elif key == "OUT":
                directives["expect_out"].append(value)
            elif key == "ERR":
                directives["expect_err"].append(value)
            elif key == "STDIN":
                directives["stdin"] += decode_escapes(value) + "\n"
            elif key == "TTY_INPUT":
                directives["tty_input"] += decode_escapes(value)
            elif key == "ARGS":
                directives["args"] = shlex.split(value)
            elif key == "TIMEOUT_MS":
                directives["timeout_s"] = int(value) / 1000.0
    return directives


def extract_coverage(path: str) -> list[str]:
    covers = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            match = COVER_RE.match(line)
            if not match:
                continue
            covers.append(match.group(2).strip())
    return covers


def run_with_stdio(cmd: list[str], stdin_text: str, timeout_s: float) -> tuple[int, str, str]:
    try:
        result = subprocess.run(
            cmd,
            input=stdin_text.encode("utf-8"),
            capture_output=True,
            timeout=timeout_s,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    stdout = result.stdout.decode("utf-8", errors="replace")
    stderr = result.stderr.decode("utf-8", errors="replace")
    return result.returncode, stdout, stderr


def run_with_pty(cmd: list[str], input_text: str, timeout_s: float) -> tuple[int, str, str]:
    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True,
    )
    os.close(slave_fd)

    output = bytearray()
    start = time.time()
    input_bytes = input_text.encode("utf-8") if input_text else b""
    if input_bytes:
        os.write(master_fd, input_bytes)

    while True:
        if time.time() - start > timeout_s:
            proc.kill()
            os.close(master_fd)
            return 124, output.decode("utf-8", errors="replace"), "timeout"

        rlist, _, _ = select.select([master_fd], [], [], 0.1)
        if master_fd in rlist:
            try:
                chunk = os.read(master_fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)

        if proc.poll() is not None and not rlist:
            break

    os.close(master_fd)
    return proc.returncode, output.decode("utf-8", errors="replace"), ""


def run_test(lang: str, path: str, directives: dict, ilc_path: str) -> dict:
    cmd = [ilc_path, "front", lang, "-run", path]
    if directives["args"]:
        cmd.append("--")
        cmd.extend(directives["args"])

    timeout_s = directives["timeout_s"]
    if directives["tty_input"]:
        exit_code, stdout, stderr = run_with_pty(cmd, directives["tty_input"], timeout_s)
    else:
        exit_code, stdout, stderr = run_with_stdio(cmd, directives["stdin"], timeout_s)

    return {
        "path": path,
        "lang": lang,
        "exit_code": exit_code,
        "stdout": stdout,
        "stderr": stderr,
    }


def check_expectations(result: dict, directives: dict) -> tuple[bool, list[str]]:
    ok = True
    notes = []

    if result["exit_code"] != directives["expect_exit"]:
        ok = False
        notes.append(
            f"exit {result['exit_code']} != expected {directives['expect_exit']}"
        )

    for needle in directives["expect_out"]:
        if needle not in result["stdout"]:
            ok = False
            notes.append(f"stdout missing: {needle}")

    for needle in directives["expect_err"]:
        if needle not in result["stderr"]:
            ok = False
            notes.append(f"stderr missing: {needle}")

    return ok, notes


def update_checklist(checklist_path: str, covered: set[str]) -> None:
    with open(checklist_path, "r", encoding="utf-8") as handle:
        lines = handle.readlines()

    class_stack = []
    class_children = {}
    class_line_idx = {}

    for i, line in enumerate(lines):
        class_match = CLASS_RE.match(line)
        if class_match:
            class_name = class_match.group(1)
            class_stack = [class_name]
            class_children.setdefault(class_name, []).append(i)
            class_line_idx[class_name] = i
            continue

        if not class_stack:
            continue

        constructor_match = CONSTRUCTOR_RE.match(line)
        if constructor_match:
            ctor = constructor_match.group(1)
            if ctor in covered:
                lines[i] = line.replace("[ ]", "[x]")
            class_children[class_stack[0]].append(i)
            continue

        member_match = MEMBER_RE.match(line)
        if member_match:
            member = member_match.group(1)
            class_name = class_stack[0]
            if f"{class_name}.{member}" in covered:
                lines[i] = line.replace("[ ]", "[x]")
            class_children[class_name].append(i)

    for class_name, indices in class_children.items():
        if not indices:
            continue
        if all("[x]" in lines[idx] or "[X]" in lines[idx] for idx in indices[1:]):
            line_idx = class_line_idx.get(class_name)
            if line_idx is not None:
                lines[line_idx] = lines[line_idx].replace("[ ]", "[x]")

    with open(checklist_path, "w", encoding="utf-8") as handle:
        handle.writelines(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Viper runtime sweep tests.")
    parser.add_argument("--update-checklist", action="store_true")
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
    ilc_path = os.path.join(repo_root, "build", "src", "tools", "ilc", "ilc")
    if not os.path.exists(ilc_path):
        print(f"error: ilc not found at {ilc_path}", file=sys.stderr)
        return 1

    viperlang_dir = os.path.join(repo_root, "tests", "viperlang_runtime")
    basic_dir = os.path.join(repo_root, "tests", "runtime_sweep", "basic")

    tests = []
    for root, _, files in os.walk(viperlang_dir):
        for name in sorted(files):
            if not name.endswith(".viper") or name.startswith("_"):
                continue
            tests.append(("viperlang", os.path.join(root, name)))
        break

    for root, _, files in os.walk(basic_dir):
        for name in sorted(files):
            if name.endswith(".bas"):
                tests.append(("basic", os.path.join(root, name)))
        break

    results = []
    covered = set()

    for lang, path in tests:
        directives = parse_directives(path)
        result = run_test(lang, path, directives, ilc_path)
        ok, notes = check_expectations(result, directives)
        results.append((result, ok, notes))
        if ok:
            covered.update(extract_coverage(path))

    failed = [r for r in results if not r[1]]
    if args.update_checklist:
        checklist_path = os.path.join(repo_root, "bugs", "runtime_test.md")
        update_checklist(checklist_path, covered)

    for result, ok, notes in results:
        status = "PASS" if ok else "FAIL"
        print(f"{status} {result['lang']} {os.path.relpath(result['path'], repo_root)}")
        if not ok:
            for note in notes:
                print(f"  - {note}")
            if result["stdout"].strip():
                print("  - stdout:")
                for line in result["stdout"].splitlines()[:10]:
                    print(f"      {line}")
            if result["stderr"].strip():
                print("  - stderr:")
                for line in result["stderr"].splitlines()[:10]:
                    print(f"      {line}")

    print(f"\nTotal: {len(results)}  Passed: {len(results) - len(failed)}  Failed: {len(failed)}")
    return 0 if not failed else 1


if __name__ == "__main__":
    raise SystemExit(main())
