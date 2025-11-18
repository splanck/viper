//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/AsmEmitter.cpp
// Purpose: Minimal assembly emission helpers for the AArch64 backend.
//
//===----------------------------------------------------------------------===//

#include "AsmEmitter.hpp"

#include <iomanip>

namespace viper::codegen::aarch64
{

void AsmEmitter::emitFunctionHeader(std::ostream &os, const std::string &name) const
{
    // Keep directives minimal and assembler-agnostic for testing.
    os << ".text\n";
    os << ".align 2\n";
    os << ".globl " << name << "\n";
    os << name << ":\n";
}

void AsmEmitter::emitPrologue(std::ostream &os) const
{
    // stp x29, x30, [sp, #-16]!; mov x29, sp
    os << "  stp x29, x30, [sp, #-16]!\n";
    os << "  mov x29, sp\n";
}

void AsmEmitter::emitEpilogue(std::ostream &os) const
{
    // ldp x29, x30, [sp], #16; ret
    os << "  ldp x29, x30, [sp], #16\n";
    os << "  ret\n";
}

void AsmEmitter::emitMovRR(std::ostream &os, PhysReg dst, PhysReg src) const
{
    os << "  mov " << rn(dst) << ", " << rn(src) << "\n";
}

void AsmEmitter::emitMovRI(std::ostream &os, PhysReg dst, long long imm) const
{
    os << "  mov " << rn(dst) << ", #" << imm << "\n";
}

void AsmEmitter::emitAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  add " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  sub " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    // AArch64 mul: mul xd, xn, xm
    os << "  mul " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitAddRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const
{
    os << "  add " << rn(dst) << ", " << rn(lhs) << ", #" << imm << "\n";
}

void AsmEmitter::emitSubRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const
{
    os << "  sub " << rn(dst) << ", " << rn(lhs) << ", #" << imm << "\n";
}

void AsmEmitter::emitAndRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  and " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitOrrRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  orr " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitEorRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  eor " << rn(dst) << ", " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitLslRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const
{
    os << "  lsl " << rn(dst) << ", " << rn(lhs) << ", #" << sh << "\n";
}

void AsmEmitter::emitLsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const
{
    os << "  lsr " << rn(dst) << ", " << rn(lhs) << ", #" << sh << "\n";
}

void AsmEmitter::emitAsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const
{
    os << "  asr " << rn(dst) << ", " << rn(lhs) << ", #" << sh << "\n";
}

void AsmEmitter::emitCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  cmp " << rn(lhs) << ", " << rn(rhs) << "\n";
}

void AsmEmitter::emitCmpRI(std::ostream &os, PhysReg lhs, long long imm) const
{
    os << "  cmp " << rn(lhs) << ", #" << imm << "\n";
}

void AsmEmitter::emitCset(std::ostream &os, PhysReg dst, const char *cond) const
{
    os << "  cset " << rn(dst) << ", " << cond << "\n";
}

void AsmEmitter::emitMovZ(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movz " << rn(dst) << ", #" << imm16;
    if (lsl) os << ", lsl #" << lsl;
    os << "\n";
}

void AsmEmitter::emitMovK(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movk " << rn(dst) << ", #" << imm16;
    if (lsl) os << ", lsl #" << lsl;
    os << "\n";
}

void AsmEmitter::emitMovImm64(std::ostream &os, PhysReg dst, unsigned long long value) const
{
    unsigned chunks[4] = {
        static_cast<unsigned>(value & 0xFFFFULL),
        static_cast<unsigned>((value >> 16) & 0xFFFFULL),
        static_cast<unsigned>((value >> 32) & 0xFFFFULL),
        static_cast<unsigned>((value >> 48) & 0xFFFFULL),
    };
    // Always start with MOVZ of the low 16 bits
    emitMovZ(os, dst, chunks[0], 0);
    if (chunks[1]) emitMovK(os, dst, chunks[1], 16);
    if (chunks[2]) emitMovK(os, dst, chunks[2], 32);
    if (chunks[3]) emitMovK(os, dst, chunks[3], 48);
}

void AsmEmitter::emitRet(std::ostream &os) const
{
    os << "  ret\n";
}

void AsmEmitter::emitFunction(std::ostream &os, const MFunction &fn) const
{
    emitFunctionHeader(os, fn.name);
    emitPrologue(os);
    for (const auto &bb : fn.blocks)
        emitBlock(os, bb);
    emitEpilogue(os);
}

void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &bb) const
{
    (void)bb; // unnamed blocks for now
    for (const auto &mi : bb.instrs)
        emitInstruction(os, mi);
}

void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &mi) const
{
    using K = MOpcode;
    auto reg = [](const MOperand &op) { return op.reg; };
    auto imm = [](const MOperand &op) { return op.imm; };
    switch (mi.opc)
    {
        case K::MovRR: emitMovRR(os, reg(mi.ops[0]), reg(mi.ops[1])); break;
        case K::MovRI: emitMovRI(os, reg(mi.ops[0]), imm(mi.ops[1])); break;
        case K::AddRRR: emitAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2])); break;
        case K::SubRRR: emitSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2])); break;
        case K::MulRRR: emitMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2])); break;
        case K::AddRI: emitAddRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2])); break;
        case K::SubRI: emitSubRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2])); break;
        case K::LslRI: emitLslRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2])); break;
        case K::LsrRI: emitLsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2])); break;
        case K::AsrRI: emitAsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2])); break;
        case K::CmpRR: emitCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1])); break;
        case K::CmpRI: emitCmpRI(os, reg(mi.ops[0]), imm(mi.ops[1])); break;
        case K::Cset: emitCset(os, reg(mi.ops[0]), mi.ops[1].cond); break;
        default: break;
    }
}

} // namespace viper::codegen::aarch64
