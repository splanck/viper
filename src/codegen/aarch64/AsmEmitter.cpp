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
    emitPrologue(os);
    // Allocate space for locals if needed
    if (plan.localFrameSize > 0)
    {
        emitSubSp(os, plan.localFrameSize);
    }
    // Save additional callee-saved GPRs from plan in pairs
    for (std::size_t i = 0; i < plan.saveGPRs.size();)
    {
        const PhysReg r0 = plan.saveGPRs[i++];
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
    for (std::size_t i = 0; i < plan.saveFPRs.size();)
    {
        const PhysReg r0 = plan.saveFPRs[i++];
        if (i < plan.saveFPRs.size())
        {
            const PhysReg r1 = plan.saveFPRs[i++];
            os << "  stp ";
            printD(os, r0);
            os << ", ";
            printD(os, r1);
            os << ", [sp, #-16]!\n";
        }
        else
        {
            os << "  str ";
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
    if (n % 2 == 1)
    {
        const PhysReg r0 = plan.saveGPRs[n - 1];
        os << "  ldr " << rn(r0) << ", [sp], #16\n";
        --n;
    }
    while (n > 0)
    {
        const PhysReg r1 = plan.saveGPRs[n - 1];
        const PhysReg r0 = plan.saveGPRs[n - 2];
        os << "  ldp " << rn(r0) << ", " << rn(r1) << ", [sp], #16\n";
        n -= 2;
    }
    // Restore FPRs in reverse
    std::size_t nf = plan.saveFPRs.size();
    if (nf % 2 == 1)
    {
        const PhysReg r0 = plan.saveFPRs[nf - 1];
        os << "  ldr ";
        printD(os, r0);
        os << ", [sp], #16\n";
        --nf;
    }
    while (nf > 0)
    {
        const PhysReg r1 = plan.saveFPRs[nf - 1];
        const PhysReg r0 = plan.saveFPRs[nf - 2];
        os << "  ldp ";
        printD(os, r0);
        os << ", ";
        printD(os, r1);
        os << ", [sp], #16\n";
        nf -= 2;
    }
    // Deallocate local frame if needed
    if (plan.localFrameSize > 0)
    {
        emitAddSp(os, plan.localFrameSize);
    }
    // Finally FP/LR and ret
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
    printD(os, dst);
    os << ", [x29, #" << offset << "]\n";
}

void AsmEmitter::emitStrFprToFp(std::ostream &os, PhysReg src, long long offset) const
{
    os << "  str ";
    printD(os, src);
    os << ", [x29, #" << offset << "]\n";
}

void AsmEmitter::emitMovZ(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movz " << rn(dst) << ", #" << imm16;
    if (lsl)
        os << ", lsl #" << lsl;
    os << "\n";
}

void AsmEmitter::emitMovK(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const
{
    os << "  movk " << rn(dst) << ", #" << imm16;
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
    emitMovZ(os, dst, chunks[0], 0);
    if (chunks[1])
        emitMovK(os, dst, chunks[1], 16);
    if (chunks[2])
        emitMovK(os, dst, chunks[2], 32);
    if (chunks[3])
        emitMovK(os, dst, chunks[3], 48);
}

void AsmEmitter::emitRet(std::ostream &os) const
{
    os << "  ret\n";
}

void AsmEmitter::emitFMovRR(std::ostream &os, PhysReg dst, PhysReg src) const
{
    os << "  fmov ";
    printD(os, dst);
    os << ", ";
    printD(os, src);
    os << "\n";
}

void AsmEmitter::emitFMovRI(std::ostream &os, PhysReg dst, double imm) const
{
    os << std::fixed;
    os << "  fmov ";
    printD(os, dst);
    os << ", #" << imm << "\n";
}

void AsmEmitter::emitFAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fadd ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fsub ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fmul ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const
{
    os << "  fdiv ";
    printD(os, dst);
    os << ", ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitFCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const
{
    os << "  fcmp ";
    printD(os, lhs);
    os << ", ";
    printD(os, rhs);
    os << "\n";
}

void AsmEmitter::emitSCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const
{
    os << "  scvtf ";
    printD(os, dstFPR);
    os << ", " << rn(srcGPR) << "\n";
}

void AsmEmitter::emitFCvtZS(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const
{
    os << "  fcvtzs " << rn(dstGPR) << ", ";
    printD(os, srcFPR);
    os << "\n";
}

void AsmEmitter::emitFunction(std::ostream &os, const MFunction &fn) const
{
    emitFunctionHeader(os, fn.name);
    const bool usePlan = !fn.savedGPRs.empty() || fn.localFrameSize > 0;
    FramePlan plan;
    if (usePlan)
    {
        plan.saveGPRs = fn.savedGPRs;
        plan.saveFPRs = fn.savedFPRs;
        plan.localFrameSize = fn.localFrameSize;
    }
    if (usePlan)
        emitPrologue(os, plan);
    else
        emitPrologue(os);
    for (const auto &bb : fn.blocks)
        emitBlock(os, bb);
    if (usePlan)
        emitEpilogue(os, plan);
    else
        emitEpilogue(os);
}

void AsmEmitter::emitBlock(std::ostream &os, const MBasicBlock &bb) const
{
    if (!bb.name.empty())
        os << bb.name << ":\n";
    for (const auto &mi : bb.instrs)
        emitInstruction(os, mi);
}

void AsmEmitter::emitInstruction(std::ostream &os, const MInstr &mi) const
{
    using K = MOpcode;
    auto reg = [](const MOperand &op) {
        assert(op.kind == MOperand::Kind::Reg && "expected reg operand");
        assert(op.reg.isPhys && "unallocated vreg reached emitter");
        return static_cast<PhysReg>(op.reg.idOrPhys);
    };
    auto imm = [](const MOperand &op) { return op.imm; };
    switch (mi.opc)
    {
        case K::MovRR:
            emitMovRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::MovRI:
        {
            const long long v = imm(mi.ops[1]);
            if (v >= 0 && v <= 65535)
                emitMovRI(os, reg(mi.ops[0]), v);
            else
                emitMovImm64(os, reg(mi.ops[0]), static_cast<unsigned long long>(v));
            break;
        }
        case K::FMovRR:
            emitFMovRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::FMovRI:
        {
            const long long bits = imm(mi.ops[1]);
            double dv;
            static_assert(sizeof(long long) == sizeof(double), "size");
            std::memcpy(&dv, &bits, sizeof(double));
            emitFMovRI(os, reg(mi.ops[0]), dv);
            break;
        }
        case K::FAddRRR:
            emitFAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FSubRRR:
            emitFSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FMulRRR:
            emitFMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FDivRRR:
            emitFDivRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::FCmpRR:
            emitFCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::SCvtF:
            emitSCvtF(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::FCvtZS:
            emitFCvtZS(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::AddRRR:
            emitAddRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::SubRRR:
            emitSubRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::MulRRR:
            emitMulRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::AndRRR:
            emitAndRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::OrrRRR:
            emitOrrRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::EorRRR:
            emitEorRRR(os, reg(mi.ops[0]), reg(mi.ops[1]), reg(mi.ops[2]));
            break;
        case K::AddRI:
            emitAddRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::SubRI:
            emitSubRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::LslRI:
            emitLslRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::LsrRI:
            emitLsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::AsrRI:
            emitAsrRI(os, reg(mi.ops[0]), reg(mi.ops[1]), imm(mi.ops[2]));
            break;
        case K::CmpRR:
            emitCmpRR(os, reg(mi.ops[0]), reg(mi.ops[1]));
            break;
        case K::CmpRI:
            emitCmpRI(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::Cset:
            emitCset(os, reg(mi.ops[0]), mi.ops[1].cond);
            break;
        case K::SubSpImm:
            emitSubSp(os, imm(mi.ops[0]));
            break;
        case K::AddSpImm:
            emitAddSp(os, imm(mi.ops[0]));
            break;
        case K::StrRegSpImm:
            emitStrToSp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::StrFprSpImm:
            emitStrFprToSp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::LdrRegFpImm:
            emitLdrFromFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::StrRegFpImm:
            emitStrToFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::LdrFprFpImm:
            emitLdrFprFromFp(os, reg(mi.ops[0]), imm(mi.ops[1]));
            break;
        case K::StrFprFpImm:
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
