// src/codegen/aarch64/LowerILToMIR.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Minimal IL→MIR lowering adapter for AArch64 (Phase A).
//          Covers the subset of patterns used by the arm64 CLI smoke tests:
//          - ret const i64
//          - ret %paramN (N in [0,7])
//          - rr ops on entry params feeding ret: add/sub/mul/and/or/xor
//          - ri ops on entry params feeding ret: add/sub
//          - shift-by-immediate on entry params: shl/lshr/ashr
//          - compares feeding ret: icmp/scmp/ucmp (rr and param-vs-imm)
// Invariants: Generates MIR that expects AsmEmitter to handle header/prologue/
//             epilogue; all values are in physical regs per ABI conventions.

#pragma once

#include <ostream>

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "il/core/Function.hpp"

namespace viper::codegen::aarch64
{

class LowerILToMIR
{
  public:
    explicit LowerILToMIR(const TargetInfo &ti) noexcept : ti_(&ti) {}

    MFunction lowerFunction(const il::core::Function &fn) const;

  private:
    const TargetInfo *ti_{};
};

} // namespace viper::codegen::aarch64
