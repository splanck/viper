//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// A lightweight direct-call inliner with a documented, conservative cost model.
// The pass targets tiny callees (instruction budget + block budget), avoids
// recursion, and skips exception-handling sensitive callees. Supported control
// flow: direct calls without EH, `br`/`cbr`/`switch`/`ret`, and block params.
// Heuristics:
// - Instruction budget: <= instrThreshold_ (default 32)
// - Block budget: <= blockBudget_ (default 4)
// - Call frequency: <= 4 direct call sites
// - Inline depth: stops at maxInlineDepth_ (default 2)
//
// Inlining clones the callee CFG into the caller, remaps callee params to call
// operands (including block parameters), rewires returns to a continuation
// block, and assigns fresh SSA temporaries for all cloned results.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

class Inliner : public ModulePass
{
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) override;

    void setInstructionThreshold(unsigned n)
    {
        instrThreshold_ = n;
    }

  private:
    unsigned instrThreshold_ = 32; // default heuristic
    unsigned blockBudget_ = 4;
    unsigned maxInlineDepth_ = 2;
};

void registerInlinePass(PassRegistry &registry);

} // namespace il::transform
