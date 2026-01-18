//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "tools/common/vm_executor.hpp"

#include "bytecode/BytecodeCompiler.hpp"
#include "bytecode/BytecodeVM.hpp"
#include "runtime/rt_args.h"
#include "runtime/rt_string.h"

#include <cstdio>
#include <iostream>

namespace il::tools::common
{

VMExecutorResult executeBytecodeVM(const il::core::Module &module,
                                   const VMExecutorConfig &config)
{
    VMExecutorResult result;

    // Compile IL to bytecode
    viper::bytecode::BytecodeCompiler bcCompiler;
    viper::bytecode::BytecodeModule bcModule = bcCompiler.compile(module);

    // Set up program arguments for the runtime
    if (!config.programArgs.empty())
    {
        rt_args_clear();
        for (const auto &s : config.programArgs)
        {
            rt_string tmp = rt_string_from_bytes(s.data(), s.size());
            rt_args_push(tmp);
            rt_string_unref(tmp);
        }
    }

    // Configure and run the VM
    viper::bytecode::BytecodeVM bcVm;
    bcVm.setThreadedDispatch(true);
    bcVm.setRuntimeBridgeEnabled(true);
    bcVm.load(&bcModule);

    viper::bytecode::BCSlot bcResult = bcVm.exec("main", {});

    // Handle results
    if (bcVm.state() == viper::bytecode::VMState::Trapped)
    {
        result.trapped = true;
        result.trapMessage = bcVm.trapMessage();
        result.exitCode = 1;

        if (config.outputTrapMessage)
        {
            std::cerr << result.trapMessage << "\n";
        }
    }
    else
    {
        result.exitCode = static_cast<int>(bcResult.i64);
    }

    if (config.flushStdout)
    {
        std::fflush(stdout);
    }

    return result;
}

} // namespace il::tools::common
