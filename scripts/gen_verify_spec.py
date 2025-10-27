#!/usr/bin/env python3
"""Generate IL verifier opcode specification tables from the VM schema."""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any, Dict, List

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA_DEFAULT = PROJECT_ROOT / "src/vm/ops/schema/ops.yaml"
OUTPUT_DEFAULT = PROJECT_ROOT / "src/il/verify/generated/SpecTables.inc"

LICENSE_BANNER = "//===- Automatically generated: do not edit. --------------------------===-"
TYPE_MAP = {
    "None": "il::verify::TypeClass::None",
    "Void": "il::verify::TypeClass::Void",
    "I1": "il::verify::TypeClass::I1",
    "I16": "il::verify::TypeClass::I16",
    "I32": "il::verify::TypeClass::I32",
    "I64": "il::verify::TypeClass::I64",
    "F64": "il::verify::TypeClass::F64",
    "Ptr": "il::verify::TypeClass::Ptr",
    "Str": "il::verify::TypeClass::Str",
    "Error": "il::verify::TypeClass::Error",
    "ResumeTok": "il::verify::TypeClass::ResumeTok",
    "InstrType": "il::verify::TypeClass::InstrType",
    "Any": "il::verify::TypeClass::None",
    "Dynamic": "il::verify::TypeClass::None",
}

ARITY_MAP = {
    "None": "il::core::ResultArity::None",
    "One": "il::core::ResultArity::One",
    "Optional": "il::core::ResultArity::Optional",
}

ACTION_MAP = {
    "default": "il::verify::VerifyAction::Default",
    "reject": "il::verify::VerifyAction::Reject",
    "idx_chk": "il::verify::VerifyAction::IdxChk",
    "alloca": "il::verify::VerifyAction::Alloca",
    "gep": "il::verify::VerifyAction::GEP",
    "load": "il::verify::VerifyAction::Load",
    "store": "il::verify::VerifyAction::Store",
    "addr_of": "il::verify::VerifyAction::AddrOf",
    "const_str": "il::verify::VerifyAction::ConstStr",
    "const_null": "il::verify::VerifyAction::ConstNull",
    "call": "il::verify::VerifyAction::Call",
    "trap_kind": "il::verify::VerifyAction::TrapKind",
    "trap_from_err": "il::verify::VerifyAction::TrapFromErr",
    "trap_err": "il::verify::VerifyAction::TrapErr",
    "cast_fp_to_si_rte_chk": "il::verify::VerifyAction::CastFpToSiRteChk",
    "cast_fp_to_ui_rte_chk": "il::verify::VerifyAction::CastFpToUiRteChk",
    "cast_si_narrow_chk": "il::verify::VerifyAction::CastSiNarrowChk",
    "cast_ui_narrow_chk": "il::verify::VerifyAction::CastUiNarrowChk",
}

SPECIAL_RULES: Dict[str, Dict[str, Any]] = {
    "Add": {
        "action": "reject",
        "message": "signed integer add must use iadd.ovf (traps on overflow)",
    },
    "Sub": {
        "action": "reject",
        "message": "signed integer sub must use isub.ovf (traps on overflow)",
    },
    "Mul": {
        "action": "reject",
        "message": "signed integer mul must use imul.ovf (traps on overflow)",
    },
    "SDiv": {
        "action": "reject",
        "message": "signed division must use sdiv.chk0 (traps on divide-by-zero and overflow)",
    },
    "UDiv": {
        "action": "reject",
        "message": "unsigned division must use udiv.chk0 (traps on divide-by-zero)",
    },
    "SRem": {
        "action": "reject",
        "message": "signed remainder must use srem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)",
    },
    "URem": {
        "action": "reject",
        "message": "unsigned remainder must use urem.chk0 (traps on divide-by-zero; matches BASIC MOD semantics)",
    },
    "Fptosi": {
        "action": "reject",
        "message": "fp to integer narrowing must use cast.fp_to_si.rte.chk (rounds to nearest-even and traps on overflow)",
    },
    "IdxChk": {"action": "idx_chk"},
    "Alloca": {"action": "alloca"},
    "GEP": {"action": "gep"},
    "Load": {"action": "load"},
    "Store": {"action": "store"},
    "AddrOf": {"action": "addr_of"},
    "ConstStr": {"action": "const_str"},
    "ConstNull": {"action": "const_null"},
    "Call": {"action": "call"},
    "TrapKind": {"action": "trap_kind"},
    "TrapFromErr": {"action": "trap_from_err"},
    "TrapErr": {"action": "trap_err"},
    "CastFpToSiRteChk": {"action": "cast_fp_to_si_rte_chk"},
    "CastFpToUiRteChk": {"action": "cast_fp_to_ui_rte_chk"},
    "CastSiNarrowChk": {"action": "cast_si_narrow_chk"},
    "CastUiNarrowChk": {"action": "cast_ui_narrow_chk"},
}


def load_schema(path: pathlib.Path) -> List[Dict[str, Any]]:
    data = json.loads(path.read_text())
    if "ops" not in data or not isinstance(data["ops"], list):
        raise ValueError("schema must contain a top-level 'ops' list")
    return data["ops"]


def map_type(name: str) -> str:
    try:
        return TYPE_MAP[name]
    except KeyError as exc:
        raise KeyError(f"unknown type class '{name}' in schema") from exc


def map_arity(name: str) -> str:
    try:
        return ARITY_MAP[name]
    except KeyError as exc:
        raise KeyError(f"unknown result arity '{name}' in schema") from exc


def to_uint8_literal(value: Any) -> str:
    if isinstance(value, int):
        return f"static_cast<uint8_t>({value})"
    if isinstance(value, str):
        if value == "kVariadicOperandCount":
            return "static_cast<uint8_t>(il::core::kVariadicOperandCount)"
        raise ValueError(f"unsupported numeric literal: {value}")
    raise TypeError(f"unexpected numeric literal type: {type(value)!r}")


def has_handler(entry: Dict[str, Any]) -> bool:
    handler = entry["dispatch"]["handler"]
    return handler != "nullptr"


def emit_opcode_specs(ops: List[Dict[str, Any]]) -> List[str]:
    lines = [
        LICENSE_BANNER.rstrip(),
        "inline constexpr std::array<OpcodeSpec, il::core::kNumOpcodes> kOpcodeSpecs = {{",
    ]
    for op in ops:
        operands = op["operands"]["types"]
        operand_list = ", ".join(map_type(t) for t in operands)
        lines.extend(
            [
                "    {",
                f"        \"{op['mnemonic']}\",",
                "        {",
                f"            {map_arity(op['result']['arity'])},",
                f"            {map_type(op['result']['type'])},",
                f"            {to_uint8_literal(op['operands']['min'])},",
                f"            {to_uint8_literal(op['operands']['max'])},",
                f"            {{ {operand_list} }},",
                "        },",
                "        {",
                f"            {str(op['flags']['side_effects']).lower()},",
                f"            {to_uint8_literal(op['flags']['successors'])},",
                f"            {str(op['flags']['terminator']).lower()},",
                "        },",
                f"        {str(has_handler(op)).lower()},",
                "    },",
            ]
        )
    lines.append("}};")
    return lines


def emit_verify_rules(ops: List[Dict[str, Any]]) -> List[str]:
    lines = [
        "inline constexpr std::array<VerifyRule, il::core::kNumOpcodes> kVerifyRules = {{",
    ]
    for op in ops:
        rule = SPECIAL_RULES.get(op["opcode"], {"action": "default"})
        action = ACTION_MAP[rule["action"]]
        message = rule.get("message")
        if message is None:
            message_literal = "nullptr"
        else:
            escaped = message.replace("\\", "\\\\").replace("\"", "\\\"")
            message_literal = f"\"{escaped}\""
        lines.append(f"    {{ {action}, {message_literal} }},")
    lines.append("}};")
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate IL verifier spec tables from schema")
    parser.add_argument("--schema", type=pathlib.Path, default=SCHEMA_DEFAULT)
    parser.add_argument("--out", type=pathlib.Path, default=OUTPUT_DEFAULT)
    args = parser.parse_args()

    ops = load_schema(args.schema)
    args.out.parent.mkdir(parents=True, exist_ok=True)

    lines = emit_opcode_specs(ops) + [""] + emit_verify_rules(ops)
    args.out.write_text("\n".join(lines) + "\n")
    print(f"wrote {args.out.relative_to(PROJECT_ROOT)}")


if __name__ == "__main__":
    main()
