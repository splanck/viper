//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/RegAllocPass.hpp
// Purpose: Declare the register allocation stage placeholder for the x86-64 pipeline.
// Key invariants:
//   - Register allocation requires legalisation to have succeeded first.
// Ownership/Lifetime:
//   - Stateless pass; marks Module::registersAllocated on success.
// Links: codegen/x86_64/passes/RegAllocPass.cpp,
//        codegen/x86_64/passes/PassManager.hpp,
//        codegen/x86_64/RegAllocLinear.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace zanna::codegen::x64::passes {

/// \brief Placeholder pass used to gate later emission on prior legalisation.
class RegAllocPass final : public Pass {
  public:
    /// @brief Run the register allocation pass: assign physical registers to virtual registers.
    /// @param module The codegen module containing MIR functions with virtual registers.
    /// @param diags Diagnostic sink for reporting allocation errors (e.g., spill failures).
    /// @return True if register allocation succeeded, false on error.
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace zanna::codegen::x64::passes
