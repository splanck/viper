//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/LegalizePass.cpp
// Purpose: Implement the MIR legalisation stage in the x86-64 codegen pipeline.
// Key invariants: Legalisation is only considered successful when lowering has produced an
//                 adapter module and all functions lower cleanly to machine IR.
// Ownership/Lifetime: Stateless pass mutating Module state in place.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief MIR legalisation pass for the x86-64 backend pipeline.
/// @details Lowers the adapter IL into machine IR, captures the shared
///          read-only literal pool, and records one frame summary per
///          function. Failures are surfaced through the shared diagnostics
///          sink so later passes never have to infer partial state.

#include "codegen/x86_64/passes/LegalizePass.hpp"

#include <string>

namespace viper::codegen::x64::passes {

/// @brief Lower the adapter module to legalized MIR.
/// @details Emits a descriptive diagnostic when lowering has not populated the
///          module's adapter artefact or when MIR legalisation fails. On
///          success, populates @c mir, @c frames, and @c roData before setting
///          @c legalised so later passes can rely on concrete machine state.
/// @param module Backend pipeline state being mutated.
/// @param diags  Diagnostics sink used to report ordering problems.
/// @return @c true when legalisation conditions are satisfied.
bool LegalizePass::run(Module &module, Diagnostics &diags) {
    if (!module.lowered) {
        diags.error("legalize: lowering has not produced an adapter module");
        return false;
    }

    if (module.target == nullptr)
        module.target = &selectTarget(module.options.targetABI);

    std::string errors;
    if (!legalizeModuleToMIR(*module.lowered,
                             *module.target,
                             module.options,
                             module.roData,
                             module.mir,
                             module.frames,
                             errors)) {
        if (errors.empty())
            errors = "legalize: MIR legalisation failed";
        diags.error(errors);
        return false;
    }

    module.legalised = true;
    module.registersAllocated = false;
    module.codegenResult.reset();
    module.binaryText.reset();
    module.binaryRodata.reset();
    module.binaryTextSections.clear();
    module.debugLineData.clear();
    return true;
}

} // namespace viper::codegen::x64::passes
