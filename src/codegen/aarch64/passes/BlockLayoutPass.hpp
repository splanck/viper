//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/BlockLayoutPass.hpp
// Purpose: Declare the block layout pass for the AArch64 codegen pipeline.
// Key invariants: Must run after RegAllocPass and before PeepholePass.
//                 Only reorders MBasicBlock entries in MFunction::blocks;
//                 never adds, removes, or modifies instructions.
//                 Entry block (index 0) always remains first.
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::mir in place.
// Links: docs/devdocs/codegen/aarch64.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes
{

/// @brief Reorder MIR basic blocks using a greedy trace algorithm.
///
/// Starting from the entry block, the pass repeatedly places the target of
/// each unconditional branch (Br) as the immediately following block.  After
/// reordering, PeepholePass can eliminate the resulting fall-through branches.
///
/// The pass is a pure reorder: block names and branch targets are stable.
class BlockLayoutPass final : public Pass
{
  public:
    /// @brief Reorder blocks in all MIR functions to improve fall-through layout.
    /// @param module Module state; mir must have physical registers assigned.
    /// @param diags  Diagnostic sink (layout is non-failing; always returns true).
    /// @return Always true.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
