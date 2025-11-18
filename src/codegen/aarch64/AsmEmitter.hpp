// src/codegen/aarch64/AsmEmitter.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Minimal AArch64 assembly emitter for early backend scaffolding.
//          Provides helpers to emit a simple function prologue/epilogue and
//          a subset of integer instructions (ADD, MOV, RET) used by smoke tests.
// Invariants: Stateless aside from formatting options; uses TargetAArch64 for
//             register naming and ABI-aligned prologue/epilogue shapes.

#pragma once

#include <ostream>
#include <string>

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

    // Integer ops (64-bit): mov dst, src; add dst, lhs, rhs; ret
    void emitMovRR(std::ostream &os, PhysReg dst, PhysReg src) const;
    // mov dst, #imm (immediate move)
    void emitMovRI(std::ostream &os, PhysReg dst, long long imm) const;
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
    void emitRet(std::ostream &os) const;

  private:
    const TargetInfo *target_{nullptr};
    [[nodiscard]] static const char *rn(PhysReg r) noexcept { return regName(r); }
};

} // namespace viper::codegen::aarch64
