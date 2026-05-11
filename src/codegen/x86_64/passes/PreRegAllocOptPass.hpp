//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/PreRegAllocOptPass.hpp
// Purpose: Pass-manager wrapper for x86-64 pre-register-allocation MIR cleanup.
// Key invariants:
//   - Runs before register allocation; operates on virtual-register MIR.
// Ownership/Lifetime:
//   - Stateless pass; mutates Module::mir in place.
// Links: codegen/x86_64/passes/PreRegAllocOptPass.cpp,
//        codegen/x86_64/passes/PassManager.hpp,
//        codegen/x86_64/PreRegAllocOpt.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes {

/// @brief Pre-register-allocation MIR optimization pass for the x86-64 pipeline.
class PreRegAllocOptPass final : public Pass {
  public:
    /// @brief Run the cleanup pass over the legalised MIR.
    /// @param module Pipeline state whose @c mir vector is mutated in place.
    /// @param diags Diagnostic sink for reporting failures.
    /// @return True on success.
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes
