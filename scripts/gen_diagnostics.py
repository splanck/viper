#!/usr/bin/env python3
"""Generate BASIC diagnostic catalog from YAML specification."""
from __future__ import annotations

import argparse
import importlib
import importlib.util
import pathlib
from typing import Any, Dict, List, Optional


class SpecError(RuntimeError):
    """Raised when the diagnostic specification is invalid."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Emit BASIC diagnostic catalog")
    parser.add_argument("--spec", required=True, type=pathlib.Path, help="YAML specification file")
    parser.add_argument("--header", required=True, type=pathlib.Path, help="Destination header path")
    parser.add_argument("--source", required=True, type=pathlib.Path, help="Destination source path")
    return parser.parse_args()


# ---------------------------------------------------------------------------
# YAML loading
# ---------------------------------------------------------------------------

def load_spec(path: pathlib.Path) -> List[Dict[str, Any]]:
    yaml_spec = importlib.util.find_spec("yaml")
    if yaml_spec is not None:
        yaml = importlib.import_module("yaml")  # type: ignore

        with path.open("r", encoding="utf-8") as handle:
            data = yaml.safe_load(handle) or []
        if not isinstance(data, list):
            raise SpecError("diagnostic spec must be a list of mappings")
        return [dict(entry) for entry in data]

    return _load_spec_fallback(path)


def _load_spec_fallback(path: pathlib.Path) -> List[Dict[str, Any]]:
    entries: List[Dict[str, Any]] = []
    current: Optional[Dict[str, Any]] = None
    current_list_key: Optional[str] = None

    with path.open("r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.rstrip()
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue

            indent = len(line) - len(stripped)
            if stripped.startswith("- ") and indent == 0:
                if current:
                    entries.append(current)
                current = {}
                current_list_key = None
                remainder = stripped[2:]
                if remainder:
                    if ":" in remainder:
                        key, value = remainder.split(":", 1)
                        current[key.strip()] = _parse_scalar(value.strip())
                    else:
                        raise SpecError(f"invalid entry line: {line}")
                continue

            if current is None:
                raise SpecError("found mapping entry before any sequence item")

            if stripped.startswith("- "):
                if not current_list_key:
                    raise SpecError(f"list item without list key: {line}")
                current[current_list_key].append(_parse_scalar(stripped[2:].strip()))
                continue

            if ":" not in stripped:
                raise SpecError(f"malformed mapping line: {line}")

            key, value = stripped.split(":", 1)
            key = key.strip()
            value = value.strip()
            if value == "":
                current[key] = []
                current_list_key = key
            else:
                current[key] = _parse_scalar(value)
                current_list_key = None

    if current:
        entries.append(current)

    return entries


def _parse_scalar(value: str) -> Any:
    if value in {"null", "Null", "NULL", "~"}:
        return None
    if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
        return value[1:-1]
    return value


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------

def cpp_string_literal(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def enum_name(identifier: str) -> str:
    stem = identifier
    if stem.startswith("BASIC_"):
        stem = stem[len("BASIC_") :]
    parts = [part for part in stem.lower().split("_") if part]
    return "".join(part.capitalize() for part in parts)


def severity_expr(value: str) -> str:
    lowered = value.strip().lower()
    mapping = {
        "note": "il::support::Severity::Note",
        "warning": "il::support::Severity::Warning",
        "error": "il::support::Severity::Error",
    }
    if lowered not in mapping:
        raise SpecError(f"unknown severity '{value}'")
    return mapping[lowered]


# ---------------------------------------------------------------------------
# Emission helpers
# ---------------------------------------------------------------------------

def validate_entry(entry: Dict[str, Any]) -> None:
    for key in ("id", "severity", "format"):
        if key not in entry:
            raise SpecError(f"diagnostic entry missing '{key}' field: {entry!r}")
    entry.setdefault("code", entry["id"])


def emit_header(entries: List[Dict[str, Any]]) -> str:
    enum_entries = ",\n".join(f"    {enum_name(e['id'])}" for e in entries)
    return "\n".join(
        [
            "//===----------------------------------------------------------------------===//",
            "// Generated file -- do not edit manually.",
            "//===----------------------------------------------------------------------===//",
            "#pragma once",
            "",
            "#include \"support/diagnostics.hpp\"",
            "#include <initializer_list>",
            "#include <string>",
            "#include <string_view>",
            "",
            "namespace il::frontends::basic::diag",
            "{",
            "",
            "enum class BasicDiag",
            "{",
            f"{enum_entries}",
            "};",
            "",
            "struct Replacement",
            "{",
            "    std::string_view key;",
            "    std::string_view value;",
            "};",
            "",
            "struct BasicDiagInfo",
            "{",
            "    std::string_view id;",
            "    std::string_view code;",
            "    il::support::Severity severity;",
            "    std::string_view format;",
            "};",
            "",
            "[[nodiscard]] const BasicDiagInfo &getInfo(BasicDiag diag);",
            "[[nodiscard]] std::string_view getId(BasicDiag diag);",
            "[[nodiscard]] std::string_view getCode(BasicDiag diag);",
            "[[nodiscard]] il::support::Severity getSeverity(BasicDiag diag);",
            "[[nodiscard]] std::string_view getFormat(BasicDiag diag);",
            "[[nodiscard]] std::string formatMessage(BasicDiag diag,",
            "                                         std::initializer_list<Replacement> replacements = {});",
            "",
            "} // namespace il::frontends::basic::diag",
            "",
        ]
    )


def emit_source(entries: List[Dict[str, Any]]) -> str:
    rows = []
    for entry in entries:
        row = (
            "        {"
            f"{cpp_string_literal(entry['id'])}, "
            f"{cpp_string_literal(entry['code'])}, "
            f"{severity_expr(entry['severity'])}, "
            f"{cpp_string_literal(entry['format'])}"
            "}"
        )
        rows.append(row)
    table = ",\n".join(rows)
    return "\n".join(
        [
            "//===----------------------------------------------------------------------===//",
            "// Generated file -- do not edit manually.",
            "//===----------------------------------------------------------------------===//",
            "",
            "#include \"viper/diag/BasicDiag.hpp\"",
            "",
            "#include <array>",
            "#include <cstddef>",
            "#include <string>",
            "",
            "namespace il::frontends::basic::diag",
            "{",
            "namespace",
            "{",
            f"constexpr std::array<BasicDiagInfo, {len(entries)}> kDiagTable = {{{{\n{table}\n    }}}};",
            "}",
            "",
            "const BasicDiagInfo &getInfo(BasicDiag diag)",
            "{",
            "    const auto index = static_cast<std::size_t>(diag);",
            "    return kDiagTable.at(index);",
            "}",
            "",
            "std::string_view getId(BasicDiag diag)",
            "{",
            "    return getInfo(diag).id;",
            "}",
            "",
            "std::string_view getCode(BasicDiag diag)",
            "{",
            "    return getInfo(diag).code;",
            "}",
            "",
            "il::support::Severity getSeverity(BasicDiag diag)",
            "{",
            "    return getInfo(diag).severity;",
            "}",
            "",
            "std::string_view getFormat(BasicDiag diag)",
            "{",
            "    return getInfo(diag).format;",
            "}",
            "",
            "std::string formatMessage(BasicDiag diag, std::initializer_list<Replacement> replacements)",
            "{",
            "    std::string message(getFormat(diag));",
            "    for (const auto &repl : replacements)",
            "    {",
            "        std::string placeholder;",
            "        placeholder.reserve(repl.key.size() + 2);",
            "        placeholder.push_back('{');",
            "        placeholder.append(repl.key.begin(), repl.key.end());",
            "        placeholder.push_back('}');",
            "        std::size_t pos = 0;",
            "        while ((pos = message.find(placeholder, pos)) != std::string::npos)",
            "        {",
            "            message.replace(pos, placeholder.size(), repl.value);",
            "            pos += repl.value.size();",
            "        }",
            "    }",
            "    return message;",
            "}",
            "",
            "} // namespace il::frontends::basic::diag",
            "",
        ]
    )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def write_file(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        existing = path.read_text(encoding="utf-8")
        if existing == content:
            return
    path.write_text(content + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    spec_entries = load_spec(args.spec)
    if not spec_entries:
        raise SpecError("diagnostic spec is empty")

    for entry in spec_entries:
        validate_entry(entry)

    header_content = emit_header(spec_entries)
    source_content = emit_source(spec_entries)

    write_file(args.header, header_content)
    write_file(args.source, source_content)


if __name__ == "__main__":
    main()
