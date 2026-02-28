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
    // TODO(codegen): At O2, the IL optimizer may break const_str → print_str
    // value chains, causing duplicate adrp+add pairs for the same rodata label.
    // This is an IL-level issue (SCCP/CSE treats const_str outputs as independent
    // values); fix requires teaching the optimizer about const_str semantics.
    module.rodataPool.emit(os);

    AsmEmitter emitter{*module.ti};
    for (const auto &fn : module.mir)
    {
        emitter.emitFunction(os, fn);
        os << "\n";
    }

    // Emit platform-specific directives at end of module.
    // .subsections_via_symbols enables function-level dead stripping on macOS
    // and prevents the linker from setting MH_ALLOW_STACK_EXECUTION.
    // .note.GNU-stack marks the stack as non-executable on Linux ELF.
    if (module.ti->isLinux())
        os << ".section .note.GNU-stack,\"\",@progbits\n";
    else if (!module.ti->isWindows())
        os << ".subsections_via_symbols\n";

    module.assembly = os.str();
    return true;
}

} // namespace viper::codegen::aarch64::passes
