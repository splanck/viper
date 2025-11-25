//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FramePlan.hpp
// Purpose: Describe a minimal frame save/restore plan for AArch64 functions.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include "codegen/aarch64/TargetAArch64.hpp"

namespace viper::codegen::aarch64
{

struct FramePlan
{
    std::vector<PhysReg> saveGPRs; // e.g., X19..X28
    std::vector<PhysReg> saveFPRs; // e.g., V8..V15 (as D regs)
    int localFrameSize{0};         // bytes for stack-allocated locals (aligned to 16)
};

} // namespace viper::codegen::aarch64
