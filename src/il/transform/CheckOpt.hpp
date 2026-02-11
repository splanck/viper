//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/CheckOpt.hpp
// Purpose: Declares the CheckOpt function pass -- optimises check opcodes
//          (IdxChk, SDivChk0, UDivChk0, etc.) via dominance-based redundancy
//          elimination and loop-invariant check hoisting to preheaders.
// Key invariants:
//   - Checks are removed only when provably dominated by an identical check.
//   - Hoisting occurs only when operands are loop-invariant and the check
//     would execute on every loop entry.
//   - CFG structure is preserved when only removing instructions.
// Ownership/Lifetime: Stateless FunctionPass; instantiated by the registry.
// Links: il/transform/PassRegistry.hpp, il/analysis/Dominators.hpp,
//        il/transform/analysis/LoopInfo.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Optimize check opcodes via redundancy elimination and loop hoisting.
///
/// This pass identifies check instructions (IdxChk, SDivChk0, UDivChk0, etc.)
/// that are redundant due to dominating equivalent checks, or that can be
/// safely hoisted out of loops when their operands are loop-invariant.
class CheckOpt : public FunctionPass
{
  public:
    /// @brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// @brief Run check optimization over @p function.
    /// @param function Function to optimize.
    /// @param analysis Analysis manager for querying dominators and loop info.
    /// @return PreservedAnalyses indicating which analyses remain valid.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

/// @brief Register the CheckOpt pass with the provided registry.
/// @param registry PassRegistry to register the pass into.
void registerCheckOptPass(PassRegistry &registry);

} // namespace il::transform
