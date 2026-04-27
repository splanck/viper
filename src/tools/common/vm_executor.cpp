//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "tools/common/vm_executor.hpp"

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "runtime/core/rt_args.h"
#include "runtime/core/rt_string.h"
#include "support/diag_expected.hpp"

#include <cstdio>
#include <iostream>

namespace il::tools::common {

VMExecutorResult executeBytecodeVM(const il::core::Module &module, const VMExecutorConfig &config) {
    VMExecutorResult result;

    // Compile IL to bytecode
    viper::bytecode::BytecodeCompiler bcCompiler;
    auto compiled = bcCompiler.compileChecked(module, config.sourceManager);
    if (!compiled) {
        result.compileFailed = true;
        result.trapped = false;
        result.exitCode = 1;
        result.trapMessage = compiled.error().message;
        if (config.outputTrapMessage) {
            il::support::printDiag(compiled.error(), std::cerr, config.sourceManager);
        }
        if (config.flushStdout) {
            std::fflush(stdout);
        }
        return result;
    }
    viper::bytecode::BytecodeModule bcModule = std::move(compiled.value());

    // Set up program arguments for the runtime. An empty forwarded argument
    // list is meaningful: it must suppress the native host argv fallback.
    rt_args_clear();
    for (const auto &s : config.programArgs) {
        rt_string tmp = rt_string_from_bytes(s.data(), s.size());
        rt_args_push(tmp);
        rt_string_unref(tmp);
    }

    // Configure and run the VM
    viper::bytecode::BytecodeVM bcVm;
    bcVm.setThreadedDispatch(true);
    bcVm.setRuntimeBridgeEnabled(true);
    bcVm.load(&bcModule);

    viper::bytecode::BCSlot bcResult = bcVm.exec("main", {});

    // Handle results
    if (bcVm.state() == viper::bytecode::VMState::Trapped) {
        result.trapped = true;
        result.trapMessage = bcVm.trapMessage();
        result.exitCode = 1;

        if (config.outputTrapMessage) {
            std::cerr << result.trapMessage << "\n";
        }
    } else {
        result.exitCode = 0;
    }

    if (config.flushStdout) {
        std::fflush(stdout);
    }

    return result;
}

} // namespace il::tools::common
