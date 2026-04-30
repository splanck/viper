//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/SchedulerPass.cpp
// Purpose: Implement the x86-64 post-RA scheduling pass.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/SchedulerPass.hpp"

#include "codegen/x86_64/Scheduler.hpp"

namespace viper::codegen::x64::passes {

bool SchedulerPass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("scheduler: register allocation must run before scheduling");
        return false;
    }

    if (module.options.optimizeLevel < 1)
        return true;

    (void)scheduleModule(module.mir);
    return true;
}

} // namespace viper::codegen::x64::passes

