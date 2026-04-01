//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/PeepholePass.cpp
// Purpose: Implement the explicit post-RA peephole pass for the x86-64 pipeline.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/PeepholePass.hpp"

#include <string>

namespace viper::codegen::x64::passes {

bool PeepholePass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("peephole: register allocation must run before backend optimization");
        return false;
    }

    std::string errors;
    if (!optimizeModuleMIR(module.mir, module.options, errors)) {
        if (errors.empty())
            errors = "peephole: backend optimization failed";
        diags.error(errors);
        return false;
    }

    return true;
}

} // namespace viper::codegen::x64::passes
