#!/usr/bin/env python3
"""Generate runtime descriptor rows from YAML specification."""
from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Any, Dict, List, Optional


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Emit runtime signature descriptors")
    parser.add_argument("--spec", required=True, type=pathlib.Path, help="YAML specification file")
    parser.add_argument(
        "--output", required=True, type=pathlib.Path, help="Destination for generated include"
    )
    return parser.parse_args()


def load_spec(path: pathlib.Path) -> List[Dict[str, Any]]:
    try:
        import yaml  # type: ignore

        with path.open("r", encoding="utf-8") as handle:
            data = yaml.safe_load(handle) or []
        if not isinstance(data, list):
            raise TypeError("runtime signature spec must be a list of mappings")
        return [dict(entry) for entry in data]
    except ModuleNotFoundError:
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
                        raise ValueError(f"Invalid entry line: {line}")
                continue

            if current is None:
                raise ValueError("Found mapping entry before any sequence item")

            if stripped.startswith("- "):
                if not current_list_key:
                    raise ValueError(f"List item without list key: {line}")
                current[current_list_key].append(_parse_scalar(stripped[2:].strip()))
                continue

            if ":" not in stripped:
                raise ValueError(f"Malformed mapping line: {line}")

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


def format_signature(entry: Dict[str, Any]) -> str:
    signature = entry.get("signature")
    if signature:
        return f"RtSig::{signature}"
    if "signature_expr" in entry:
        return str(entry["signature_expr"])
    return "std::nullopt"


def format_spec(entry: Dict[str, Any]) -> str:
    spec_expr = entry.get("spec")
    if spec_expr:
        return str(spec_expr)
    signature = entry.get("signature")
    if signature:
        return f"data::kRtSigSpecs[static_cast<std::size_t>(RtSig::{signature})]"
    explicit = entry.get("spec_expr")
    if explicit:
        return str(explicit)
    raise KeyError(f"Entry '{entry.get('name')}' must provide either 'signature' or 'spec'")


def format_lowering(value: Optional[str]) -> str:
    if value is None:
        raise KeyError("Missing lowering directive")
    lowered = value.strip()
    lower_ascii = lowered.lower()
    if lower_ascii == "always":
        return "kAlwaysLowering"
    if lower_ascii == "bounds_checked":
        return "kBoundsCheckedLowering"
    if lower_ascii == "manual":
        return "kManualLowering"
    if lower_ascii.startswith("feature:"):
        parts = lowered.split(":")
        if len(parts) < 2:
            raise ValueError(f"Invalid feature lowering '{value}'")
        feature = parts[1]
        ordered = len(parts) > 2 and parts[2].lower() == "ordered"
        suffix = ", true" if ordered else ""
        return f"featureLowering(RuntimeFeature::{feature}{suffix})"
    return lowered


def format_trap(value: Optional[str]) -> str:
    if value is None:
        return "RuntimeTrapClass::None"
    trimmed = value.strip()
    if trimmed.startswith("RuntimeTrapClass::"):
        return trimmed
    if trimmed.lower() == "none":
        return "RuntimeTrapClass::None"
    return f"RuntimeTrapClass::{trimmed}"


def format_hidden_ptr(entry: Dict[str, Any]) -> str:
    if "hidden_ptr" in entry:
        return str(entry["hidden_ptr"])
    return "nullptr"


def format_hidden_count(entry: Dict[str, Any], ptr_expr: str) -> str:
    if "hidden_count" in entry:
        return str(entry["hidden_count"])
    if ptr_expr == "nullptr":
        return "0"
    count = entry.get("hidden_size")
    if count is None:
        raise KeyError(
            f"Entry '{entry.get('name')}' provides hidden_ptr but no hidden_count/hidden_size"
        )
    return str(count)


def sanitize_handler(entry: Dict[str, Any]) -> str:
    handler = entry.get("handler")
    if not handler:
        raise KeyError(f"Entry '{entry.get('name')}' is missing handler expression")
    return str(handler)






def emit(entries: List[Dict[str, Any]]) -> str:
    lines = [
        "//===----------------------------------------------------------------------===//",
        "// Generated file -- do not edit manually.",
        "//===----------------------------------------------------------------------===//",
    ]
    for entry in entries:
        name = entry.get("name")
        if not name:
            raise KeyError("Every entry must define a name")
        signature_expr = format_signature(entry)
        spec_expr = format_spec(entry)
        handler_expr = sanitize_handler(entry)
        lowering_expr = format_lowering(entry.get("lowering"))
        trap_expr = format_trap(entry.get("trap"))
        hidden_ptr = format_hidden_ptr(entry)
        hidden_count = format_hidden_count(entry, hidden_ptr)

        lines.extend(
            [
                f"    DescriptorRow{{\"{name}\",",
                f"                  {signature_expr},",
                f"                  {spec_expr},",
                f"                  {handler_expr},",
                f"                  {lowering_expr},",
                f"                  {hidden_ptr},",
                f"                  {hidden_count},",
                f"                  {trap_expr}}},",
            ]
        )
    lines.append("//===----------------------------------------------------------------------===//")
    return "\n".join(lines) + "\n"

def main() -> int:
    args = parse_args()
    spec_entries = load_spec(args.spec)
    output = emit(spec_entries)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
