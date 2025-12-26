//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the memory-to-register (mem2reg) promotion pass. Mem2reg
// transforms stack-allocated local variables into SSA-form temporary values,
// eliminating redundant memory operations and enabling scalar optimizations.
//
// IL code generated from high-level languages often uses alloca to create local
// variables, with loads and stores for all accesses. When these allocations don't
// escape their defining function and have simple access patterns, they can be
// promoted to SSA temporaries. This pass identifies promotable allocas, constructs
// SSA form using block parameters for join points, and eliminates the memory
// operations, dramatically improving code quality.
//
// Key Responsibilities:
// - Identify promotable allocas (primitive types, non-escaping, load/store only)
// - Build SSA form using sealed SSA construction with block parameters
// - Handle control flow merges and loops using block parameter phi nodes
// - Replace load instructions with SSA temporary uses
// - Replace store instructions with SSA temporary definitions
// - Eliminate the alloca instruction after successful promotion
//
// Design Notes:
// The implementation uses the sealed SSA construction algorithm which handles
// loops correctly by placing block parameters at control flow join points. Only
// allocas of primitive types (i1, i16, i32, i64, f64) are promoted. Allocas whose
// addresses escape (passed to calls, stored to memory) cannot be promoted safely
// and are left as memory operations. The pass reports statistics on promoted
// variables and eliminated memory operations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Module.hpp"

namespace viper::passes
{

struct Mem2RegStats
{
    unsigned promotedVars{0};
    unsigned removedLoads{0};
    unsigned removedStores{0};
};

/// \brief Promote simple allocas to SSA form.
/// \param M Module to transform in place.
/// \param stats Optional statistics output.
void mem2reg(il::core::Module &M, Mem2RegStats *stats = nullptr);

} // namespace viper::passes
