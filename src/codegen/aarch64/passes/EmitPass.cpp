//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/EmitPass.cpp
// Purpose: Assembly-text emission pass for the AArch64 modular pipeline.
//          Emits rodata globals, then each MIR function via AsmEmitter;
//          appends platform-specific module-level directives at the end.
// Key invariants:
//   - Requires AArch64Module::ti to be non-null.
//   - Requires register allocation to have completed (operates on physical regs).
//   - .subsections_via_symbols emitted on Mach-O for dead-strip support.
//   - .note.GNU-stack emitted on Linux ELF to mark non-executable stack.
// Ownership/Lifetime:
//   - Stateless pass; writes into AArch64Module::assembly string.
// Links: codegen/aarch64/passes/EmitPass.hpp,
//        codegen/aarch64/AsmEmitter.hpp,
//        codegen/aarch64/RodataPool.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/EmitPass.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"

#include <sstream>

namespace viper::codegen::aarch64::passes {

/// @brief Emit AArch64 assembly text for the entire module.
/// @details Pipeline: validates @p module.ti, prints the rodata pool, then
///          asks @ref AsmEmitter to print every MIR function. Finally appends
///          platform-specific module-level directives (`.subsections_via_symbols`
///          on Mach-O, `.note.GNU-stack` on Linux ELF). The accumulated string
///          is stored in @p module.assembly for downstream consumers.
/// @return true on success; on failure records errors via @p diags.
bool EmitPass::run(AArch64Module &module, Diagnostics &diags) {
    if (!module.ti) {
        diags.error("EmitPass: ti must be non-null");
        return false;
    }

    std::ostringstream os;

    // Emit rodata globals (string literals etc.) before function bodies.
    // NOTE: EarlyCSE/GVN now handle ConstStr via ValueKey (line 149 of
    // ValueKey.cpp).  Residual duplicate adrp+add pairs may still appear if
    // SCCP constant-propagates const_str values after CSE has already run.
    // Monitor whether pass ordering changes eliminate the remaining cases.
    module.rodataPool.emit(os);

    AsmEmitter emitter{*module.ti};
    for (const auto &fn : module.mir) {
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
