//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/LICM.hpp
// Purpose: Loop-Invariant Code Motion -- function pass that hoists
//          loop-invariant, side-effect-free instructions to loop preheaders,
//          reducing redundant computation per iteration. Loads are hoisted
//          only when the loop contains no aliasing memory writes.
// Key invariants:
//   - Only hoists instructions that are pure, non-trapping, and whose operands
//     are defined outside the loop or are themselves loop-invariant.
//   - Assumes LoopSimplify has provided dedicated preheader/latch blocks.
// Ownership/Lifetime: Stateless FunctionPass; instantiated by the registry.
// Links: il/transform/PassRegistry.hpp, il/transform/analysis/LoopInfo.hpp,
//        il/analysis/BasicAA.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Perform loop-invariant code motion for trivially safe instructions.
/// @details Hoists instructions whose operands are loop-invariant, whose opcode
///          is side-effect free and non-trapping, and (for loads) only when the
///          loop contains no memory writes (based on BasicAA/modref metadata).
///          Assumes LoopSimplify has provided a dedicated preheader/latch.
class LICM : public FunctionPass
{
  public:
    /// @brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// @brief Run loop-invariant code motion over @p function.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

/// @brief Register the LICM pass with the provided registry.
void registerLICMPass(PassRegistry &registry);

} // namespace il::transform
