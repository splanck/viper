//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/InlineLiteralCacheTests.cpp
// Purpose: Ensure inline ConstStr operands reuse cached runtime handles for embedded NULs and ASCII
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "vm/VM.hpp"

#include "../unit/VMTestHook.hpp"

#include "viper/runtime/rt.h"

#include <cassert>
#include <cstring>
#include <string>

using namespace il::core;

namespace
{
Module buildLoopModule(const std::string &literal, int64_t iterations)
{
    Module module;

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);
    fn.valueNames.resize(5);

    BasicBlock entry;
    entry.label = "entry";
    Instr branchToLoop;
    branchToLoop.op = Opcode::Br;
    branchToLoop.type = Type(Type::Kind::Void);
    branchToLoop.labels.push_back("loop");
    branchToLoop.brArgs.push_back({Value::constInt(0)});
    entry.instructions.push_back(branchToLoop);
    entry.terminated = true;

    BasicBlock loop;
    loop.label = "loop";
    loop.params.push_back(Param{"i", Type(Type::Kind::I64), 0});

    Instr makeStr;
    makeStr.result = 2;
    makeStr.op = Opcode::ConstStr;
    makeStr.type = Type(Type::Kind::Str);
    makeStr.operands.push_back(Value::constStr(literal));
    loop.instructions.push_back(makeStr);

    Instr next;
    next.result = 3;
    next.op = Opcode::IAddOvf;
    next.type = Type(Type::Kind::I64);
    next.operands.push_back(Value::temp(0));
    next.operands.push_back(Value::constInt(1));
    loop.instructions.push_back(next);

    Instr done;
    done.result = 4;
    done.op = Opcode::ICmpEq;
    done.type = Type(Type::Kind::I1);
    done.operands.push_back(Value::temp(3));
    done.operands.push_back(Value::constInt(iterations));
    loop.instructions.push_back(done);

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::temp(4));
    cbr.labels.push_back("exit");
    cbr.labels.push_back("loop");
    cbr.brArgs.push_back({Value::temp(3)});
    cbr.brArgs.push_back({Value::temp(3)});
    loop.instructions.push_back(cbr);
    loop.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    exit.params.push_back(Param{"acc", Type(Type::Kind::I64), 1});

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(1));
    exit.instructions.push_back(ret);
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(loop));
    fn.blocks.push_back(std::move(exit));

    module.functions.push_back(std::move(fn));
    return module;
}

void runLiteralCacheScenario(const std::string &literal, int64_t iterations)
{
    Module module = buildLoopModule(literal, iterations);
    il::vm::VM vm(module);

    // After VM construction, the cache should be pre-populated with string literals
    // found in the module to eliminate hot-path lookups during execution.
    assert(il::vm::VMTestHook::literalCacheSize(vm) == 1);
    rt_string prePopulated = il::vm::VMTestHook::literalCacheLookup(vm, literal);
    assert(prePopulated != nullptr);

    // Verify the pre-populated string matches the literal
    assert(rt_str_len(prePopulated) == static_cast<int64_t>(literal.size()));
    const char *data = rt_string_cstr(prePopulated);
    assert(data != nullptr);
    assert(std::memcmp(data, literal.data(), literal.size()) == 0);

    rt_string cachedHandle = prePopulated;
    for (int run = 0; run < 3; ++run)
    {
        int64_t result = vm.run();
        assert(result == iterations);

        // Cache size should remain 1 throughout execution
        assert(il::vm::VMTestHook::literalCacheSize(vm) == 1);

        rt_string current = il::vm::VMTestHook::literalCacheLookup(vm, literal);
        assert(current != nullptr);

        // The same cached handle should be reused across all runs
        assert(current == cachedHandle);
    }
}
} // namespace

int main()
{
    constexpr int64_t iterations = 32;

    const std::string literal = std::string("cache\0literal", 13);
    runLiteralCacheScenario(literal, iterations);

    const std::string asciiLiteral = "foo";
    runLiteralCacheScenario(asciiLiteral, iterations);

    return 0;
}
