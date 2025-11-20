// File: tests/vm/ConstNullTests.cpp
// Purpose: Validate const.null initializes destination slots with zero values for each IL kind.
// License: GPL-3.0-only. See LICENSE in the project root for details.

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include "../unit/VMTestHook.hpp"

#include <cassert>

using namespace il::core;
using il::vm::Slot;

namespace
{
Slot runConstNull(Type::Kind kind)
{
    Module module;
    il::build::IRBuilder builder(module);

    auto &fn = builder.startFunction("main", Type(kind), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    Instr cn;
    cn.result = builder.reserveTempId();
    cn.op = Opcode::ConstNull;
    cn.type = Type(kind);
    cn.loc = {1, 1, 1};
    bb.instructions.push_back(cn);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};
    ret.operands.push_back(Value::temp(*cn.result));
    bb.instructions.push_back(ret);

    il::vm::VM vm(module);
    return il::vm::VMTestHook::run(vm, fn, {});
}
} // namespace

int main()
{
    {
        Slot result = runConstNull(Type::Kind::I1);
        assert(result.i64 == 0);
    }

    {
        Slot result = runConstNull(Type::Kind::I16);
        assert(result.i64 == 0);
    }

    {
        Slot result = runConstNull(Type::Kind::I32);
        assert(result.i64 == 0);
    }

    {
        Slot result = runConstNull(Type::Kind::I64);
        assert(result.i64 == 0);
    }

    {
        Slot result = runConstNull(Type::Kind::Error);
        assert(result.i64 == 0);
    }

    {
        Slot result = runConstNull(Type::Kind::ResumeTok);
        assert(result.i64 == 0);
    }

    {
        Slot result = runConstNull(Type::Kind::F64);
        assert(result.f64 == 0.0);
    }

    {
        Slot result = runConstNull(Type::Kind::Ptr);
        assert(result.ptr == nullptr);
    }

    {
        Slot result = runConstNull(Type::Kind::Str);
        assert(result.str == nullptr);
    }

    return 0;
}
