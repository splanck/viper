// File: src/il/transform/PipelineExecutor.hpp
// Purpose: Declare a helper that executes registered pass pipelines on IL modules.
// Key invariants: Pass ordering matches the provided pipeline sequence.
// Ownership/Lifetime: Executor borrows registries owned by the pass manager and never outlives
// them. Links: docs/codemap.md
#pragma once

#include "il/core/fwd.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/PassRegistry.hpp"

#include <string>
#include <vector>

namespace il::transform
{

class PipelineExecutor
{
  public:
    PipelineExecutor(const PassRegistry &registry,
                     const AnalysisRegistry &analysisRegistry,
                     bool verifyBetweenPasses);

    void run(core::Module &module, const std::vector<std::string> &pipeline) const;

  private:
    const PassRegistry &registry_;
    const AnalysisRegistry &analysisRegistry_;
    bool verifyBetweenPasses_;
};

} // namespace il::transform
