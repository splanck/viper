#!/usr/bin/env python3
"""
Generate x86_64 encoding tables from JSON specification.

This script reads a JSON file defining x86_64 instruction encodings and
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

def generate_encoding_table(spec: Dict[str, Any]) -> str:
    """Generate the kEncodingTable array."""
    lines = []
    lines.append("// Generated from tools/spec/x86_64_encodings.json")
    lines.append("// DO NOT EDIT - regenerate with tools/gen_x86_64_encodings.py")
    lines.append("")
    lines.append("static constexpr std::array<EncodingRow, 44> kEncodingTable = {{")

    for entry in spec['encodings']:
        opcode = entry['opcode']
        mnemonic = entry['mnemonic']
        form = entry['form']
        order = entry['order']
        pattern = entry['pattern']
        flags = entry.get('flags', [])

        # Format the pattern
        pattern_args = []
        for p in pattern:
            pattern_args.append(f"OperandKind::{p}")
        # Pad with None if less than 3 args
        while len(pattern_args) < 3:
            pattern_args.append("OperandKind::None")
        pattern_str = f"makePattern({', '.join(pattern_args[:3])})"

        # Format the flags
        if flags:
            flags_str = " | ".join(f"EncodingFlag::{f}" for f in flags)
        else:
            flags_str = "EncodingFlag::None"

        lines.append(f"    {{MOpcode::{opcode},")
        lines.append(f'     "{mnemonic}",')
        lines.append(f"     EncodingForm::{form},")
        lines.append(f"     OperandOrder::{order},")
        lines.append(f"     {pattern_str},")
        lines.append(f"     {flags_str}}},")

    lines.append("}};")
    lines.append("")

    return '\n'.join(lines)

def generate_opfmt_table(spec: Dict[str, Any]) -> str:
    """Generate the kOpFmt array."""
    lines = []
    lines.append("// Generated from tools/spec/x86_64_encodings.json")
    lines.append("// DO NOT EDIT - regenerate with tools/gen_x86_64_encodings.py")
    lines.append("")
    lines.append("static constexpr std::array<OpFmt, 44> kOpFmt = {{")

    # Map certain conditions to format flags
    fmt_flags_map = {
        'LEA': 'kFmtLea',
        'SHIFT': 'kFmtShift',
        'MOVZX_RR8': 'kFmtMovzx8',
        'CALL': 'kFmtCall',
        'JUMP': 'kFmtJump',
        'JCC': 'kFmtJump | kFmtCond',
        'SETCC': 'kFmtCond | kFmtSetcc',
    }

    for entry in spec['encodings']:
        opcode = entry['opcode']
        mnemonic = entry['mnemonic']
        pattern = entry['pattern']
        order = entry['order']
        form = entry['form']

        # Calculate operand count
        operand_count = len(pattern)

        # Determine format flags based on order and form
        flags = []
        if order in fmt_flags_map:
            flags.append(fmt_flags_map[order])
        elif order == 'DIRECT':
            flags.append('kFmtDirect')
        elif form == 'Call':
            flags.append('kFmtCall')
        elif form == 'Jump':
            flags.append('kFmtJump')
        elif form == 'Condition' and opcode == 'JCC':
            flags.append('kFmtJump | kFmtCond')
        elif form == 'Setcc':
            flags.append('kFmtCond | kFmtSetcc')

        if flags:
            if ' | ' in flags[0]:
                flags_str = f"static_cast<std::uint8_t>({flags[0]})"
            else:
                flags_str = flags[0]
        else:
            flags_str = "0U"

        lines.append(f"    {{MOpcode::{opcode}, \"{mnemonic}\", {operand_count}U, {flags_str}}},")

    lines.append("}};")
    lines.append("")

    return '\n'.join(lines)

def main():
    # Paths
    script_dir = Path(__file__).parent
    spec_file = script_dir / "spec" / "x86_64_encodings.json"
    output_dir = script_dir.parent / "src" / "codegen" / "x86_64" / "generated"

    # Create output directory if it doesn't exist
    output_dir.mkdir(parents=True, exist_ok=True)

    # Load specification
    spec = load_spec(spec_file)

    # Generate encoding table
    encoding_table = generate_encoding_table(spec)
    encoding_output = output_dir / "EncodingTable.inc"
    with open(encoding_output, 'w') as f:
        f.write(encoding_table)
    print(f"Generated {encoding_output}")

    # Generate OpFmt table
    opfmt_table = generate_opfmt_table(spec)
    opfmt_output = output_dir / "OpFmtTable.inc"
    with open(opfmt_output, 'w') as f:
        f.write(opfmt_table)
    print(f"Generated {opfmt_output}")

    print("Code generation complete!")

    return 0

if __name__ == "__main__":
    sys.exit(main())