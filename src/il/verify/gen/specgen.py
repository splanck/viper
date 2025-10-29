#!/usr/bin/env python3
import argparse
import json
import pathlib
from typing import Any, Dict, List, Tuple

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[4]
SCHEMA_DEFAULT = PROJECT_ROOT / "src" / "vm" / "ops" / "schema" / "ops.yaml"
OUTPUT_DEFAULT = PROJECT_ROOT / "src" / "il" / "verify" / "generated" / "SpecTables.cpp"

HEADER_PREFIX = "//===----------------------------------------------------------------------===//\n"
HEADER_SUFFIX = "//===----------------------------------------------------------------------===//\n"
LICENSE_BANNER = (
    "// This file is auto-generated. Do not edit manually.\n"
    "// Use src/il/verify/gen/specgen.py to regenerate.\n"
)

TYPE_MAP = {
    "None": "il::core::TypeCategory::None",
    "Void": "il::core::TypeCategory::Void",
    "I1": "il::core::TypeCategory::I1",
    "I16": "il::core::TypeCategory::I16",
    "I32": "il::core::TypeCategory::I32",
    "I64": "il::core::TypeCategory::I64",
    "F64": "il::core::TypeCategory::F64",
    "Ptr": "il::core::TypeCategory::Ptr",
    "Str": "il::core::TypeCategory::Str",
    "Error": "il::core::TypeCategory::Error",
    "ResumeTok": "il::core::TypeCategory::ResumeTok",
    "Any": "il::core::TypeCategory::Any",
    "InstrType": "il::core::TypeCategory::InstrType",
    "Dynamic": "il::core::TypeCategory::Dynamic",
}

ARITY_MAP = {
    "None": "il::core::ResultArity::None",
    "One": "il::core::ResultArity::One",
    "Optional": "il::core::ResultArity::Optional",
}

STRATEGY_DEFAULT = "VerifyStrategy::Default"

STRATEGY_OVERRIDES: Dict[str, Tuple[str, str]] = {
    "Alloca": ("VerifyStrategy::Alloca", "nullptr"),
    "GEP": ("VerifyStrategy::GEP", "nullptr"),
    "Load": ("VerifyStrategy::Load", "nullptr"),
    "Store": ("VerifyStrategy::Store", "nullptr"),
    "AddrOf": ("VerifyStrategy::AddrOf", "nullptr"),
    "ConstStr": ("VerifyStrategy::ConstStr", "nullptr"),
    "ConstNull": ("VerifyStrategy::ConstNull", "nullptr"),
    "Call": ("VerifyStrategy::Call", "nullptr"),
    "TrapKind": ("VerifyStrategy::TrapKind", "nullptr"),
    "TrapFromErr": ("VerifyStrategy::TrapFromErr", "nullptr"),
    "TrapErr": ("VerifyStrategy::TrapErr", "nullptr"),
    "IdxChk": ("VerifyStrategy::IdxChk", "nullptr"),
    "CastFpToSiRteChk": ("VerifyStrategy::CastFpToSiRteChk", "nullptr"),
    "CastFpToUiRteChk": ("VerifyStrategy::CastFpToUiRteChk", "nullptr"),
    "CastSiNarrowChk": ("VerifyStrategy::CastSiNarrowChk", "nullptr"),
    "CastUiNarrowChk": ("VerifyStrategy::CastUiNarrowChk", "nullptr"),
    "Add": (
        "VerifyStrategy::Reject",
        "\"signed integer add must use iadd.ovf (traps on overflow)\"",
    ),
    "Sub": (
        "VerifyStrategy::Reject",
        "\"signed integer sub must use isub.ovf (traps on overflow)\"",
    ),
    "Mul": (
        "VerifyStrategy::Reject",
        "\"signed integer mul must use imul.ovf (traps on overflow)\"",
    ),
    "SDiv": (
        "VerifyStrategy::Reject",
        "\"signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)\"",
    ),
    "UDiv": (
        "VerifyStrategy::Reject",
        "\"unsigned division must use udiv.chk0 (traps on divide-by-zero)\"",
    ),
    "SRem": (
        "VerifyStrategy::Reject",
        "\"signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)\"",
    ),
    "URem": (
        "VerifyStrategy::Reject",
        "\"unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)\"",
    ),
    "Fptosi": (
        "VerifyStrategy::Reject",
        "\"fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)\"",
    ),
}


def load_schema(path: pathlib.Path) -> List[Dict[str, Any]]:
    data = json.loads(path.read_text())
    ops = data.get("ops")
    if not isinstance(ops, list):
        raise ValueError("schema must contain a top-level 'ops' list")
    return ops


def numeric_literal(value: Any) -> str:
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        if value.startswith("k"):
            return f"il::core::{value}"
        return value
    raise TypeError(f"unsupported numeric literal: {value!r}")


def quote_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def operand_types(entry: Dict[str, Any]) -> str:
    types = entry["operands"]["types"]
    converted = [TYPE_MAP[t] for t in types]
    return ", ".join(converted)


def strategy_for(opcode: str) -> Tuple[str, str]:
    return STRATEGY_OVERRIDES.get(opcode, (STRATEGY_DEFAULT, "nullptr"))


def emit_spec_table(ops: List[Dict[str, Any]]) -> str:
    lines: List[str] = [
        "#include \"il/verify/SpecTables.hpp\"",
        "",
        "namespace il::verify",
        "{",
        "namespace",
        "{",
        "inline constexpr std::array<InstructionSpec, il::core::kNumOpcodes> kSpecs = {{",
    ]

    for op in ops:
        opcode = op["opcode"]
        result = op["result"]
        operands = op["operands"]
        flags = op["flags"]
        strategy, message = strategy_for(opcode)

        lines.append("    {")
        lines.append(f"        {ARITY_MAP[result['arity']]},")
        lines.append(f"        {TYPE_MAP[result['type']]},")
        lines.append(
            f"        static_cast<uint8_t>({numeric_literal(operands['min'])}),"
        )
        lines.append(
            f"        static_cast<uint8_t>({numeric_literal(operands['max'])}),"
        )
        lines.append(
            f"        {{ {operand_types(op)} }},"
        )
        lines.append(f"        {'true' if flags['side_effects'] else 'false'},")
        lines.append(
            f"        static_cast<uint8_t>({numeric_literal(flags['successors'])}),"
        )
        lines.append(f"        {'true' if flags['terminator'] else 'false'},")
        lines.append(f"        {strategy},")
        if message == "nullptr":
            lines.append("        nullptr,")
        else:
            if message.startswith('"') and message.endswith('"'):
                lines.append(f"        {message},")
            else:
                lines.append(f"        {quote_string(message)},")
        lines.append("    },")

    lines.append("}};")
    lines.append("")
    lines.append("} // namespace")
    lines.append("")
    lines.append("const InstructionSpec &getInstructionSpec(il::core::Opcode opcode)")
    lines.append("{")
    lines.append("    return kSpecs[static_cast<size_t>(opcode)];")
    lines.append("}")
    lines.append("} // namespace il::verify")
    lines.append("")

    body = "\n".join(lines)
    return HEADER_PREFIX + HEADER_SUFFIX + LICENSE_BANNER + "\n" + body


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate verifier spec tables from schema")
    parser.add_argument("--schema", type=pathlib.Path, default=SCHEMA_DEFAULT)
    parser.add_argument("--out", type=pathlib.Path, default=OUTPUT_DEFAULT)
    args = parser.parse_args()

    ops = load_schema(args.schema)
    content = emit_spec_table(ops)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(content)
    print(f"wrote {args.out.relative_to(PROJECT_ROOT)}")


if __name__ == "__main__":
    main()
