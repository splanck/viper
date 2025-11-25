//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/LegalizePass.hpp
// Purpose: Declare the legalisation pass for the x86-64 codegen pipeline.
// Key invariants: Legalisation requires lowering to have populated the adapter IL module.
// Ownership/Lifetime: Stateless pass toggling flags on the shared Module instance.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes
{

/// \brief Placeholder legalisation pass that validates lowering prerequisites.
class LegalizePass final : public Pass
{
  public:
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes
