//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/transform/Devirtualize.hpp
// Purpose: Convert statically-known indirect calls to direct calls.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform {

class Devirtualize : public FunctionPass {
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

void registerDevirtualizePass(PassRegistry &registry);

} // namespace il::transform

