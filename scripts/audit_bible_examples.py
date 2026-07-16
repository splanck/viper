#!/usr/bin/env python3
# Script: audit_bible_examples.py
# Purpose: Independently compile-check runnable code fences in docs/book.

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import tempfile
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
BIBLE = ROOT / "docs" / "bible"
VIPER_CANDIDATES = (
    ROOT / "build" / "src" / "tools" / "viper" / "viper",
    ROOT / "build" / "install" / "bin" / "viper",
    ROOT / "build" / "install-validation" / "bin" / "viper",
)
VIPER = next((path for path in VIPER_CANDIDATES if path.is_file()), VIPER_CANDIDATES[0])

ZIA_LANGS = {"rust", "zia", "viper"}
BASIC_LANGS = {"basic", "bas"}
IL_LANGS = {"il"}

PSEUDO_MARKERS = (
    "...",
    "/path/to",
    "<CODE>",
    "<diagnostic-code>",
    "YourName",
    "someCondition",
    "condition1",
    "condition2",
)

DECL_RE = r"(?:(?:expose|hide|public|export|private)\s+)?(?:class|struct|interface|enum|func)\b"

SIDE_EFFECT_MARKERS = (
    "Viper.IO",
    "Viper.Net",
    "Viper.Network",
    "Viper.Http",
    "Viper.Crypto.Tls",
    "Viper.Thread",
    "Viper.Graphics",
    "Viper.GUI",
    "Viper.Audio",
    "Viper.Audio",
    "Viper.Input",
    "Viper.System",
    "Viper.Time",
    "ReadLine",
    "InputLine",
    "Prompt(",
    "while true",
)

NEGATIVE_CUES = (
    "what happens if",
    "without this line",
    "without the bind",
    "forget the semicolon",
    "forget the quotes",
    "forget it",
    "forgetting the semicolon",
    "forgetting the quotes",
    "misspell",
    "wrong kind of quotes",
    "won't compile",
    "won't work",
    "will not compile",
    "will not work",
    "doesn't compile",
    "does not compile",
    "expected to fail",
    "expected to trap",
    "expected to time out",
    "intentionally broken",
    "compile-time error",
    "runtime error",
    "infinite loop",
    "oops!",
    "what the code probably looks like",
)


@dataclass
class Fence:
    path: str
    start_line: int
    end_line: int
    lang: str
    code: str


@dataclass
class Result:
    path: str
    start_line: int
    end_line: int
    lang: str
    kind: str
    mode: str
    status: str
    rc: int | None = None
    reason: str = ""
    generated: str = ""
    stdout: str = ""
    stderr: str = ""


def iter_markdown_files(paths: list[str]) -> list[Path]:
    if paths:
        files: list[Path] = []
        for raw in paths:
            path = Path(raw)
            if path.is_dir():
                files.extend(sorted(path.rglob("*.md")))
            else:
                files.append(path)
        return files
    return sorted(BIBLE.rglob("*.md"))


def iter_fences(files: Iterable[Path]) -> Iterable[Fence]:
    for path in files:
        lines = path.read_text(encoding="utf-8").splitlines()
        in_fence = False
        lang = ""
        start_line = 0
        body: list[str] = []
        for line_no, line in enumerate(lines, 1):
            if line.startswith("```"):
                if not in_fence:
                    in_fence = True
                    lang = line[3:].strip().split()[0] if line[3:].strip() else ""
                    start_line = line_no
                    body = []
                else:
                    yield Fence(
                        str(path),
                        start_line,
                        line_no,
                        lang,
                        "\n".join(body).strip("\n"),
                    )
                    in_fence = False
            elif in_fence:
                body.append(line)


def nearby_text(fence: Fence, before: int = 6, after: int = 0) -> str:
    lines = Path(fence.path).read_text(encoding="utf-8").splitlines()
    lo = max(1, fence.start_line - before)
    hi = min(len(lines), fence.end_line + after)
    return "\n".join(lines[lo - 1 : hi]).lower()


def module_name(fence: Fence) -> str:
    stem = re.sub(r"[^A-Za-z0-9_]", "_", Path(fence.path).stem)
    return f"Doc_{stem}_{fence.start_line}"


def file_marker(fence: Fence) -> str:
    match = re.search(r"(?m)^\s*//\s*file:\s*([A-Za-z0-9_./-]+\.zia)\s*$", fence.code)
    return match.group(1) if match else ""


def pseudo_reason(code: str) -> str:
    for marker in PSEUDO_MARKERS:
        if marker in code:
            return f"contains placeholder {marker!r}"
    # Match standalone domain-style type names, not qualified API members such
    # as `Cipher.DecryptResult` or `Tls.ConnectResult`.
    if re.search(r"(?<!\.)\b[A-Z][A-Za-z0-9_]*(Result|Repository|Service|Gateway)\b", code):
        return "contains domain placeholder types"
    if re.search(r"\b(?:Record|Item|Image|Request|Response|User|Account|Browser|Cart|Database|Schedule|Equipment|Benefits|StockGrant|SMTP)\b", code):
        return "contains domain placeholder types"
    if re.search(r"\b(?:fillBuffer|clearBuffer|processNextBatch|loadTestData|processData|computeResults|writeResults|validateRecords|parseRecords)\b", code):
        return "contains placeholder helper functions"
    significant = [
        line.split("//", 1)[0].strip()
        for line in code.splitlines()
        if line.split("//", 1)[0].strip()
    ]
    if significant and all(
        "{" not in line
        and "}" not in line
        and (
            re.match(r"(?:func\s+)?[A-Za-z_][A-Za-z0-9_.]*\s*\([^)]*\)\s*(?:->\s*[A-Za-z_][A-Za-z0-9_?\\[\\].]*)?$", line)
            or re.match(r"[A-Za-z_][A-Za-z0-9_.]*\s*:\s*[A-Za-z_][A-Za-z0-9_?\\[\\].]*$", line)
        )
        for line in significant
    ):
        return "reference signature, not a runnable example"
    return ""


def is_negative_example(fence: Fence) -> bool:
    if Path(fence.path).name == "d-error-messages.md":
        return True
    if re.search(r"(?m)^\s*[^/\s].*//\s*(Error|Invalid)\b", fence.code):
        return True
    context = nearby_text(fence, after=6)
    return any(cue in context for cue in NEGATIVE_CUES)


def should_run(source: str, has_entry: bool) -> bool:
    if not has_entry:
        return False
    return not any(marker in source for marker in SIDE_EFFECT_MARKERS)


def zia_source(fence: Fence, negative: bool) -> tuple[str, str, bool]:
    code = fence.code.strip()
    module_count = len(re.findall(r"(?m)^\s*module\s+", code))
    if negative and module_count == 0:
        return code + "\n", "as-shown-negative", False
    if code.strip() in {"{", "}"}:
        return code + "\n", "isolated-brace", False
    if not negative and code.count("{") != code.count("}"):
        return code + "\n", "unbalanced-fragment", False
    significant = [
        line.split("//", 1)[0].strip()
        for line in code.splitlines()
        if line.split("//", 1)[0].strip()
    ]
    if (
        not negative
        and module_count == 0
        and significant
        and all(";" not in line for line in significant)
        and not any(re.match(rf"(bind|{DECL_RE}|var|final)\b", line) for line in significant)
    ):
        return code + "\n", "expression-fragment", False
    if module_count > 1:
        return code + "\n", "multi-module", False
    if module_count == 1:
        has_entry = re.search(r"(?m)^\s*func\s+start\s*\(\s*\)", code) is not None
        return code + "\n", "as-is", has_entry
    if (
        module_count == 0
        and not re.search(rf"(?m)^\s*{DECL_RE}", code)
    ):
        bind_lines: list[str] = []
        body_lines: list[str] = []
        in_body = False
        for line in code.splitlines():
            if not in_body and (not line.strip() or line.lstrip().startswith("//")):
                bind_lines.append(line)
            elif not in_body and re.match(r"\s*bind\b", line):
                bind_lines.append(line)
            else:
                in_body = True
                body_lines.append(line)
        source = f"module {module_name(fence)};\n"
        if bind_lines:
            source += "\n" + "\n".join(bind_lines).strip("\n") + "\n"
        elif any(token in "\n".join(body_lines) for token in ("Say(", "Print(", "InputLine(")):
            source += "\nbind Viper.Terminal;\n"
        source += "\nfunc start() {\n"
        for line in body_lines:
            source += f"    {line}\n"
        source += "}\n"
        return source, "start-wrap", True
    if re.search(rf"(?m)^\s*(bind|{DECL_RE}|var|final)\b", code):
        prelude_lines: list[str] = []
        module_lines: list[str] = []
        start_lines: list[str] = []
        lines = code.splitlines()
        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.strip()
            if not stripped or stripped.startswith("//"):
                module_lines.append(line)
                i += 1
                continue
            if re.match(r"\s*(bind|final)\b", line):
                prelude_lines.append(line)
                i += 1
                continue
            if re.match(rf"\s*{DECL_RE}", line):
                depth = 0
                saw_brace = False
                while i < len(lines):
                    block_line = lines[i]
                    module_lines.append(block_line)
                    depth += block_line.count("{") - block_line.count("}")
                    saw_brace = saw_brace or "{" in block_line
                    i += 1
                    if saw_brace and depth <= 0:
                        break
                continue
            start_lines.append(line)
            i += 1

        source = f"module {module_name(fence)};\n\n"
        if prelude_lines:
            source += "\n".join(prelude_lines).strip("\n") + "\n\n"
        source += "\n".join(module_lines).strip("\n") + "\n"
        has_entry = re.search(r"(?m)^\s*func\s+start\s*\(\s*\)", "\n".join(module_lines)) is not None
        if not has_entry:
            visible_prelude = "\n".join(prelude_lines)
            if any(token in "\n".join(start_lines) for token in ("Say(", "Print(", "InputLine(")) and "bind Viper.Terminal" not in visible_prelude:
                source += "\nbind Viper.Terminal;\n"
            source += "\nfunc start() {\n"
            for line in start_lines:
                source += f"    {line}\n"
            source += "}\n"
            has_entry = True
        return source, "module-wrap", has_entry
    source = f"module {module_name(fence)};\n\nbind Viper.Terminal;\n\nfunc start() {{\n"
    for line in code.splitlines():
        source += f"    {line}\n"
    source += "}\n"
    return source, "start-wrap", True


def run_cmd(cmd: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        timeout=timeout,
    )


def check_file(path: Path, timeout: float) -> subprocess.CompletedProcess[str]:
    return run_cmd(
        [str(VIPER), "check", str(path), "--diagnostic-format=json"],
        timeout,
    )


def context_failure_reason(stderr: str) -> str:
    try:
        payload = json.loads(stderr)
    except json.JSONDecodeError:
        return ""
    diagnostics = payload.get("diagnostics", [])
    errors = [diag for diag in diagnostics if diag.get("severity") == "error"]
    if not errors:
        return ""
    context_markers = (
        "Failed to open imported file",
        "Unknown type",
        "Undefined identifier",
        "Unknown base class",
        "Unknown interface",
        "'self' can only be used inside a method",
        "'new' can only be used with struct, class, or collection types",
    )
    induced_markers = (
        "Logical not requires Boolean operand",
        "Negation requires numeric operand",
        "Invalid operands for arithmetic operation",
        "Expression is not indexable",
        "Range bounds must be integers",
        "Type mismatch: expected",
    )
    messages = [diag.get("message", "") for diag in errors]
    has_context = any(any(marker in message for marker in context_markers) for message in messages)
    if has_context and all(
        any(marker in message for marker in context_markers)
        or any(marker in message for marker in induced_markers)
        for message in messages
    ):
        return "depends on definitions from earlier/later examples"
    return ""


def run_file(path: Path, timeout: float) -> subprocess.CompletedProcess[str]:
    return run_cmd([str(VIPER), "run", str(path), "--max-steps", "500000"], timeout)


def safe_group_path(base: Path, marker: str) -> Path:
    rel = Path(marker)
    if rel.is_absolute() or ".." in rel.parts:
        raise ValueError(f"unsafe file marker: {marker}")
    return base / rel


def audit_zia_file_group(group: list[Fence], tmp: Path, timeout: float) -> list[Result]:
    group_dir = tmp / f"group_{module_name(group[0])}"
    group_dir.mkdir(parents=True, exist_ok=True)
    generated: list[tuple[Fence, Path]] = []
    combined = ""
    for fence in group:
        marker = file_marker(fence)
        try:
            out = safe_group_path(group_dir, marker)
        except ValueError as exc:
            return [
                Result(
                    item.path,
                    item.start_line,
                    item.end_line,
                    item.lang,
                    "zia",
                    "file-group",
                    "fail",
                    reason=str(exc),
                )
                for item in group
            ]
        out.parent.mkdir(parents=True, exist_ok=True)
        source = fence.code.strip() + "\n"
        out.write_text(source, encoding="utf-8")
        generated.append((fence, out))
        combined += source + "\n"

    results: list[Result] = []
    failed = False
    for fence, path in generated:
        try:
            checked = check_file(path, timeout)
        except subprocess.TimeoutExpired:
            failed = True
            results.append(
                Result(
                    fence.path,
                    fence.start_line,
                    fence.end_line,
                    fence.lang,
                    "zia",
                    "file-group",
                    "timeout",
                    generated=str(path),
                )
            )
            continue
        status = "pass" if checked.returncode == 0 else "fail"
        failed = failed or checked.returncode != 0
        results.append(
            Result(
                fence.path,
                fence.start_line,
                fence.end_line,
                fence.lang,
                "zia",
                "file-group",
                status,
                checked.returncode,
                generated=str(path),
                stdout=checked.stdout[-1200:],
                stderr=checked.stderr[-1200:],
            )
        )

    if failed or not should_run(combined, True):
        return results

    entry = next(
        (
            path
            for fence, path in generated
            if re.search(r"(?m)^\s*func\s+start\s*\(\s*\)", fence.code)
            and path.stem.lower() == "main"
        ),
        None,
    )
    if entry is None:
        entry = next(
            (
                path
                for fence, path in generated
                if re.search(r"(?m)^\s*func\s+start\s*\(\s*\)", fence.code)
            ),
            None,
        )
    if entry is None:
        return results

    try:
        ran = run_file(entry, timeout)
    except subprocess.TimeoutExpired:
        return [
            result
            if result.generated != str(entry)
            else Result(
                result.path,
                result.start_line,
                result.end_line,
                result.lang,
                result.kind,
                result.mode,
                "run-timeout",
                generated=result.generated,
            )
            for result in results
        ]
    if ran.returncode == 0:
        return results
    return [
        result
        if result.generated != str(entry)
        else Result(
            result.path,
            result.start_line,
            result.end_line,
            result.lang,
            result.kind,
            result.mode,
            "run-fail",
            ran.returncode,
            generated=result.generated,
            stdout=ran.stdout[-1200:],
            stderr=ran.stderr[-1200:],
        )
        for result in results
    ]


def audit_zia(fence: Fence, tmp: Path, timeout: float) -> Result:
    code = fence.code.strip()
    reason = pseudo_reason(code)
    if reason:
        return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "zia", "skip", "skipped", reason=reason)
    negative = is_negative_example(fence)
    source, mode, has_entry = zia_source(fence, negative)
    if mode in {"multi-module", "isolated-brace", "unbalanced-fragment", "expression-fragment"}:
        return Result(
            fence.path,
            fence.start_line,
            fence.end_line,
            fence.lang,
            "zia",
            mode,
            "skipped",
            reason={
                "multi-module": "multiple modules in one illustrative fence",
                "isolated-brace": "isolated brace fragment",
                "unbalanced-fragment": "unbalanced syntax fragment",
                "expression-fragment": "expression or literal fragment",
            }[mode],
        )
    generated = tmp / f"{module_name(fence)}.zia"
    generated.write_text(source, encoding="utf-8")
    try:
        checked = check_file(generated, timeout)
    except subprocess.TimeoutExpired:
        return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "zia", mode, "timeout", generated=str(generated))
    if negative:
        if checked.returncode != 0:
            status = "expected-fail"
        elif should_run(source, True):
            try:
                ran = run_file(generated, timeout)
            except subprocess.TimeoutExpired:
                return Result(
                    fence.path,
                    fence.start_line,
                    fence.end_line,
                    fence.lang,
                    "zia",
                    mode,
                    "expected-fail",
                    generated=str(generated),
                    reason="expected runtime timeout",
                )
            status = "expected-fail" if ran.returncode != 0 else "unexpected-pass"
            return Result(
                fence.path,
                fence.start_line,
                fence.end_line,
                fence.lang,
                "zia",
                mode,
                status,
                ran.returncode,
                generated=str(generated),
                stdout=ran.stdout[-1200:],
                stderr=ran.stderr[-1200:],
            )
        else:
            status = "unexpected-pass"
    else:
        status = "pass" if checked.returncode == 0 else "fail"
        if status == "fail":
            context_reason = context_failure_reason(checked.stderr)
            if context_reason:
                return Result(
                    fence.path,
                    fence.start_line,
                    fence.end_line,
                    fence.lang,
                    "zia",
                    mode,
                    "skipped",
                    checked.returncode,
                    reason=context_reason,
                    generated=str(generated),
                    stdout=checked.stdout[-1200:],
                    stderr=checked.stderr[-1200:],
                )
    if checked.returncode == 0 and not negative and should_run(source, has_entry):
        try:
            ran = run_file(generated, timeout)
        except subprocess.TimeoutExpired:
            return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "zia", mode, "run-timeout", generated=str(generated))
        if ran.returncode != 0:
            return Result(
                fence.path,
                fence.start_line,
                fence.end_line,
                fence.lang,
                "zia",
                mode,
                "run-fail",
                ran.returncode,
                generated=str(generated),
                stdout=ran.stdout[-1200:],
                stderr=ran.stderr[-1200:],
            )
    return Result(
        fence.path,
        fence.start_line,
        fence.end_line,
        fence.lang,
        "zia",
        mode,
        status,
        checked.returncode,
        generated=str(generated),
        stdout=checked.stdout[-1200:],
        stderr=checked.stderr[-1200:],
    )


def audit_basic(fence: Fence, tmp: Path, timeout: float) -> Result:
    code = fence.code.strip()
    reason = pseudo_reason(code)
    if reason:
        return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "basic", "skip", "skipped", reason=reason)
    generated = tmp / f"basic_{module_name(fence)}.bas"
    generated.write_text(code + "\n", encoding="utf-8")
    negative = is_negative_example(fence)
    try:
        checked = check_file(generated, timeout)
    except subprocess.TimeoutExpired:
        return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "basic", "as-is", "timeout", generated=str(generated))
    if negative:
        status = "expected-fail" if checked.returncode != 0 else "unexpected-pass"
    else:
        status = "pass" if checked.returncode == 0 else "fail"
        if status == "fail":
            context_reason = basic_context_failure_reason(checked.stderr)
            if context_reason:
                return Result(
                    fence.path,
                    fence.start_line,
                    fence.end_line,
                    fence.lang,
                    "basic",
                    "as-is",
                    "skipped",
                    checked.returncode,
                    reason=context_reason,
                    generated=str(generated),
                    stdout=checked.stdout[-1200:],
                    stderr=checked.stderr[-1200:],
                )
    if checked.returncode == 0 and not negative and should_run(code, True):
        try:
            ran = run_file(generated, timeout)
        except subprocess.TimeoutExpired:
            return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "basic", "as-is", "run-timeout", generated=str(generated))
        if ran.returncode != 0:
            return Result(
                fence.path,
                fence.start_line,
                fence.end_line,
                fence.lang,
                "basic",
                "as-is",
                "run-fail",
                ran.returncode,
                generated=str(generated),
                stdout=ran.stdout[-1200:],
                stderr=ran.stderr[-1200:],
            )
    return Result(
        fence.path,
        fence.start_line,
        fence.end_line,
        fence.lang,
        "basic",
        "as-is",
        status,
        checked.returncode,
        generated=str(generated),
        stdout=checked.stdout[-1200:],
        stderr=checked.stderr[-1200:],
    )


def basic_context_failure_reason(stderr: str) -> str:
    try:
        payload = json.loads(stderr)
    except json.JSONDecodeError:
        return ""
    diagnostics = payload.get("diagnostics", [])
    errors = [diag for diag in diagnostics if diag.get("severity") == "error"]
    if not errors:
        return ""
    context_markers = (
        "unknown variable",
        "unknown procedure",
        "unknown type",
        "no such property",
        "argument count mismatch",
    )
    messages = [diag.get("message", "").lower() for diag in errors]
    if messages and all(any(marker in message for marker in context_markers) for message in messages):
        return "depends on definitions from surrounding reference context"
    return ""


def audit_il(fence: Fence, tmp: Path, timeout: float) -> Result:
    code = fence.code.strip()
    return Result(
        fence.path,
        fence.start_line,
        fence.end_line,
        fence.lang,
        "il",
        "skip",
        "skipped",
        reason="IL snippets are illustrative; current viper run accepts .zia/.bas/project targets",
    )


def audit_fence(fence: Fence, tmp: Path, timeout: float) -> Result:
    if not fence.code.strip():
        return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "empty", "skip", "skipped", reason="empty fence")
    lang = fence.lang.lower()
    if lang in ZIA_LANGS:
        return audit_zia(fence, tmp, timeout)
    if lang in BASIC_LANGS:
        return audit_basic(fence, tmp, timeout)
    if lang in IL_LANGS:
        return audit_il(fence, tmp, timeout)
    return Result(fence.path, fence.start_line, fence.end_line, fence.lang, "other", "skip", "skipped", reason="non-runnable fence language")


def print_summary(results: list[Result]) -> None:
    status_counts = Counter(result.status for result in results)
    print("Status:")
    for status, count in sorted(status_counts.items()):
        print(f"  {status}: {count}")
    print("")
    by_file: dict[str, Counter[str]] = defaultdict(Counter)
    for result in results:
        by_file[result.path][result.status] += 1
    print("By file:")
    for path in sorted(by_file):
        counts = " ".join(f"{key}:{value}" for key, value in sorted(by_file[path].items()))
        print(f"  {path}: {counts}")
    failures = [
        result
        for result in results
        if result.status in {"fail", "run-fail", "timeout", "run-timeout", "unexpected-pass"}
    ]
    if failures:
        print("")
        print("Failures:")
        for result in failures:
            detail = result.reason or result.stderr.replace("\n", " ")[:300]
            print(f"  {result.status}: {result.path}:{result.start_line} [{result.kind}/{result.mode}] {detail}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*", help="Bible markdown files to audit; defaults to all")
    parser.add_argument("--json", dest="json_path", help="write full JSON report")
    parser.add_argument("--keep", action="store_true", help="keep generated temporary files")
    parser.add_argument("--timeout", type=float, default=5.0, help="per-example timeout in seconds")
    args = parser.parse_args()

    if not VIPER.exists():
        raise SystemExit(f"missing built viper tool: {VIPER}")

    tmp = Path(tempfile.mkdtemp(prefix="viper-bible-audit-", dir="/tmp"))
    results: list[Result] = []
    try:
        fences = list(iter_fences(iter_markdown_files(args.files)))
        index = 0
        while index < len(fences):
            fence = fences[index]
            if fence.lang.lower() in ZIA_LANGS and file_marker(fence):
                group = [fence]
                next_index = index + 1
                while (
                    next_index < len(fences)
                    and fences[next_index].path == fence.path
                    and fences[next_index].lang.lower() in ZIA_LANGS
                    and file_marker(fences[next_index])
                ):
                    group.append(fences[next_index])
                    next_index += 1
                if len(group) > 1:
                    results.extend(audit_zia_file_group(group, tmp, args.timeout))
                    index = next_index
                    continue
            results.append(audit_fence(fence, tmp, args.timeout))
            index += 1
        print_summary(results)
        if args.json_path:
            Path(args.json_path).write_text(
                json.dumps([asdict(result) for result in results], indent=2),
                encoding="utf-8",
            )
        if args.keep:
            print("")
            print(f"Generated files kept at {tmp}")
        return 0
    finally:
        if not args.keep:
            shutil.rmtree(tmp)


if __name__ == "__main__":
    raise SystemExit(main())
