//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include <cstring>

namespace viper::codegen::aarch64
{

void AsmEmitter::emitFunctionHeader(std::ostream &os, const std::string &name) const
{
    // Keep directives minimal and assembler-agnostic for testing.
    os << ".text\n";
    os << ".align 2\n";
    // Do not mangle or prefix symbols (e.g., with '_' on Darwin); tests expect
    // the exact function name to appear in the global directive and label.
    const std::string sym = name;
    os << ".globl " << sym << "\n";
    os << sym << ":\n";
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

void AsmEmitter::emitPrologue(std::ostream &os, const FramePlan &plan) const
{
    // Always save FP/LR first
/// @brief Emits prologue.
/// @param os Parameter description needed.
/// @return Return value description needed.
    emitPrologue(os);
    // Allocate space for locals if needed
/// @brief Implements if functionality.
/// @param 0 Parameter description needed.
/// @return Return value description needed.
    if (plan.localFrameSize > 0)
    {
/// @brief Emits subsp.
/// @param os Parameter description needed.
/// @param plan.localFrameSize Parameter description needed.
/// @return Return value description needed.
        emitSubSp(os, plan.localFrameSize);
    }
    // Save additional callee-saved GPRs from plan in pairs
/// @brief Implements for functionality.
/// @param plan.saveGPRs.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < plan.saveGPRs.size();)
    {
        const PhysReg r0 = plan.saveGPRs[i++];
/// @brief Implements if functionality.
/// @param plan.saveGPRs.size( Parameter description needed.
/// @return Return value description needed.
        if (i < plan.saveGPRs.size())
        {
            const PhysReg r1 = plan.saveGPRs[i++];
            os << "  stp " << rn(r0) << ", " << rn(r1) << ", [sp, #-16]!\n";
        }
        else
        {
            os << "  str " << rn(r0) << ", [sp, #-16]!\n";
        }
    }
    // Save callee-saved FPRs (D-registers) in pairs
/// @brief Implements for functionality.
/// @param plan.saveFPRs.size( Parameter description needed.
/// @return Return value description needed.
    for (std::size_t i = 0; i < plan.saveFPRs.size();)
    {
        const PhysReg r0 = plan.saveFPRs[i++];
/// @brief Implements if functionality.
/// @param plan.saveFPRs.size( Parameter description needed.
/// @return Return value description needed.
        if (i < plan.saveFPRs.size())
        {
            const PhysReg r1 = plan.saveFPRs[i++];
            os << "  stp ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param r0 Parameter description needed.
/// @return Return value description needed.
            printD(os, r0);
            os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param r1 Parameter description needed.
/// @return Return value description needed.
            printD(os, r1);
            os << ", [sp, #-16]!\n";
        }
        else
        {
            os << "  str ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param r0 Parameter description needed.
/// @return Return value description needed.
            printD(os, r0);
            os << ", [sp, #-16]!\n";
        }
    }
}

void AsmEmitter::emitEpilogue(std::ostream &os, const FramePlan &plan) const
{
    // Restore in reverse order of saves, then FP/LR
    // Compute how many stores we emitted.
    std::size_t n = plan.saveGPRs.size();
    // If odd, we restored one with STR/then LDR in reverse
/// @brief Implements if functionality.
/// @param 1 Parameter description needed.
/// @return Return value description needed.
    if (n % 2 == 1)
    {
        const PhysReg r0 = plan.saveGPRs[n - 1];
        os << "  ldr " << rn(r0) << ", [sp], #16\n";
        --n;
    }
/// @brief Implements while functionality.
/// @param 0 Parameter description needed.
/// @return Return value description needed.
    while (n > 0)
    {
        const PhysReg r1 = plan.saveGPRs[n - 1];
        const PhysReg r0 = plan.saveGPRs[n - 2];
        os << "  ldp " << rn(r0) << ", " << rn(r1) << ", [sp], #16\n";
        n -= 2;
    }
    // Restore FPRs in reverse
    std::size_t nf = plan.saveFPRs.size();
/// @brief Implements if functionality.
/// @param 1 Parameter description needed.
/// @return Return value description needed.
    if (nf % 2 == 1)
    {
        const PhysReg r0 = plan.saveFPRs[nf - 1];
        os << "  ldr ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param r0 Parameter description needed.
/// @return Return value description needed.
        printD(os, r0);
        os << ", [sp], #16\n";
        --nf;
    }
/// @brief Implements while functionality.
/// @param 0 Parameter description needed.
/// @return Return value description needed.
    while (nf > 0)
    {
        const PhysReg r1 = plan.saveFPRs[nf - 1];
        const PhysReg r0 = plan.saveFPRs[nf - 2];
        os << "  ldp ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param r0 Parameter description needed.
/// @return Return value description needed.
        printD(os, r0);
        os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param r1 Parameter description needed.
/// @return Return value description needed.
        printD(os, r1);
        os << ", [sp], #16\n";
        nf -= 2;
    }
    // Deallocate local frame if needed
/// @brief Implements if functionality.
/// @param 0 Parameter description needed.
/// @return Return value description needed.
    if (plan.localFrameSize > 0)
    {
/// @brief Emits addsp.
/// @param os Parameter description needed.
/// @param plan.localFrameSize Parameter description needed.
/// @return Return value description needed.
        emitAddSp(os, plan.localFrameSize);
    }
    // Finally FP/LR and ret
/// @brief Emits epilogue.
/// @param os Parameter description needed.
/// @return Return value description needed.
    emitEpilogue(os);
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

void AsmEmitter::emitSubSp(std::ostream &os, long long bytes) const
{
    os << "  sub sp, sp, #" << bytes << "\n";
}

void AsmEmitter::emitAddSp(std::ostream &os, long long bytes) const
{
    os << "  add sp, sp, #" << bytes << "\n";
}

void AsmEmitter::emitStrToSp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str " << rn(src) << ", [sp, #" << offset << "]\n";
}

void AsmEmitter::emitStrFprToSp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param src Parameter description needed.
/// @return Return value description needed.
    printD(os, src);
    os << ", [sp, #" << offset << "]\n";
}

void AsmEmitter::emitLdrFromFp(std::ostream &os, PhysReg dst, long long offset) const
{
    os << "  ldr " << rn(dst) << ", [x29, #" << offset << "]\n";
}

void AsmEmitter::emitStrToFp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str " << rn(src) << ", [x29, #" << offset << "]\n";
}

void AsmEmitter::emitLdrFprFromFp(std::ostream &os, PhysReg dst, long long offset) const
{
    os << "  ldr ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", [x29, #" << offset << "]\n";
}

void AsmEmitter::emitStrFprToFp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param src Parameter description needed.
/// @return Return value description needed.
    printD(os, src);
    os << ", [x29, #" << offset << "]\n";
}

void AsmEmitter::emitMovZ(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movz " << rn(dst) << ", #" << imm16;
/// @brief Implements if functionality.
/// @param lsl Parameter description needed.
/// @return Return value description needed.
    if (lsl)
        os << ", lsl #" << lsl;
    os << "\n";
}

void AsmEmitter::emitMovK(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movk " << rn(dst) << ", #" << imm16;
/// @brief Implements if functionality.
/// @param lsl Parameter description needed.
/// @return Return value description needed.
    if (lsl)
        os << ", lsl #" << lsl;
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
/// @brief Emits movz.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @param chunks[0] Parameter description needed.
/// @param 0 Parameter description needed.
/// @return Return value description needed.
    emitMovZ(os, dst, chunks[0], 0);
/// @brief Implements if functionality.
/// @param chunks[1] Parameter description needed.
/// @return Return value description needed.
    if (chunks[1])
/// @brief Emits movk.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @param chunks[1] Parameter description needed.
/// @param 16 Parameter description needed.
/// @return Return value description needed.
        emitMovK(os, dst, chunks[1], 16);
/// @brief Implements if functionality.
/// @param chunks[2] Parameter description needed.
/// @return Return value description needed.
    if (chunks[2])
/// @brief Emits movk.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @param chunks[2] Parameter description needed.
/// @param 32 Parameter description needed.
/// @return Return value description needed.
        emitMovK(os, dst, chunks[2], 32);
/// @brief Implements if functionality.
/// @param chunks[3] Parameter description needed.
/// @return Return value description needed.
    if (chunks[3])
/// @brief Emits movk.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @param chunks[3] Parameter description needed.
/// @param 48 Parameter description needed.
/// @return Return value description needed.
        emitMovK(os, dst, chunks[3], 48);
}

void AsmEmitter::emitRet(std::ostream &os) const
{
    os << "  ret\n";
}

void AsmEmitter::emitFMovRR(std::ostream &os, PhysReg dst, PhysReg src) const
{
    os << "  fmov ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param src Parameter description needed.
/// @return Return value description needed.
    printD(os, src);
    os << "\n";
}

void AsmEmitter::emitFMovRI(std::ostream &os, PhysReg dst, double imm) const
{
    os << std::fixed;
    os << "  fmov ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", #" << imm << "\n";
}

void AsmEmitter::emitFAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fadd ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param lhs Parameter description needed.
/// @return Return value description needed.
    printD(os, lhs);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param rhs Parameter description needed.
/// @return Return value description needed.
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fsub ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param lhs Parameter description needed.
/// @return Return value description needed.
    printD(os, lhs);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param rhs Parameter description needed.
/// @return Return value description needed.
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fmul ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param lhs Parameter description needed.
/// @return Return value description needed.
    printD(os, lhs);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param rhs Parameter description needed.
/// @return Return value description needed.
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fdiv ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dst Parameter description needed.
/// @return Return value description needed.
    printD(os, dst);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param lhs Parameter description needed.
/// @return Return value description needed.
    printD(os, lhs);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param rhs Parameter description needed.
/// @return Return value description needed.
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  fcmp ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param lhs Parameter description needed.
/// @return Return value description needed.
    printD(os, lhs);
    os << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param rhs Parameter description needed.
/// @return Return value description needed.
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitSCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const
{
    os << "  scvtf ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param dstFPR Parameter description needed.
/// @return Return value description needed.
    printD(os, dstFPR);
    os << ", " << rn(srcGPR) << "\n";
}

void AsmEmitter::emitFCvtZS(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const
{
    os << "  fcvtzs " << rn(dstGPR) << ", ";
/// @brief Implements printD functionality.
/// @param os Parameter description needed.
/// @param srcFPR Parameter description needed.
/// @return Return value description needed.
    printD(os, srcFPR);
    os << "\n";
}

void AsmEmitter::emitFunction(std::ostream &os, const MFunction &fn) const
{
/// @brief Emits functionheader.
/// @param os Parameter description needed.
/// @param fn.name Parameter description needed.
/// @return Return value description needed.
    emitFunctionHeader(os, fn.name);
    const bool usePlan = !fn.savedGPRs.empty() || fn.localFrameSize > 0;
    FramePlan plan;
/// @brief Implements if functionality.
/// @param usePlan Parameter description needed.
/// @return Return value description needed.
    if (usePlan)
    {
        plan.saveGPRs = fn.savedGPRs;
        plan.saveFPRs = fn.savedFPRs;
        plan.localFrameSize = fn.localFrameSize;
    }
/// @brief Implements if functionality.
/// @param usePlan Parameter description needed.
/// @return Return value description needed.
    if (usePlan)
/// @brief Emits prologue.
/// @param os Parameter description needed.
/// @param plan Parameter description needed.
/// @return Return value description needed.
        emitPrologue(os, plan);
    else
/// @brief Emits prologue.
/// @param os Parameter description needed.
/// @return Return value description needed.
        emitPrologue(os);
/// @brief Implements for functionality.
/// @param fn.blocks Parameter description needed.
/// @return Return value description needed.
    for (const auto &bb : fn.blocks)
/// @brief Emits block.
/// @param os Parameter description needed.
/// @param bb Parameter description needed.
/// @return Return value description needed.
        emitBlock(os, bb);
/// @brief Implements if functionality.
/// @param usePlan Parameter description needed.
/// @return Return value description needed.
    if (usePlan)
/// @brief Emits epilogue.
/// @param os Parameter description needed.
/// @param plan Parameter description needed.
/// @return Return value description needed.
        emitEpilogue(os, plan);
    else
/// @brief Emits epilogue.
/// @param os Parameter description needed.
/// @return Return value description needed.
        emitEpilogue(os);
}

void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &bb) const
{
/// @brief Implements if functionality.
/// @param !bb.name.empty( Parameter description needed.
/// @return Return value description needed.
    if (!bb.name.empty())
        os << bb.name << ":\n";
/// @brief Implements for functionality.
/// @param bb.instrs Parameter description needed.
/// @return Return value description needed.
    for (const auto &mi : bb.instrs)
/// @brief Emits instruction.
/// @param os Parameter description needed.
/// @param mi Parameter description needed.
/// @return Return value description needed.
        emitInstruction(os, mi);
}

void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &mi) const
{
    using K = MOpcode;
    auto reg = [](const MOperand &op) {
/// @brief Implements assert functionality.
/// @param operand" Parameter description needed.
/// @return Return value description needed.
        assert(op.kind == MOperand::Kind::Reg && "expected reg operand");
/// @brief Implements assert functionality.
/// @param emitter" Parameter description needed.
/// @return Return value description needed.
        assert(op.reg.isPhys && "unallocated vreg reached emitter");
        return static_cast<PhysReg>(op.reg.idOrPhys);
    };
    auto imm = [](const MOperand &op) { return op.imm; };
/// @brief Implements switch functionality.
/// @param mi.opc Parameter description needed.
/// @return Return value description needed.
    switch (mi.opc)
    {
        case K::MovRR:
/// @brief Emits movrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitMovRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::MovRI:
        {
            const long long v = imm(mi.ops[1]);
/// @brief Implements if functionality.
/// @param 65535 Parameter description needed.
/// @return Return value description needed.
            if (v >= 0 && v <= 65535)
/// @brief Emits movri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
                emitMovRI(os, reg(mi.ops[0]), v);
            else
/// @brief Emits movimm64.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
                emitMovImm64(os, reg(mi.ops[0]), static_cast<unsigned long long>(v));
            break;
        }
        case K::FMovRR:
/// @brief Emits fmovrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFMovRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::FMovRI:
        {
            const long long bits = imm(mi.ops[1]);
            double dv;
/// @brief Implements static_assert functionality.
/// @param long Parameter description needed.
/// @return Return value description needed.
            static_assert(sizeof(long long) == sizeof(double), "size");
            std::memcpy(&dv, &bits, sizeof(double));
/// @brief Emits fmovri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFMovRI(os, reg(mi.ops[0]), dv);
            break;
        }
        case K::FAddRRR:
/// @brief Emits faddrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FSubRRR:
/// @brief Emits fsubrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FMulRRR:
/// @brief Emits fmulrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FDivRRR:
/// @brief Emits fdivrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFDivRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FCmpRR:
/// @brief Emits fcmprr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::SCvtF:
/// @brief Emits scvtf.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitSCvtF(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::FCvtZS:
/// @brief Emits fcvtzs.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitFCvtZS(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::AddRRR:
/// @brief Emits addrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::SubRRR:
/// @brief Emits subrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::MulRRR:
/// @brief Emits mulrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::AndRRR:
/// @brief Emits andrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitAndRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::OrrRRR:
/// @brief Emits orrrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitOrrRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::EorRRR:
/// @brief Emits eorrrr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitEorRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::AddRI:
/// @brief Emits addri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitAddRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::SubRI:
/// @brief Emits subri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitSubRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::LslRI:
/// @brief Emits lslri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitLslRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::LsrRI:
/// @brief Emits lsrri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitLsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::AsrRI:
/// @brief Emits asrri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitAsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::CmpRR:
/// @brief Emits cmprr.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::CmpRI:
/// @brief Emits cmpri.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitCmpRI(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::Cset:
/// @brief Emits cset.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitCset(os, reg(mi.ops[0]), mi.ops[1].cond);
            break;
        case K::SubSpImm:
/// @brief Emits subsp.
/// @param os Parameter description needed.
/// @param imm(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitSubSp(os, imm(mi.ops[0]));
            break;
        case K::AddSpImm:
/// @brief Emits addsp.
/// @param os Parameter description needed.
/// @param imm(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitAddSp(os, imm(mi.ops[0]));
            break;
        case K::StrRegSpImm:
/// @brief Emits strtosp.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitStrToSp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::StrFprSpImm:
/// @brief Emits strfprtosp.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitStrFprToSp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::LdrRegFpImm:
/// @brief Emits ldrfromfp.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitLdrFromFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::StrRegFpImm:
/// @brief Emits strtofp.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitStrToFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::LdrFprFpImm:
/// @brief Emits ldrfprfromfp.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitLdrFprFromFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::StrFprFpImm:
/// @brief Emits strfprtofp.
/// @param os Parameter description needed.
/// @param reg(mi.ops[0] Parameter description needed.
/// @return Return value description needed.
            emitStrFprToFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::Br:
            os << "  b " << mi.ops[0].label << "\n";
            break;
        case K::BCond:
            os << "  b." << mi.ops[0].cond << " " << mi.ops[1].label << "\n";
            break;
        case K::Bl:
            os << "  bl " << mi.ops[0].label << "\n";
            break;
        case K::AdrPage:
            os << "  adrp " << rn(reg(mi.ops[0])) << ", " << mi.ops[1].label << "@PAGE\n";
            break;
        case K::AddPageOff:
            os << "  add " << rn(reg(mi.ops[0])) << ", " << rn(reg(mi.ops[1])) << ", "
               << mi.ops[2].label << "@PAGEOFF\n";
            break;
        default:
            break;
    }
}

} // namespace viper::codegen::aarch64
