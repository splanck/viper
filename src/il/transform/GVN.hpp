//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Global Value Numbering (GVN) + Redundant Load Elimination — function pass
//
// This pass performs dominator-tree based global value numbering to eliminate
// redundant, side-effect-free computations across basic blocks. In addition, it
// performs simple redundant load elimination using BasicAA to disambiguate
// memory and invalidate available loads on intervening stores or calls that may
// clobber memory.
//
// The algorithm traverses the dominator tree in preorder, maintains a table of
// canonical value expressions, and replaces later equivalent expressions with
// the dominating SSA value. For loads, it memoises available (ptr,type) reads
// and reuses a dominating value when no clobber is observed.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

class GVN : public FunctionPass
{
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

/// Register GVN with the pass registry under identifier "gvn".
void registerGVNPass(PassRegistry &registry);

} // namespace il::transform

