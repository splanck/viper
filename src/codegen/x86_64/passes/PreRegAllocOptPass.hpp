//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/PreRegAllocOptPass.hpp
// Purpose: Pass-manager wrapper for x86-64 pre-register-allocation MIR cleanup.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

namespace viper::codegen::x64::passes {

class PreRegAllocOptPass final : public Pass {
  public:
    bool run(Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::x64::passes
