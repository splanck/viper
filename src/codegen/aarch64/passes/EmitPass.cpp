//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/EmitPass.cpp
// Purpose: Assembly emission pass for the AArch64 modular pipeline.
//
// Emits Darwin-compatible AArch64 assembly:
//   1. .text section header
//   2. Rodata pool globals (string literals)
//   3. Each MIR function emitted via AsmEmitter::emitFunction
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/EmitPass.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"

#include <sstream>

namespace viper::codegen::aarch64::passes
{

bool EmitPass::run(AArch64Module &module, Diagnostics &diags)
{
    if (!module.ti)
    {
        diags.error("EmitPass: ti must be non-null");
        return false;
    }

    std::ostringstream os;

    // Emit rodata globals (string literals etc.) before function bodies.
    module.rodataPool.emit(os);

    AsmEmitter emitter{*module.ti};
    for (const auto &fn : module.mir)
    {
        emitter.emitFunction(os, fn);
        os << "\n";
    }

    module.assembly = os.str();
    return true;
}

} // namespace viper::codegen::aarch64::passes
