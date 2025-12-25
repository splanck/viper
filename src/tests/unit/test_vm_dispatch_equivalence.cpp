//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_dispatch_equivalence.cpp
// Purpose: Verify all dispatch strategies produce identical results.
// Key invariants: FnTable, Switch, and Threaded strategies must produce the
//                 same observable behavior (return value).
// Ownership/Lifetime: Builds ephemeral modules and executes with each strategy.
// Links: docs/vm.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include <cstdlib>
#ifdef _WIN32
#include "tests/common/PosixCompat.h"
#endif
#include <cassert>
#include <cstdlib>

using namespace il::core;

namespace
{

/// @brief Build a simple arithmetic module that returns 42.
/// @details Computes (10 * 4) + 2 = 42.
Module buildSimpleModule()
{
    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    // %0 = add 10, 0
    Instr i0;
    i0.result = 0;
    i0.op = Opcode::Add;
    i0.type = Type(Type::Kind::I64);
    i0.operands = {Value::constInt(10), Value::constInt(0)};
    entry.instructions.push_back(i0);

    // %1 = mul %0, 4
    Instr i1;
    i1.result = 1;
    i1.op = Opcode::Mul;
    i1.type = Type(Type::Kind::I64);
    i1.operands = {Value::temp(0), Value::constInt(4)};
    entry.instructions.push_back(i1);

    // %2 = add %1, 2
    Instr i2;
    i2.result = 2;
    i2.op = Opcode::Add;
    i2.type = Type(Type::Kind::I64);
    i2.operands = {Value::temp(1), Value::constInt(2)};
    entry.instructions.push_back(i2);

    // ret %2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(2)};
    entry.instructions.push_back(ret);
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(3);
    m.functions.push_back(std::move(fn));

    return m;
}

/// @brief Run a module with a specific dispatch strategy.
/// @param m Module to execute.
/// @param strategy Strategy name: "table", "switch", or "threaded".
/// @return The result from VM::run().
int64_t runWithStrategy(const Module &m, const char *strategy)
{
    // Set environment variable to select dispatch strategy
    setenv("VIPER_DISPATCH", strategy, 1);
    il::vm::VM vm(m);
    return vm.run();
}

} // namespace

int main()
{
    // Build a simple test program: (10 * 4) + 2 = 42
    Module m = buildSimpleModule();

    // Run with each dispatch strategy
    int64_t resultTable = runWithStrategy(m, "table");
    int64_t resultSwitch = runWithStrategy(m, "switch");
    int64_t resultThreaded = runWithStrategy(m, "threaded");

    // Verify all strategies produce the same result (42)
    assert(resultTable == 42 && "FnTable strategy: expected 42");
    assert(resultSwitch == 42 && "Switch strategy: expected 42");
    assert(resultThreaded == 42 && "Threaded strategy: expected 42");

    // Verify all strategies agree
    assert(resultTable == resultSwitch && "FnTable and Switch must produce same result");
    assert(resultSwitch == resultThreaded && "Switch and Threaded must produce same result");

    return 0;
}
