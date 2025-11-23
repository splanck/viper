#!/usr/bin/env python3
"""
Generate AArch64 opcode dispatch table from JSON specification.

This script reads a JSON file defining AArch64 instruction encodings and
generates C++ include files for the AsmEmitter.
"""

import sys
import json
from pathlib import Path
from typing import List, Dict, Any

def load_spec(spec_file: Path) -> Dict[str, Any]:
    """Load the JSON specification file."""
    with open(spec_file, 'r') as f:
        return json.load(f)

def generate_dispatch_table(spec: Dict[str, Any]) -> str:
    """Generate the opcode dispatch switch statement."""
    lines = []
    lines.append("// Generated from tools/spec/aarch64_encodings.json")
    lines.append("// DO NOT EDIT - regenerate with tools/gen_aarch64_encodings.py")
    lines.append("")
    lines.append("// This file contains the opcode dispatch logic for emitInstruction")
    lines.append("")

    # Generate the switch body
    lines.append("    using K = MOpcode;")
    lines.append("    auto reg = [](const MOperand &op) {")
    lines.append("        assert(op.kind == MOperand::Kind::Reg && \"expected reg operand\");")
    lines.append("        assert(op.reg.isPhys && \"unallocated vreg reached emitter\");")
    lines.append("        return static_cast<PhysReg>(op.reg.idOrPhys);")
    lines.append("    };")
    lines.append("    auto imm = [](const MOperand &op) { return op.imm; };")
    lines.append("    switch (mi.opc)")
    lines.append("    {")

    for opcode in spec['opcodes']:
        enum = opcode['enum']
        lines.append(f"        case K::{enum}:")

        # Handle special cases
        if enum == "MovRI":
            lines.append("        {")
            lines.append("            const long long v = imm(mi.ops[1]);")
            lines.append("            if (v >= 0 && v <= 65535)")
            lines.append("                emitMovRI(os, reg(mi.ops[0]), v);")
            lines.append("            else")
            lines.append("                emitMovImm64(os, reg(mi.ops[0]), static_cast<unsigned long long>(v));")
            lines.append("            break;")
            lines.append("        }")
        elif enum == "FMovRI":
            lines.append("        {")
            lines.append("            const long long bits = imm(mi.ops[1]);")
            lines.append("            double dv;")
            lines.append("            static_assert(sizeof(long long) == sizeof(double), \"size\");")
            lines.append("            std::memcpy(&dv, &bits, sizeof(double));")
            lines.append("            emitFMovRI(os, reg(mi.ops[0]), dv);")
            lines.append("            break;")
            lines.append("        }")
        elif enum == "Br":
            lines.append("            os << \"  b \" << mi.ops[0].label << \"\\n\";")
            lines.append("            break;")
        elif enum == "BCond":
            lines.append("            os << \"  b.\" << mi.ops[0].cond << \" \" << mi.ops[1].label << \"\\n\";")
            lines.append("            break;")
        elif enum == "Bl":
            lines.append("            os << \"  bl \" << mi.ops[0].label << \"\\n\";")
            lines.append("            break;")
        elif enum == "AdrPage":
            lines.append("            os << \"  adrp \" << rn(reg(mi.ops[0])) << \", \" << mi.ops[1].label << \"@PAGE\\n\";")
            lines.append("            break;")
        elif enum == "AddPageOff":
            lines.append("            os << \"  add \" << rn(reg(mi.ops[0])) << \", \" << rn(reg(mi.ops[1])) << \", \"")
            lines.append("               << mi.ops[2].label << \"@PAGEOFF\\n\";")
            lines.append("            break;")
        elif enum == "Cset":
            lines.append("            emitCset(os, reg(mi.ops[0]), mi.ops[1].cond);")
            lines.append("            break;")
        else:
            # Generate standard emit call - map to actual function names
            func_name_map = {
                'SubSpImm': 'SubSp',
                'AddSpImm': 'AddSp',
                'StrRegSpImm': 'StrToSp',
                'StrFprSpImm': 'StrFprToSp',
                'LdrRegFpImm': 'LdrFromFp',
                'StrRegFpImm': 'StrToFp',
                'LdrFprFpImm': 'LdrFprFromFp',
                'StrFprFpImm': 'StrFprToFp',
            }
            base_func = func_name_map.get(enum, enum)
            emit_func = f"emit{base_func}"
            args = []

            # Determine operand extraction based on format
            format_type = opcode.get('format', '')
            operands = opcode.get('operands', [])

            for i, operand_spec in enumerate(operands):
                name, typ = operand_spec.split(':')
                if typ in ['reg', 'gpr', 'fpr']:
                    args.append(f"reg(mi.ops[{i}])")
                elif typ in ['imm', 'f64']:
                    args.append(f"imm(mi.ops[{i}])")
                elif typ == 'cond':
                    args.append(f"mi.ops[{i}].cond")
                elif typ == 'label':
                    args.append(f"mi.ops[{i}].label")

            if args:
                lines.append(f"            {emit_func}(os, {', '.join(args)});")
            else:
                lines.append(f"            {emit_func}(os);")
            lines.append("            break;")

    lines.append("        default:")
    lines.append("            os << \"  # <unknown opcode>\\n\";")
    lines.append("            break;")
    lines.append("    }")
    lines.append("")

    return '\n'.join(lines)

def main():
    # Paths
    script_dir = Path(__file__).parent
    spec_file = script_dir / "spec" / "aarch64_encodings.json"
    output_dir = script_dir.parent / "src" / "codegen" / "aarch64" / "generated"

    # Create output directory if it doesn't exist
    output_dir.mkdir(parents=True, exist_ok=True)

    # Load specification
    spec = load_spec(spec_file)

    # Generate dispatch table
    dispatch_table = generate_dispatch_table(spec)
    dispatch_output = output_dir / "OpcodeDispatch.inc"
    with open(dispatch_output, 'w') as f:
        f.write(dispatch_table)
    print(f"Generated {dispatch_output}")

    print("Code generation complete!")

    return 0

if __name__ == "__main__":
    sys.exit(main())