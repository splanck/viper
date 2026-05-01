//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/passes/PreRegAllocOptPass.cpp
// Purpose: Run AArch64 pre-register-allocation MIR cleanup in the modular pipeline.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/PreRegAllocOptPass.hpp"

#include "codegen/aarch64/PreRegAllocOpt.hpp"

namespace viper::codegen::aarch64::passes {

bool PreRegAllocOptPass::run(AArch64Module &module, Diagnostics &diags) {
    if (!module.ti) {
        diags.error("PreRegAllocOptPass: ti must be non-null");
        return false;
    }
    for (auto &fn : module.mir)
        (void)runPreRegAllocOpt(fn);
    return true;
}

} // namespace viper::codegen::aarch64::passes
