//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_executor_compile_diagnostics.cpp
// Purpose: Ensure VM execution reports bytecode compile failures before running.
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"
#include "tools/common/vm_executor.hpp"

#include <utility>

namespace {

il::core::Module buildMalformedBytecodeModule() {
    using namespace il::core;

    Module module;

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr gaddr;
    gaddr.result = 0;
    gaddr.op = Opcode::GAddr;
    gaddr.type = Type(Type::Kind::Ptr);
    gaddr.operands.push_back(Value::global("data"));
    gaddr.loc = {3, 2, 7};
    entry.instructions.push_back(std::move(gaddr));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {3, 3, 1};
    entry.instructions.push_back(ret);
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));
    return module;
}

TEST(VMExecutorDiagnostics, BytecodeCompileFailureDoesNotRunVm) {
    il::tools::common::VMExecutorConfig config;
    config.outputTrapMessage = false;

    auto result = il::tools::common::executeBytecodeVM(buildMalformedBytecodeModule(), config);

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_TRUE(result.compileFailed);
    EXPECT_FALSE(result.trapped);
    EXPECT_CONTAINS(result.trapMessage, "bytecode compile failed in @main");
    EXPECT_CONTAINS(result.trapMessage, "unknown global @data");
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
