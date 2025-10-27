# ===-----------------------------------------------------------------------===#
#
#  Part of the Viper project, under the MIT License.
#  See LICENSE in the project root for license information.
#
# ===-----------------------------------------------------------------------===#
"""Generate IL verifier specification tables from the opcode schema.

This script reads the unified opcode schema (JSON/YAML) used across the
project and emits a compact `.inc` file containing the data structures the
verifier consumes at compile time.  Keeping the generator close to the schema
ensures that adding a new opcode only requires updating the schema and
regenerating the tables, without touching the verifier implementation.

Usage:
    python specgen.py --schema <path/to/ops.yaml> --out <output-directory>

The resulting file is named `InstructionSpec.inc` and should be included from
`SpecTables.cpp`.
"""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any, Dict, Iterable, List


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[4]
SCHEMA_DEFAULT = PROJECT_ROOT / "src" / "vm" / "ops" / "schema" / "ops.yaml"
GENERATED_DIR_DEFAULT = PROJECT_ROOT / "src" / "il" / "verify" / "generated"


LICENSE_BANNER = "// This file is auto-generated. Do not edit manually.\n"


def load_schema(path: pathlib.Path) -> List[Dict[str, Any]]:
    data = json.loads(path.read_text())
    if "ops" not in data or not isinstance(data["ops"], list):
        raise ValueError("schema must contain a top-level 'ops' list")
    return data["ops"]


def ensure_dir(path: pathlib.Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def result_arity_expr(name: str) -> str:
    return f"il::core::ResultArity::{name}"


def type_expr(name: str) -> str:
    return f"il::core::TypeCategory::{name}"


def opcode_expr(name: str) -> str:
    return f"il::core::Opcode::{name}"


def bool_literal(value: bool) -> str:
    return "true" if value else "false"


def numeric_literal(value: Any) -> str:
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        if value.startswith("k"):
            return f"il::core::{value}"
        return value
    raise TypeError(f"unsupported numeric literal: {value!r}")


def format_operand_types(entries: Iterable[str]) -> str:
    types = list(entries)
    if len(types) != 3:
        raise ValueError("schema must list exactly three operand type slots")
    return ", ".join(type_expr(entry) for entry in types)


def emit_instruction_spec(ops: List[Dict[str, Any]]) -> str:
    lines: List[str] = [LICENSE_BANNER]
    lines.append("static constexpr std::array<InstructionSpec, il::core::kNumOpcodes>\n"
                 "    kInstructionSpecs = {{")
    for op in ops:
        mnemonic = op["mnemonic"]
        res = op["result"]
        operands = op["operands"]
        flags = op["flags"]
        lines.append("        {")
        lines.append(f"            {opcode_expr(op['opcode'])},")
        lines.append(f"            \"{mnemonic}\",")
        lines.append("            {")
        lines.append(f"                {result_arity_expr(res['arity'])},")
        lines.append(f"                {type_expr(res['type'])},")
        lines.append("            },")
        lines.append("            {")
        lines.append(
            f"                static_cast<uint8_t>({numeric_literal(operands['min'])}),")
        lines.append(
            f"                static_cast<uint8_t>({numeric_literal(operands['max'])}),")
        lines.append(
            f"                {{ {format_operand_types(operands['types'])} }},")
        lines.append("            },")
        lines.append("            {")
        lines.append(f"                {bool_literal(flags['side_effects'])},")
        lines.append(
            f"                static_cast<uint8_t>({numeric_literal(flags['successors'])}),")
        lines.append(f"                {bool_literal(flags['terminator'])}")
        lines.append("            },")
        lines.append("        },")
    lines.append("    }};")
    return "\n".join(lines) + "\n"


def write_file(path: pathlib.Path, content: str) -> None:
    path.write_text(content)
    rel = path.relative_to(PROJECT_ROOT)
    print(f"wrote {rel}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate IL verifier tables from schema")
    parser.add_argument("--schema", type=pathlib.Path, default=SCHEMA_DEFAULT)
    parser.add_argument("--out", type=pathlib.Path, default=GENERATED_DIR_DEFAULT)
    args = parser.parse_args()

    ops = load_schema(args.schema)
    ensure_dir(args.out)

    content = emit_instruction_spec(ops)
    write_file(args.out / "InstructionSpec.inc", content)


if __name__ == "__main__":
    main()
