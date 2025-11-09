// File: src/il/transform/LoopSimplify.hpp
// Purpose: Declare a conservative loop canonicalisation pass for IL functions.
// Key invariants: Pass only performs structural changes that preserve semantics and SSA form.
// Ownership/Lifetime: Operates in place on caller-owned functions through the pass manager.
// Links: docs/codemap.md
#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

class LoopSimplify : public FunctionPass
{
  public:
    /// \brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// \brief Run the loop simplifier over @p function using @p analysis for queries.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

} // namespace il::transform
