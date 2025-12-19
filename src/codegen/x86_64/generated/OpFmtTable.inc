//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// IMPORTANT: This file is auto-generated. Do not edit manually.
//
// File: codegen/x86_64/generated/OpFmtTable.inc
// Purpose: Instruction mnemonic and operand format table for x86-64.
//
// Generator: cmake/GenX86Encodings.cmake
// Source: tools/spec/x86_64_encodings.json
// Regenerate: cmake --build build (automatic on spec change)
//
// Description: Contains OpFmt entries with mnemonic strings and operand
//              format information for assembly output.
//
// Links: docs/il-guide.md#reference, codegen/x86_64/Emit.cpp
//
//===----------------------------------------------------------------------===//

static constexpr std::array<OpFmt, 44> kOpFmt = {{
    {MOpcode::MOVrr, "movq", 2U, 0U},
    {MOpcode::MOVri, "movq", 2U, 0U},
    {MOpcode::CMOVNErr, "cmovne", 2U, 0U},
    {MOpcode::LEA, "leaq", 2U, kFmtLea},
    {MOpcode::ADDrr, "addq", 2U, 0U},
    {MOpcode::ADDri, "addq", 2U, 0U},
    {MOpcode::ANDrr, "andq", 2U, 0U},
    {MOpcode::ANDri, "andq", 2U, 0U},
    {MOpcode::ORrr, "orq", 2U, 0U},
    {MOpcode::ORri, "orq", 2U, 0U},
    {MOpcode::XORrr, "xorq", 2U, 0U},
    {MOpcode::XORri, "xorq", 2U, 0U},
    {MOpcode::SUBrr, "subq", 2U, 0U},
    {MOpcode::SHLri, "shlq", 2U, 0U},
    {MOpcode::SHLrc, "shlq", 2U, kFmtShift},
    {MOpcode::SHRri, "shrq", 2U, 0U},
    {MOpcode::SHRrc, "shrq", 2U, kFmtShift},
    {MOpcode::SARri, "sarq", 2U, 0U},
    {MOpcode::SARrc, "sarq", 2U, kFmtShift},
    {MOpcode::IMULrr, "imulq", 2U, 0U},
    {MOpcode::CQO, "cqto", 0U, 0U},
    {MOpcode::IDIVrm, "idivq", 1U, kFmtDirect},
    {MOpcode::DIVrm, "divq", 1U, kFmtDirect},
    {MOpcode::XORrr32, "xorl", 2U, 0U},
    {MOpcode::CMPrr, "cmpq", 2U, 0U},
    {MOpcode::CMPri, "cmpq", 2U, 0U},
    {MOpcode::SETcc, "set", 2U, static_cast<std::uint8_t>(kFmtCond | kFmtSetcc)},
    {MOpcode::MOVZXrr32, "movzbq", 2U, kFmtMovzx8},
    {MOpcode::TESTrr, "testq", 2U, 0U},
    {MOpcode::JMP, "jmp", 1U, kFmtJump},
    {MOpcode::JCC, "j", 2U, static_cast<std::uint8_t>(kFmtJump | kFmtCond)},
    {MOpcode::CALL, "callq", 1U, kFmtCall},
    {MOpcode::UD2, "ud2", 0U, 0U},
    {MOpcode::RET, "ret", 0U, 0U},
    {MOpcode::FADD, "addsd", 2U, 0U},
    {MOpcode::FSUB, "subsd", 2U, 0U},
    {MOpcode::FMUL, "mulsd", 2U, 0U},
    {MOpcode::FDIV, "divsd", 2U, 0U},
    {MOpcode::UCOMIS, "ucomisd", 2U, 0U},
    {MOpcode::CVTSI2SD, "cvtsi2sdq", 2U, 0U},
    {MOpcode::CVTTSD2SI, "cvttsd2siq", 2U, 0U},
    {MOpcode::MOVSDrr, "movsd", 2U, 0U},
    {MOpcode::MOVSDrm, "movsd", 2U, 0U},
    {MOpcode::MOVSDmr, "movsd", 2U, 0U},
}};
