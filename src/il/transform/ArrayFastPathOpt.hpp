//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/ArrayFastPathOpt.hpp
// Purpose: Select unchecked numeric array runtime helpers after proven bounds checks.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform {

class ArrayFastPathOpt : public FunctionPass {
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

void registerArrayFastPathOptPass(PassRegistry &registry);

} // namespace il::transform

