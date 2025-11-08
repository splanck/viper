// File: src/il/transform/LICM.hpp
// Purpose: Declare the loop-invariant code motion pass for IL functions.
// Key invariants: Hoists only trivially safe, loop-invariant instructions into preheaders.
// Ownership/Lifetime: Pass operates in place on caller-owned functions via the pass manager.
// Links: docs/codemap.md
#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

/// @brief Perform loop-invariant code motion for trivially safe instructions.
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
