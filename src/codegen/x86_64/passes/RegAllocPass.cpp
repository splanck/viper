//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/RegAllocPass.cpp
// Purpose: Implement the register allocation stage for the x86-64 pipeline.
// Key invariants: Register allocation is only considered complete when legalisation has run
//                 and the module carries legalized machine IR.
// Ownership/Lifetime: Stateless transformation mutating Module MIR in place.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Register allocation pass for the x86-64 code generator.
/// @details Validates pipeline ordering, runs allocation/frame lowering on the
///          legalized machine IR, and records diagnostics on failure.

#include "codegen/x86_64/passes/RegAllocPass.hpp"

#include <string>

namespace viper::codegen::x64::passes {

/// @brief Run register allocation on legalized MIR.
/// @details The pass checks the @p module bookkeeping flags and concrete MIR
///          state before invoking the backend allocation helpers. On success it
///          flips the `registersAllocated` flag, allowing subsequent passes to
///          emit assembly or binary code without rerunning hidden lowering work.
/// @param module Backend pipeline state to update.
/// @param diags  Diagnostics sink for reporting ordering mistakes.
/// @return @c true when register allocation can be considered complete.
bool RegAllocPass::run(Module &module, Diagnostics &diags) {
    if (!module.legalised) {
        diags.error("regalloc: legalisation must run before register allocation");
        return false;
    }

    if (module.target == nullptr) {
        diags.error("regalloc: target selection is missing");
        return false;
    }
    if (module.mir.size() != module.frames.size()) {
        diags.error("regalloc: MIR/frame state is inconsistent");
        return false;
    }

    std::string errors;
    if (!allocateModuleMIR(module.mir, module.frames, *module.target, module.options, errors)) {
        if (errors.empty())
            errors = "regalloc: register allocation failed";
        diags.error(errors);
        return false;
    }

    module.registersAllocated = true;
    return true;
}

} // namespace viper::codegen::x64::passes
