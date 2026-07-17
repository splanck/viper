//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/PeepholePass.hpp
// Purpose: Declare the explicit post-RA peephole pass for the x86-64 pipeline.
// Key invariants:
//   - Runs after register allocation; operates on physical-register MIR.
// Ownership/Lifetime:
//   - Stateless pass; mutates Module::mir in place.
// Links: codegen/x86_64/passes/PeepholePass.cpp,
//        codegen/x86_64/passes/PassManager.hpp,
//        codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace zanna::codegen::x64::passes {

/// @brief Post-RA peephole optimization pass for the x86-64 codegen pipeline.
class PeepholePass final : public Pass {
  public:
    /// @brief Run peephole rewrites over physical-register MIR.
    /// @param module Pipeline state whose @c mir vector is mutated in place.
    /// @param diags Diagnostic sink for ordering failures.
    /// @return True on success.
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace zanna::codegen::x64::passes
