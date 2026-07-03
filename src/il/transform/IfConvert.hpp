//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/IfConvert.hpp
// Purpose: Convert small branch diamonds and triangles into `select`
//          instructions so backends can emit conditional moves instead of
//          branches.
// Key invariants:
//   - Only side-effect-free, non-trapping arm instructions are speculated.
//   - Arm blocks must be single-predecessor and joined through block params.
// Ownership/Lifetime:
//   - Stateless function pass owned by the pass registry.
// Links: docs/adr/0063-il-select-and-if-conversion.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform {

/// @brief Function pass folding branch diamonds/triangles into `select`.
/// @invariant Speculated instructions are pure and non-trapping.
/// @ownership Owned by the pass registry factory; no per-run state.
class IfConvert : public FunctionPass {
  public:
    std::string_view id() const override;
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

void registerIfConvertPass(PassRegistry &registry);

} // namespace il::transform
