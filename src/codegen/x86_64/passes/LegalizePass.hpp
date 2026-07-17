//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/LegalizePass.hpp
// Purpose: Declare the legalisation pass for the x86-64 codegen pipeline.
// Key invariants:
//   - Legalisation requires lowering to have populated the adapter IL module.
// Ownership/Lifetime:
//   - Stateless pass; toggles flags on the shared Module instance.
// Links: codegen/x86_64/passes/LegalizePass.cpp,
//        codegen/x86_64/passes/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace zanna::codegen::x64::passes {

/// \brief Placeholder legalisation pass that validates lowering prerequisites.
class LegalizePass final : public Pass {
  public:
    /// @brief Run the legalization pass: validate and normalize MIR for x86-64 constraints.
    /// @param module The codegen module containing MIR functions to legalize.
    /// @param diags Diagnostic sink for reporting legalization errors.
    /// @return True if legalization succeeded, false on error.
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace zanna::codegen::x64::passes
