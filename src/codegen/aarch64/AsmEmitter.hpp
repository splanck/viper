//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/AsmEmitter.hpp
// Purpose: Minimal AArch64 assembly emitter for early backend scaffolding.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <string>

#include "codegen/aarch64/FramePlan.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

class AsmEmitter
{
  public:
    explicit AsmEmitter(const TargetInfo &target) noexcept : target_(&target) {}

    // Emit a simple function header; does not mangle names.
    void emitFunctionHeader(std::ostream &os, const std::string &name) const;

    // ABI-conformant frame prologue/epilogue for leaf-like functions.
    void emitPrologue(std::ostream &os) const;
    void emitEpilogue(std::ostream &os) const;
    // Prologue/epilogue honoring an explicit frame plan (callee-saved saves)
    void emitPrologue(std::ostream &os, const FramePlan &plan) const;
    void emitEpilogue(std::ostream &os, const FramePlan &plan) const;

    // Integer ops (64-bit): mov dst, src; add dst, lhs, rhs; ret
    void emitMovRR(std::ostream &os, PhysReg dst, PhysReg src) const;
    // mov dst, #imm (immediate move)
    void emitMovRI(std::ostream &os, PhysReg dst, long long imm) const;
    // Floating-point scalar (64-bit) ops
    void emitFMovRR(std::ostream &os, PhysReg dst, PhysReg src) const;
    void emitFMovRI(std::ostream &os, PhysReg dst, double imm) const;
    void emitFAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitFSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitFMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitFDivRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitFCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const;
    // Conversions
    void emitSCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const;
    void emitFCvtZS(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const;
    void emitUCvtF(std::ostream &os, PhysReg dstFPR, PhysReg srcGPR) const;
    void emitFCvtZU(std::ostream &os, PhysReg dstGPR, PhysReg srcFPR) const;
    void emitAddRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitSubRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitMulRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    // add/sub with small immediate
    void emitAddRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const;
    void emitSubRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long imm) const;
    // bitwise rr
    void emitAndRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitOrrRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    void emitEorRRR(std::ostream &os, PhysReg dst, PhysReg lhs, PhysReg rhs) const;
    // shifts by immediate
    void emitLslRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const;
    void emitLsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const;
    void emitAsrRI(std::ostream &os, PhysReg dst, PhysReg lhs, long long sh) const;

    // Compare and set
    void emitCmpRR(std::ostream &os, PhysReg lhs, PhysReg rhs) const;
    void emitCmpRI(std::ostream &os, PhysReg lhs, long long imm) const;
    void emitCset(std::ostream &os, PhysReg dst, const char *cond) const;

    // Wide immediate materialization (movz/movk sequence)
    void emitMovZ(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const;
    void emitMovK(std::ostream &os, PhysReg dst, unsigned imm16, unsigned lsl) const;
    void emitMovImm64(std::ostream &os, PhysReg dst, unsigned long long value) const;
    void emitRet(std::ostream &os) const;

    // Stack/Memory helpers for outgoing arg area
    void emitSubSp(std::ostream &os, long long bytes) const;
    void emitAddSp(std::ostream &os, long long bytes) const;
    void emitStrToSp(std::ostream &os, PhysReg src, long long offset) const;
    void emitStrFprToSp(std::ostream &os, PhysReg src, long long offset) const;

    // Load/store from frame pointer (for locals)
    void emitLdrFromFp(std::ostream &os, PhysReg dst, long long offset) const;
    void emitStrToFp(std::ostream &os, PhysReg src, long long offset) const;
    void emitLdrFprFromFp(std::ostream &os, PhysReg dst, long long offset) const;
    void emitStrFprToFp(std::ostream &os, PhysReg src, long long offset) const;

    // Load/store from arbitrary base register
    void emitLdrFromBase(std::ostream &os, PhysReg dst, PhysReg base, long long offset) const;
    void emitStrToBase(std::ostream &os, PhysReg src, PhysReg base, long long offset) const;

    // Emit from minimal MIR (Phase A)
    void emitFunction(std::ostream &os, const MFunction &fn) const;
    void emitBlock(std::ostream &os, const MBasicBlock &bb) const;
    void emitInstruction(std::ostream &os, const MInstr &mi) const;

  private:
    const TargetInfo *target_{nullptr};
    // Mutable state used during emitFunction to pass frame plan to Ret instructions
    mutable const FramePlan *currentPlan_{nullptr};
    mutable bool currentPlanValid_{false};

    [[nodiscard]] static const char *rn(PhysReg r) noexcept
    {
        return regName(r);
    }

    // Print FPR as dN (64-bit scalar view)
    static void printD(std::ostream &os, PhysReg r)
    {
        // Map Vn -> dn
        const char *name = regName(r); // e.g., "v8"
        if (name[0] == 'v')
        {
            os << 'd' << (name + 1);
        }
        else
        {
            // Fallback: if mis-specified, still print name
            os << name;
        }
    }
};

} // namespace viper::codegen::aarch64
