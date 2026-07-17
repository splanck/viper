//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/RuntimeFastPathOpt.hpp
// Purpose: Select specialized runtime helpers after simple provenance proofs.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform {

class RuntimeFastPathOpt : public FunctionPass {
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

void registerRuntimeFastPathOptPass(PassRegistry &registry);

} // namespace il::transform

