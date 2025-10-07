// File: tests/vm/InlineLiteralCacheTests.cpp
// Purpose: Ensure inline ConstStr operands with embedded NULs reuse cached runtime handles.
// License: MIT License. See LICENSE in project root for details.

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

#include "rt_string.h"

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
} // namespace

int main()
{
    const std::string literal = std::string("cache\0literal", 13);
    constexpr int64_t iterations = 32;

    Module module = buildLoopModule(literal, iterations);
    il::vm::VM vm(module);

    assert(il::vm::VMTestHook::literalCacheSize(vm) == 0);
    assert(il::vm::VMTestHook::literalCacheLookup(vm, literal) == nullptr);

    int64_t first = vm.run();
    assert(first == iterations);

    assert(il::vm::VMTestHook::literalCacheSize(vm) == 1);
    rt_string cached = il::vm::VMTestHook::literalCacheLookup(vm, literal);
    assert(cached != nullptr);
    assert(rt_len(cached) == static_cast<int64_t>(literal.size()));
    const char *data = rt_string_cstr(cached);
    assert(data != nullptr);
    assert(std::memcmp(data, literal.data(), literal.size()) == 0);

    const rt_string cachedBeforeSecond = cached;
    int64_t second = vm.run();
    assert(second == iterations);
    assert(il::vm::VMTestHook::literalCacheSize(vm) == 1);
    rt_string cachedAfter = il::vm::VMTestHook::literalCacheLookup(vm, literal);
    assert(cachedAfter == cachedBeforeSecond);

    return 0;
}
