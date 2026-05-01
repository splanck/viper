//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/passes/PreRegAllocOptPass.hpp
// Purpose: Pass-manager wrapper for AArch64 pre-register-allocation MIR cleanup.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

namespace viper::codegen::aarch64::passes {

class PreRegAllocOptPass final : public Pass {
  public:
    bool run(AArch64Module &module, Diagnostics &diags) override;
};

} // namespace viper::codegen::aarch64::passes
