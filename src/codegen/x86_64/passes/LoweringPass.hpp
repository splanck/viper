//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/LoweringPass.hpp
// Purpose: Declare the IL-to-adapter lowering pass for the x86-64 codegen pipeline.
// Key invariants: Lowering preserves SSA identifiers and records value kinds for temporaries.
// Ownership/Lifetime: Pass is stateless and mutates the provided Module in place.
// Links: docs/codemap.md, src/codegen/x86_64/LowerILToMIR.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes
{

/// \brief Lower il::core modules into the backend adapter IL form.
class LoweringPass final : public Pass
{
  public:
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes
