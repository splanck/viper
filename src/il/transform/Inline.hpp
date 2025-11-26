//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// A simple direct-call function inliner. This "teaching inliner" only inlines
// small, non-recursive, direct-call functions with a single basic block and a
// conventional `ret` terminator. It avoids externs, indirect calls, and large
// callees. Heuristics: callee instruction count <= threshold (default 32) and
// optionally low call-site count from a tiny call graph helper.
//
// Inlining is performed at the IL level by cloning the callee's instructions
// into the caller at the call site, remapping callee parameters to call
// arguments and assigning fresh SSA temporaries for callee results.
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

    void setInstructionThreshold(unsigned n) { instrThreshold_ = n; }

  private:
    unsigned instrThreshold_ = 32; // default heuristic
};

void registerInlinePass(PassRegistry &registry);

} // namespace il::transform

