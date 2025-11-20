// src/codegen/aarch64/FramePlan.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Describe a minimal frame save/restore plan for AArch64 functions.
//          Used by the emitter to shape prologue/epilogue beyond FP/LR.
// Invariants: Save lists contain only callee-saved registers; emitter assumes
//             16-byte stack alignment and uses paired stores when possible.

#pragma once

#include <vector>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

struct FramePlan
{
    std::vector<PhysReg> saveGPRs; // e.g., X19..X28
    std::vector<PhysReg> saveFPRs; // e.g., V8..V15 (as D regs)
};

} // namespace viper::codegen::aarch64
