//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/OwnershipOpt.hpp
// Purpose: Remove provably redundant runtime retain/release traffic.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform {

class OwnershipOpt : public FunctionPass {
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

void registerOwnershipOptPass(PassRegistry &registry);

} // namespace il::transform

