//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/RegAllocPass.cpp
// Purpose: Register allocation pass for the AArch64 modular pipeline.
//
// Runs the linear-scan register allocator on every MIR function produced by
// LoweringPass.  After this pass, all virtual registers are replaced with
// physical AArch64 registers and spill/reload code has been inserted.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/RegAllocPass.hpp"

#include "codegen/aarch64/RegAllocLinear.hpp"

namespace viper::codegen::aarch64::passes
{

bool RegAllocPass::run(AArch64Module &module, Diagnostics &diags)
{
    if (!module.ti)
    {
        diags.error("RegAllocPass: ti must be non-null");
        return false;
    }

    for (auto &fn : module.mir)
    {
        [[maybe_unused]] auto result = allocate(fn, *module.ti);
    }

    return true;
}

} // namespace viper::codegen::aarch64::passes
