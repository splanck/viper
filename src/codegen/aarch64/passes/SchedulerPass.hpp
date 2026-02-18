//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/SchedulerPass.hpp
// Purpose: Declare the post-RA instruction scheduling pass for AArch64.
// Key invariants: Runs after RegAllocPass (physical registers must be assigned).
//                 Reorders instructions within each basic block to reduce
//                 pipeline stalls by increasing the distance between a producer
//                 instruction and the consumer that depends on its result.
//                 Terminators are always kept at the end of the block.
//                 Does not add, remove, or modify instructions — only reorders.
// Ownership/Lifetime: Stateless pass; mutates AArch64Module::mir in place.
// Links: docs/codemap.md, src/codegen/aarch64/Scheduler.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes
{

/// @brief Post-RA instruction scheduler using list scheduling with AArch64 latencies.
///
/// For each basic block, constructs a data-dependency DAG from the
/// post-allocation physical-register operands and applies a list-scheduling
/// algorithm that prioritises instructions on the critical path.  The schedule
/// reduces load-use stalls (ldr latency ~4 cycles on Apple Silicon) by moving
/// independent instructions between a load and its first use.
class SchedulerPass final : public Pass
{
  public:
    /// @brief Apply post-RA scheduling to every basic block in every function.
    /// @param module Module state; mir must have physical registers assigned.
    /// @param diags  Diagnostic sink (scheduling is non-failing; always true).
    /// @return Always true; scheduling failures fall back to the original order.
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
