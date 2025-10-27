#!/usr/bin/env python3
"""Opcode metadata generator for the VM dispatch pipeline."""

import argparse
import json
import pathlib
from typing import Any, Dict, List

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[4]
SCHEMA_DEFAULT = PROJECT_ROOT / "src" / "vm" / "ops" / "schema" / "ops.yaml"
GENERATED_DIR = PROJECT_ROOT / "src" / "vm" / "ops" / "generated"

HEADER_PREFIX = "//===----------------------------------------------------------------------===//\n"
HEADER_SUFFIX = "//===----------------------------------------------------------------------===//\n"
LICENSE_BANNER = (
    "// This file is auto-generated. Do not edit manually.\n"
    "// Use src/vm/ops/gen/opgen.py to regenerate.\n"
)


def load_schema(path: pathlib.Path) -> List[Dict[str, Any]]:
    data = json.loads(path.read_text())
    if "ops" not in data or not isinstance(data["ops"], list):
        raise ValueError("schema must contain a top-level 'ops' list")
    return data["ops"]


def ensure_dir(path: pathlib.Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def type_expr(name: str) -> str:
    return f"il::core::TypeCategory::{name}"


def result_expr(name: str) -> str:
    return f"il::core::ResultArity::{name}"


def dispatch_enum(name: str) -> str:
    return f"il::core::VMDispatch::{name}"


def numeric_literal(value: Any) -> str:
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        if value.startswith("k"):
            return f"il::core::{value}"
        return value
    raise TypeError(f"Unsupported numeric literal: {value!r}")


def bool_literal(value: bool) -> str:
    return "true" if value else "false"


def handler_expr(entry: Dict[str, Any]) -> str:
    handler = entry["dispatch"]["handler"]
    if handler == "nullptr":
        return "nullptr"
    return handler


def emit_inline_decls(ops: List[Dict[str, Any]]) -> str:
    lines = [LICENSE_BANNER]
    for op in ops:
        name = op["opcode"]
        lines.append(f"    void inline_handle_{name}(ExecState &st);")
    return "\n".join(lines) + "\n"


def emit_inline_impls(ops: List[Dict[str, Any]]) -> str:
    lines = [LICENSE_BANNER]
    for op in ops:
        name = op["opcode"]
        handler = handler_expr(op)
        lines.append(f"void VM::inline_handle_{name}(ExecState &st)")
        lines.append("{")
        lines.append("    const Instr *instr = st.currentInstr;")
        lines.append("    if (!instr)")
        lines.append("    {")
        lines.append("        trapUnimplemented(Opcode::" + name + ");")
        lines.append("    }")
        lines.append(f"    auto handler = {handler};")
        lines.append("    if (!handler)")
        lines.append("    {")
        lines.append("        trapUnimplemented(Opcode::" + name + ");")
        lines.append("    }")
        lines.append(
            "    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);"
        )
        lines.append("    handleInlineResult(st, exec);")
        lines.append("}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def emit_switch_decl(ops: List[Dict[str, Any]]) -> str:
    lines = [LICENSE_BANNER]
    lines.append("    void dispatchOpcodeSwitch(ExecState &state, const il::core::Instr &instr);")
    return "\n".join(lines) + "\n"


def emit_switch_impl(ops: List[Dict[str, Any]]) -> str:
    lines = [LICENSE_BANNER]
    lines.append("void VM::dispatchOpcodeSwitch(ExecState &state, const il::core::Instr &instr)")
    lines.append("{")
    lines.append("    switch (instr.op)")
    lines.append("    {")
    for op in ops:
        name = op["opcode"]
        lines.append(f"        case il::core::Opcode::{name}:")
        lines.append("        {")
        lines.append("            traceInstruction(instr, state.fr);")
        lines.append(f"            inline_handle_{name}(state);")
        lines.append("            break;")
        lines.append("        }")
    lines.append("        default:")
    lines.append("            trapUnimplemented(instr.op);")
    lines.append("    }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def emit_threaded_labels(ops: List[Dict[str, Any]]) -> str:
    lines = [LICENSE_BANNER.rstrip()]
    for op in ops:
        name = op["opcode"]
        lines.append(f"    &&LBL_{name},")
    lines.append("    &&LBL_UNIMPL,")
    return "\n".join(lines) + "\n"


def emit_threaded_cases(ops: List[Dict[str, Any]]) -> str:
    lines = [LICENSE_BANNER.rstrip()]
    for op in ops:
        name = op["opcode"]
        lines.append(f"LBL_{name}:")
        lines.append("{")
        lines.append("    vm.traceInstruction(*currentInstr, state.fr);")
        lines.append(
            "    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);"
        )
        lines.append("    if (vm.finalizeDispatch(state, exec))")
        lines.append("        return true;")
        lines.append("    opcode = fetchNext();")
        lines.append("    if (state.exitRequested)")
        lines.append("        return true;")
        lines.append("    DISPATCH_TO(opcode);")
        lines.append("}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def emit_handler_table(ops: List[Dict[str, Any]]) -> str:
    lines = [
        HEADER_PREFIX,
        "// Auto-generated opcode handler table.",
        LICENSE_BANNER.strip(),
        HEADER_SUFFIX,
        "#pragma once",
        "",
        "#include \"vm/OpHandlers.hpp\"",
        "#include \"vm/VM.hpp\"",
        "",
        "namespace il::vm::generated",
        "{",
        "inline const VM::OpcodeHandlerTable &opcodeHandlers()",
        "{",
        "    static const VM::OpcodeHandlerTable table = {",
    ]
    for op in ops:
        handler = handler_expr(op)
        lines.append(f"        {handler},")
    lines.append("    };")
    lines.append("    return table;")
    lines.append("}")
    lines.append("} // namespace il::vm::generated")
    return "\n".join(lines) + "\n"


def emit_schema_header(ops: List[Dict[str, Any]]) -> str:
    lines = [
        HEADER_PREFIX,
        "// Auto-generated opcode schema metadata.",
        LICENSE_BANNER.strip(),
        HEADER_SUFFIX,
        "#pragma once",
        "",
        "#include \"il/core/OpcodeInfo.hpp\"",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace il::vm::generated",
        "{",
        "struct OpSchemaEntry",
        "{",
        "    const char *mnemonic;",
        "    il::core::ResultArity resultArity;",
        "    il::core::TypeCategory resultType;",
        "    uint8_t operandMin;",
        "    uint8_t operandMax;",
        "    std::array<il::core::TypeCategory, 3> operandTypes;",
        "    bool hasSideEffects;",
        "    uint8_t successors;",
        "    bool terminator;",
        "    il::core::VMDispatch dispatch;",
        "    bool hasHandler;",
        "};",
        "",
        "inline constexpr std::array<OpSchemaEntry, il::core::kNumOpcodes> kOpSchema = {",
        "    {",
    ]
    for op in ops:
        mnemonic = op["mnemonic"]
        res = op["result"]
        operands = op["operands"]
        flags = op["flags"]
        dispatch_name = op["dispatch"]["enum"] or "None"
        handler = handler_expr(op)
        operand_types = ", ".join(type_expr(t) for t in operands["types"])
        lines.append("        {")
        lines.append(f"            \"{mnemonic}\",")
        lines.append(f"            {result_expr(res['arity'])},")
        lines.append(f"            {type_expr(res['type'])},")
        lines.append(f"            static_cast<uint8_t>({numeric_literal(operands['min'])}),")
        lines.append(f"            static_cast<uint8_t>({numeric_literal(operands['max'])}),")
        lines.append(f"            {{ {operand_types} }},")
        lines.append(f"            {bool_literal(flags['side_effects'])},")
        lines.append(f"            static_cast<uint8_t>({numeric_literal(flags['successors'])}),")
        lines.append(f"            {bool_literal(flags['terminator'])},")
        lines.append(f"            {dispatch_enum(dispatch_name)},")
        lines.append(f"            {bool_literal(handler != 'nullptr')},")
        lines.append("        },")
    lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr const OpSchemaEntry &schema(il::core::Opcode op)")
    lines.append("{")
    lines.append("    return kOpSchema[static_cast<size_t>(op)];")
    lines.append("}")
    lines.append("} // namespace il::vm::generated")
    return "\n".join(lines) + "\n"


def write_file(path: pathlib.Path, content: str) -> None:
    path.write_text(content)
    print(f"wrote {path.relative_to(PROJECT_ROOT)}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate VM opcode helpers from schema")
    parser.add_argument("--schema", type=pathlib.Path, default=SCHEMA_DEFAULT)
    parser.add_argument("--out", type=pathlib.Path, default=GENERATED_DIR)
    args = parser.parse_args()

    ops = load_schema(args.schema)
    ensure_dir(args.out)

    write_file(args.out / "InlineHandlersDecl.inc", emit_inline_decls(ops))
    write_file(args.out / "InlineHandlersImpl.inc", emit_inline_impls(ops))
    write_file(args.out / "SwitchDispatchDecl.inc", emit_switch_decl(ops))
    write_file(args.out / "SwitchDispatchImpl.inc", emit_switch_impl(ops))
    write_file(args.out / "ThreadedLabels.inc", emit_threaded_labels(ops))
    write_file(args.out / "ThreadedCases.inc", emit_threaded_cases(ops))
    write_file(args.out / "HandlerTable.hpp", emit_handler_table(ops))
    write_file(args.out / "OpSchema.hpp", emit_schema_header(ops))


if __name__ == "__main__":
    main()
