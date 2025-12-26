//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_stack_memcpy_ops.cpp
// Purpose: Validate VM load/store against stack-allocated memory use memcpy paths.
// Key invariants: All scalar kinds load/store correctly; misalignment traps covered elsewhere.
// Links: docs/architecture.md
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

#include "VMTestHook.hpp"

#include "viper/runtime/rt.h"

#include <cassert>
#include <cstring>
#include <string>

using namespace il::core;

namespace
{
constexpr il::support::SourceLoc L(unsigned line)
{
    return {1, static_cast<uint32_t>(line), 0};
}

size_t sizeOfKind(Type::Kind k)
{
    switch (k)
    {
        case Type::Kind::I1:
            return sizeof(uint8_t);
        case Type::Kind::I16:
            return sizeof(int16_t);
        case Type::Kind::I32:
            return sizeof(int32_t);
        case Type::Kind::I64:
            return sizeof(int64_t);
        case Type::Kind::F64:
            return sizeof(double);
        case Type::Kind::Ptr:
        case Type::Kind::Error:
        case Type::Kind::ResumeTok:
            return sizeof(void *);
        case Type::Kind::Str:
            return sizeof(rt_string);
        case Type::Kind::Void:
            return 0;
    }
    return 0;
}

void test_scalar_store_load_integer(Type::Kind kind, long long value)
{
    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    const size_t bytes = sizeOfKind(kind);

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<long long>(bytes)));
    alloca.loc = L(1);
    bb.instructions.push_back(alloca);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(kind);
    store.operands.push_back(Value::temp(0));
    if (kind == Type::Kind::I1)
        store.operands.push_back(Value::constBool(value != 0));
    else
        store.operands.push_back(Value::constInt(value));
    store.loc = L(2);
    bb.instructions.push_back(store);

    Instr load;
    load.result = 1U;
    load.op = Opcode::Load;
    load.type = Type(kind);
    load.operands.push_back(Value::temp(0));
    load.loc = L(3);
    bb.instructions.push_back(load);

    // Return zero (we will validate via VMTestHook register inspection)
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = L(4);
    bb.instructions.push_back(ret);
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(2);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    auto &mainFn = m.functions.front();
    auto state = il::vm::VMTestHook::prepare(vm, mainFn);

    auto step = [&]() { return il::vm::VMTestHook::step(vm, state); };
    // alloca
    assert(!step());
    // store
    assert(!step());
    // load
    assert(!step());

    const il::vm::Slot &loaded = state.fr.regs[1U];
    if (kind == Type::Kind::I1)
        assert(loaded.i64 == (value ? 1 : 0));
    else if (kind == Type::Kind::I16)
        assert(loaded.i64 == static_cast<int16_t>(value));
    else if (kind == Type::Kind::I32)
        assert(loaded.i64 == static_cast<int32_t>(value));
    else if (kind == Type::Kind::I64)
        assert(loaded.i64 == value);

    // ret
    assert(step().has_value());
}

void test_f64_store_load(double value)
{
    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<long long>(sizeof(double))));
    alloca.loc = L(1);
    bb.instructions.push_back(alloca);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::F64);
    store.operands.push_back(Value::temp(0));
    store.operands.push_back(Value::constFloat(value));
    store.loc = L(2);
    bb.instructions.push_back(store);

    Instr load;
    load.result = 1U;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::F64);
    load.operands.push_back(Value::temp(0));
    load.loc = L(3);
    bb.instructions.push_back(load);

    // Convert to i64 and return for convenience
    Instr fptosi;
    fptosi.result = 2U;
    fptosi.op = Opcode::Fptosi;
    fptosi.type = Type(Type::Kind::I64);
    fptosi.operands.push_back(Value::temp(1));
    fptosi.loc = L(4);
    bb.instructions.push_back(fptosi);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(2));
    ret.loc = L(5);
    bb.instructions.push_back(ret);
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(3);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    const int64_t exitCode = vm.run();
    assert(exitCode == static_cast<int64_t>(value));
}

void test_ptr_store_load()
{
    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<long long>(sizeof(void *))));
    alloca.loc = L(1);
    bb.instructions.push_back(alloca);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::Ptr);
    store.operands.push_back(Value::temp(0));
    store.operands.push_back(Value::null());
    store.loc = L(2);
    bb.instructions.push_back(store);

    Instr load;
    load.result = 1U;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::Ptr);
    load.operands.push_back(Value::temp(0));
    load.loc = L(3);
    bb.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = L(4);
    bb.instructions.push_back(ret);
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(2);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    auto &mainFn = m.functions.front();
    auto state = il::vm::VMTestHook::prepare(vm, mainFn);
    auto step = [&]() { return il::vm::VMTestHook::step(vm, state); };
    assert(!step()); // alloca
    assert(!step()); // store
    assert(!step()); // load
    assert(state.fr.regs[1U].ptr == nullptr);
    assert(step().has_value()); // ret
}

void test_str_store_load()
{
    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<long long>(sizeof(rt_string))));
    alloca.loc = L(1);
    bb.instructions.push_back(alloca);

    Instr cstr;
    cstr.result = 1U;
    cstr.op = Opcode::ConstStr;
    cstr.type = Type(Type::Kind::Str);
    cstr.operands.push_back(Value::constStr("hello"));
    cstr.loc = L(2);
    bb.instructions.push_back(cstr);

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::Str);
    store.operands.push_back(Value::temp(0));
    store.operands.push_back(Value::temp(1));
    store.loc = L(3);
    bb.instructions.push_back(store);

    Instr load;
    load.result = 2U;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::Str);
    load.operands.push_back(Value::temp(0));
    load.loc = L(4);
    bb.instructions.push_back(load);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = L(6);
    bb.instructions.push_back(ret);
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(4);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    auto &mainFn = m.functions.front();
    auto state = il::vm::VMTestHook::prepare(vm, mainFn);
    auto step = [&]() { return il::vm::VMTestHook::step(vm, state); };

    // Execute: alloca, const.str, store, load
    auto r0 = step();
    assert(!r0); // alloca
    auto r1 = step();
    assert(!r1); // const.str
    auto r2 = step();
    assert(!r2); // store
    auto r3 = step();
    assert(!r3); // load

    rt_string s = state.fr.regs[2U].str;
    assert(s != nullptr);
    const char *c = rt_string_cstr(s);
    assert(c != nullptr);
    assert(std::string(c) == "hello");
    // ret
    assert(step().has_value());
}

void test_hot_loop_i64_store_load()
{
    constexpr int kIters = 5000; // Keep runtime reasonable
    Module m;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock bb;
    bb.label = "entry";

    Instr alloca;
    alloca.result = 0U;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<long long>(sizeof(int64_t))));
    alloca.loc = L(1);
    bb.instructions.push_back(alloca);

    for (int i = 0; i < kIters; ++i)
    {
        Instr s;
        s.op = Opcode::Store;
        s.type = Type(Type::Kind::I64);
        s.operands.push_back(Value::temp(0));
        s.operands.push_back(Value::constInt(i));
        s.loc = L(2);
        bb.instructions.push_back(s);

        Instr l;
        l.result = 1U;
        l.op = Opcode::Load;
        l.type = Type(Type::Kind::I64);
        l.operands.push_back(Value::temp(0));
        l.loc = L(3);
        bb.instructions.push_back(l);
    }

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(1)); // Return last loaded value
    ret.loc = L(4);
    bb.instructions.push_back(ret);
    bb.terminated = true;

    fn.blocks.push_back(std::move(bb));
    fn.valueNames.resize(2);
    m.functions.push_back(std::move(fn));

    il::vm::VM vm(m);
    const int64_t exitCode = vm.run();
    assert(exitCode == kIters - 1);
}
} // namespace

int main()
{
    // Integer-like kinds
    test_scalar_store_load_integer(Type::Kind::I1, 1);
    test_scalar_store_load_integer(Type::Kind::I16, -12345);
    test_scalar_store_load_integer(Type::Kind::I32, -123456789);
    test_scalar_store_load_integer(Type::Kind::I64, 0x1122334455667788LL);

    // Floating-point kind
    test_f64_store_load(42.0);

    // Pointer-like kinds
    test_ptr_store_load();

    // String kind via runtime len
    test_str_store_load();

    // Hot loop stress to guard memcpy perf path in handlers
    test_hot_loop_i64_store_load();

    return 0;
}
