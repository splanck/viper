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

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/Peephole.hpp"

#include <cstdlib>
#include <string>

namespace viper::codegen::x64::passes {
namespace {

[[nodiscard]] bool codegenStatsEnabled() noexcept {
    if (const char *value = std::getenv("VIPER_CODEGEN_STATS"))
        return value[0] != '\0' && value[0] != '0';
    return false;
}

} // namespace

bool PeepholePass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("peephole: register allocation must run before backend optimization");
        return false;
    }

    if (module.options.optimizeLevel < 1)
        return true;

    if (module.target == nullptr)
        module.target = &selectTarget(module.options.targetABI);

    std::size_t total = 0;
    for (auto &fn : module.mir) {
        total += runPeepholes(fn, *module.target);
    }

    if (codegenStatsEnabled())
        diags.warning("x86-64 peephole: " + std::to_string(total) + " transformations");

    return true;
}

} // namespace viper::codegen::x64::passes
