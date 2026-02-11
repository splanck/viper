//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/LoopSimplify.hpp
// Purpose: Loop Simplification canonicalisation pass -- ensures each natural
//          loop has a unique preheader, a dedicated latch, and dedicated exit
//          blocks. Purely structural; does not change program semantics.
// Key invariants:
//   - SSA form is maintained via proper block parameter threading.
//   - Only modifies loops that violate canonical form.
// Ownership/Lifetime: Stateless FunctionPass; instantiated by the registry.
// Links: il/transform/PassRegistry.hpp, il/transform/analysis/LoopInfo.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Loop canonicalisation pass that ensures well-structured loop form.
/// @details Transforms each natural loop to have a unique preheader block,
///          a single dedicated latch block, and dedicated exit blocks. This
///          canonical form is required by downstream loop optimisation passes
///          like IndVarSimplify and loop-invariant code motion.
class LoopSimplify : public FunctionPass
{
  public:
    /// \brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// \brief Run the loop simplifier over @p function using @p analysis for queries.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

} // namespace il::transform
