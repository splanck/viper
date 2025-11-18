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

} // namespace viper::codegen::aarch64
