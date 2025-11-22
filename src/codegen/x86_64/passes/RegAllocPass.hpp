//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/RegAllocPass.hpp
// Purpose: Declare the register allocation stage placeholder for the x86-64 pipeline. 
// Key invariants: Register allocation requires legalisation to have succeeded first.
// Ownership/Lifetime: Stateless pass that marks progress through the pipeline.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes
{

/// \brief Placeholder pass used to gate later emission on prior legalisation.
class RegAllocPass final : public Pass
{
  public:
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes
